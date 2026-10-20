// Copyright (c) 2026 Robert Wittams
// Licensed under the MIT license (https://opensource.org/license/mit/)

#define BUILD_CONSOLE_INTERFACE 1
#define NEED_ASYNC 0
#define SOCKET_INIT_MANUAL 1
#include "base/base_inc.h"
#include "socket/socket_inc.h"
#include "ipc/ipc.h"
#include "base/base_inc.c"
#include "socket/socket_inc.c"
#include "ipc/ipc.c"
#if OS_MAC
#include <libproc.h>
#endif

#define Check(c) do { if(!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); abort_self(1); } } while(0)

#include "socket/tests/win32_socket_warmup.h"

#if OS_WINDOWS
typedef SOCKET NativeSocket;
#define NativeSocket_Invalid INVALID_SOCKET
#else
typedef int NativeSocket;
#define NativeSocket_Invalid -1
#endif

read_only global U128 instance_id = {.u8 = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16}};
read_only global U256 auth_key = {.u8 = {32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,
                                       48,49,50,51,52,53,54,55,56,57,58,59,60,61,62,63}};
// Independent wire oracle, not produced by the implementation's encoder.
read_only global U8 wire_request[] =
{
  'R','D','I','P', 2,1,0,0, 8,7,6,5,4,3,2,1,
  4,0,0,0,0,0,0,0, 1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,
  221,132,3,119,159,122,82,12,195,124,87,246,226,75,143,200,
  251,120,163,229,246,142,129,98,231,221,123,162,21,7,230,252,
  'a',0,';',255,
  253,133,108,40,196,111,184,156,145,202,254,86,28,158,161,60,
  110,78,194,171,205,198,149,165,3,175,177,208,214,26,89,138,
};
read_only global U8 wire_reply[] =
{
  'R','D','I','P', 2,2,0,0, 8,7,6,5,4,3,2,1,
  6,0,0,0,0,0,0,0, 1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,
  64,165,63,154,201,92,85,108,133,191,172,219,242,178,124,26,
  1,220,215,191,23,26,138,40,146,202,9,58,76,178,90,229,
  'd','o','n','e',13,10,
  121,8,66,226,108,212,138,231,222,239,241,248,159,158,147,2,
  106,201,113,82,97,44,136,24,246,239,55,101,189,199,200,18,
};

// Fixture edits use a one-shot reference composition; the literal tags above
// were calculated independently with Python's standard hmac/hashlib modules.
internal SHA256
test_auth_tag(String8 domain, String8 bytes)
{
  Temp scratch = scratch_begin(0, 0);
  U64 inner_size = 64+domain.size+bytes.size;
  U8 *inner = push_array(scratch.arena, U8, inner_size);
  U8 outer[64+32];
  for(U64 idx = 0; idx < 64; idx += 1)
  {
    U8 key_byte = idx < sizeof(auth_key.u8) ? auth_key.u8[idx] : 0;
    inner[idx] = 0x36 ^ key_byte;
    outer[idx] = 0x5c ^ key_byte;
  }
  MemoryCopy(inner+64, domain.str, domain.size);
  MemoryCopy(inner+64+domain.size, bytes.str, bytes.size);
  SHA256 digest = sha256_from_data(str8(inner, inner_size));
  MemoryCopy(outer+64, digest.u8, sizeof(digest.u8));
  digest = sha256_from_data(str8(outer, sizeof(outer)));
  scratch_end(scratch);
  return digest;
}

internal void
test_sign_frame(U8 *bytes, U64 size)
{
  SHA256 tag = test_auth_tag(str8_lit("RDIP header v2"), str8(bytes, 40));
  MemoryCopy(bytes+40, tag.u8, 32);
  tag = test_auth_tag(str8_lit("RDIP payload v2"), str8(bytes, size-32));
  MemoryCopy(bytes+size-32, tag.u8, 32);
}

internal void
native_close(NativeSocket fd)
{
#if OS_WINDOWS
  closesocket(fd);
#else
  close(fd);
#endif
}

internal NativeSocket
native_configure(NativeSocket fd)
{
#if OS_WINDOWS
  DWORD timeout = 3000;
#else
  struct timeval timeout = {.tv_sec = 3};
#endif
  Check(setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout, sizeof(timeout)) == 0);
  Check(setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, (char *)&timeout, sizeof(timeout)) == 0);
#if OS_MAC
  int no_sigpipe = 1;
  Check(setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &no_sigpipe, sizeof(no_sigpipe)) == 0);
#endif
  return fd;
}

internal NativeSocket
native_connect(SOCK_Endpoint address)
{
  NativeSocket fd = socket(AF_INET, SOCK_STREAM, 0);
  Check(fd != NativeSocket_Invalid);
  native_configure(fd);
  struct sockaddr_in addr = {0};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(address.port);
  MemoryCopy(&addr.sin_addr, address.address_u8, sizeof(addr.sin_addr));
  Check(connect(fd, (struct sockaddr *)&addr, sizeof(addr)) == 0);
  return fd;
}

internal void
native_write(NativeSocket fd, String8 data)
{
  for(U64 pos = 0; pos < data.size;)
  {
    S64 n = send(fd, (char *)data.str+pos, (int)(data.size-pos), 0);
    Check(n > 0);
    pos += (U64)n;
  }
}

internal void
native_read(NativeSocket fd, void *buffer, U64 size)
{
  for(U64 pos = 0; pos < size;)
  {
    S64 n = recv(fd, (char *)buffer+pos, (int)(size-pos), 0);
    Check(n > 0);
    pos += (U64)n;
  }
}

internal void
test_native_wire(void)
{
  Temp scratch = scratch_begin(0, 0);
  IPC_Server *server = ipc_server_open(0, instance_id, auth_key, 0);
  Check(server != 0);
  IPC_Endpoint endpoint = ipc_server_endpoint(server);
  Check(endpoint.address.port != 0 && endpoint.address.address_u8[0] == 127);
  NativeSocket peer = native_connect(endpoint.address);
  native_write(peer, str8(wire_request, sizeof(wire_request)));
  IPC_Message request = {0};
  Check(ipc_server_receive(scratch.arena, server, &request, now_time_us()+Million(3)));
  Check(request.id == 0x0102030405060708 && request.peer.id != 0);
  Check(str8_match(request.data, str8(wire_request+72, 4), 0));
  Check(ipc_server_reply(server, request, str8_lit("done\r\n"), now_time_us()+Million(3)));
  U8 response[sizeof(wire_reply)];
  native_read(peer, response, sizeof(response));
  Check(MemoryMatch(response, wire_reply, sizeof(response)));
  native_close(peer);
  ipc_server_close(server);
  scratch_end(scratch);
}

internal void
test_fragmented_request(void)
{
  Temp scratch = scratch_begin(0, 0);
  IPC_Server *server = ipc_server_open(0, instance_id, auth_key, 0);
  Check(server != 0);
  NativeSocket peer = native_connect(ipc_server_endpoint(server).address);
  IPC_Message request = {.id = 42, .data = str8_lit("unchanged")};
  for(U64 idx = 0; idx < sizeof(wire_request)-1; idx += 1)
  {
    native_write(peer, str8(wire_request+idx, 1));
    Check(!ipc_server_receive(scratch.arena, server, &request, 0));
    Check(request.id == 42 && str8_match(request.data, str8_lit("unchanged"), 0));
  }
  native_write(peer, str8(wire_request+sizeof(wire_request)-1, 1));
  Check(ipc_server_receive(scratch.arena, server, &request, now_time_us()+Million(3)));
  Check(request.id == 0x0102030405060708 && str8_match(request.data, str8(wire_request+72, 4), 0));
  native_close(peer);
  ipc_server_close(server);
  scratch_end(scratch);
}

internal void
test_coalesced_requests(void)
{
  Temp scratch = scratch_begin(0, 0);
  IPC_Server *server = ipc_server_open(0, instance_id, auth_key, 0);
  Check(server != 0);
  NativeSocket peer = native_connect(ipc_server_endpoint(server).address);
  U8 bytes[2*sizeof(wire_request)];
  MemoryCopy(bytes, wire_request, sizeof(wire_request));
  MemoryCopy(bytes+sizeof(wire_request), wire_request, sizeof(wire_request));
  bytes[sizeof(wire_request)+8] = 9;
  bytes[sizeof(wire_request)+72] = 'b';
  test_sign_frame(bytes+sizeof(wire_request), sizeof(wire_request));
  native_write(peer, str8(bytes, sizeof(bytes)));
  IPC_Message first = {0}, second = {0};
  Check(ipc_server_receive(scratch.arena, server, &first, now_time_us()+Million(3)));
  Check(ipc_server_receive(scratch.arena, server, &second, now_time_us()+Million(3)));
  Check(first.id == 0x0102030405060708 && second.id == 0x0102030405060709);
  Check(str8_match(first.data, str8(wire_request+72, 4), 0));
  Check(str8_match(second.data, str8_lit("b\0;\xff"), 0));
  Check(ipc_server_reply(server, second, str8_lit("done\r\n"), now_time_us()+Million(3)));
  Check(ipc_server_reply(server, first, str8_lit("done\r\n"), now_time_us()+Million(3)));
  U8 responses[2*sizeof(wire_reply)], expected[2*sizeof(wire_reply)];
  MemoryCopy(expected, wire_reply, sizeof(wire_reply));
  MemoryCopy(expected+sizeof(wire_reply), wire_reply, sizeof(wire_reply));
  expected[8] = 9;
  test_sign_frame(expected, sizeof(wire_reply));
  native_read(peer, responses, sizeof(responses));
  Check(MemoryMatch(responses, expected, sizeof(responses)));
  native_close(peer);
  ipc_server_close(server);
  scratch_end(scratch);
}

internal void
test_coalesced_empty_burst(void)
{
  Temp scratch = scratch_begin(0, 0);
  IPC_Server *server = ipc_server_open(0, instance_id, auth_key, 0);
  Check(server != 0);
  NativeSocket peer = native_connect(ipc_server_endpoint(server).address);
  U8 bytes[103*104];
  for(U64 idx = 0; idx < 103; idx += 1)
  {
    MemoryCopy(bytes+idx*104, wire_request, 72);
    MemoryZero(bytes+idx*104+8, 16);
    bytes[idx*104+8] = (U8)(idx+1);
    test_sign_frame(bytes+idx*104, 104);
  }
  native_write(peer, str8(bytes, sizeof(bytes)));
  for(U64 idx = 0; idx < 103; idx += 1)
  {
    IPC_Message request = {0};
    Check(ipc_server_receive(scratch.arena, server, &request, now_time_us()+Million(3)));
    Check(request.id == idx+1 && request.data.size == 0);
  }
  native_close(peer);
  ipc_server_close(server);
  scratch_end(scratch);
}

internal void
test_invalid_peer_isolation(void)
{
  Temp scratch = scratch_begin(0, 0);
  for(U64 variant = 0; variant < 9; variant += 1)
  {
    IPC_Server *server = ipc_server_open(0, instance_id, auth_key, 0);
    Check(server != 0);
    SOCK_Endpoint address = ipc_server_endpoint(server).address;
    NativeSocket bad = native_connect(address);
    U8 bytes[sizeof(wire_request)];
    MemoryCopy(bytes, wire_request, sizeof(bytes));
    if(variant < 7) { bytes[72] = 'x'; }
    switch(variant)
    {
      case 0: bytes[0] = 'X'; break;
      case 1: bytes[4] = 1; break;
      case 2: bytes[5] = 2; break;
      case 3: bytes[6] = 1; break;
      case 4: MemoryZero(bytes+8, 8); break;
      case 5: bytes[16] = 1; bytes[19] = 4; break; // 64 MiB + 1.
      case 6: bytes[24] ^= 128; break;
      case 7: bytes[40] ^= 1; break;
      case 8: bytes[sizeof(bytes)-1] ^= 1; break;
    }
    if(variant < 7) { test_sign_frame(bytes, sizeof(bytes)); }
    native_write(bad, str8(bytes, sizeof(bytes)));
    NativeSocket good = native_connect(address);
    native_write(good, str8(wire_request, sizeof(wire_request)));
    IPC_Message request = {0};
    Check(ipc_server_receive(scratch.arena, server, &request, now_time_us()+Million(3)));
    Check(str8_match(request.data, str8(wire_request+72, 4), 0));
    Check(!ipc_server_receive(scratch.arena, server, &request, now_time_us()+20000));
    U8 byte;
    Check(recv(bad, (char *)&byte, 1, 0) == 0);
    Check(ipc_server_reply(server, request, str8_lit("done\r\n"), now_time_us()+Million(3)));
    U8 response[sizeof(wire_reply)];
    native_read(good, response, sizeof(response));
    Check(MemoryMatch(response, wire_reply, sizeof(response)));
    native_close(bad);
    native_close(good);
    ipc_server_close(server);
  }
  scratch_end(scratch);
}

internal void
test_truncated_request(void)
{
  Temp scratch = scratch_begin(0, 0);
  for(U64 variant = 0; variant < 3; variant += 1)
  {
    IPC_Server *server = ipc_server_open(0, instance_id, auth_key, 0);
    Check(server != 0);
    SOCK_Endpoint address = ipc_server_endpoint(server).address;
    NativeSocket bad = native_connect(address);
    native_write(bad, str8(wire_request, variant == 0 ? 20 : variant == 1 ? 75 : sizeof(wire_request)-1));
    native_close(bad);
    NativeSocket good = native_connect(address);
    native_write(good, str8(wire_request, sizeof(wire_request)));
    IPC_Message request = {0};
    Check(ipc_server_receive(scratch.arena, server, &request, now_time_us()+Million(3)));
    Check(str8_match(request.data, str8(wire_request+72, 4), 0));
    Check(!ipc_server_receive(scratch.arena, server, &request, now_time_us()+20000));
    native_close(good);
    ipc_server_close(server);
  }
  scratch_end(scratch);
}

typedef struct RequestFixture RequestFixture;
struct RequestFixture
{
  IPC_Server *server;
  String8 request;
  String8 reply;
  U64 timeout_us;
  U64 request_id;
};

internal void
request_server_thread(void *ptr)
{
  RequestFixture *fixture = ptr;
  Temp scratch = scratch_begin(0, 0);
  U64 endt_us = now_time_us()+(fixture->timeout_us != 0 ? fixture->timeout_us : Million(3));
  IPC_Message request = {0};
  Check(ipc_server_receive(scratch.arena, fixture->server, &request, endt_us));
  fixture->request_id = request.id;
  Check(str8_match(request.data, fixture->request, 0));
  Check(ipc_server_reply(fixture->server, request, fixture->reply, endt_us));
  scratch_end(scratch);
}

internal void
test_large_request_reply(void)
{
  Temp scratch = scratch_begin(0, 0);
  IPC_Server *server = ipc_server_open(0, instance_id, auth_key, 0);
  Check(server != 0);
  String8 request = str8(push_array(scratch.arena, U8, MB(20)), MB(20));
  String8 reply = str8(push_array(scratch.arena, U8, MB(24)), MB(24));
  for(U64 idx = 0; idx < request.size; idx += 1) { request.str[idx] = (U8)(idx%251); }
  for(U64 idx = 0; idx < reply.size; idx += 1) { reply.str[idx] = (U8)(250-idx%251); }
  RequestFixture fixture = {server, request, reply, Million(10)};
  Thread thread = thread_launch(request_server_thread, &fixture);
  Check(thread.u64[0] != 0);
  IPC_Response response = ipc_request(scratch.arena, ipc_server_endpoint(server), request, now_time_us()+Million(10));
  Check(response.status == IPC_Status_Success && str8_match(response.data, reply, 0));
  Check(thread_join(thread, now_time_us()+Million(3)));
  ipc_server_close(server);
  scratch_end(scratch);
}

internal void
test_instances(void)
{
  Temp scratch = scratch_begin(0, 0);
  U128 other = instance_id;
  other.u8[0] ^= 128;
  IPC_Server *a = ipc_server_open(0, instance_id, auth_key, 0);
  IPC_Server *b = ipc_server_open(0, other, auth_key, 0);
  Check(a != 0 && b != 0);
  Check(ipc_server_endpoint(a).address.port != ipc_server_endpoint(b).address.port);
  RequestFixture fixtures[2] = {{a, str8_lit("first"), str8_lit("A")}, {b, str8_lit("second"), str8_lit("B")}};
  Thread threads[2];
  for(U64 idx = 0; idx < 2; idx += 1)
  {
    threads[idx] = thread_launch(request_server_thread, &fixtures[idx]);
    Check(threads[idx].u64[0] != 0);
  }
  for(U64 idx = 0; idx < 2; idx += 1)
  {
    IPC_Response response = ipc_request(scratch.arena, ipc_server_endpoint(fixtures[idx].server), fixtures[idx].request, now_time_us()+Million(3));
    Check(response.status == IPC_Status_Success && str8_match(response.data, fixtures[idx].reply, 0));
    Check(thread_join(threads[idx], now_time_us()+Million(3)));
  }
  ipc_server_close(a);
  ipc_server_close(b);
  scratch_end(scratch);
}

internal void
test_private_discovery(void)
{
  Temp scratch = scratch_begin(0, 0);
  String8 root = {0};
#if OS_WINDOWS
  WCHAR temp_path[MAX_PATH], temp_file[MAX_PATH];
  DWORD size = GetTempPathW(ArrayCount(temp_path), temp_path);
  Check(size != 0 && size < ArrayCount(temp_path));
  Check(GetTempFileNameW(temp_path, L"rad", 0, temp_file) != 0);
  Check(DeleteFileW(temp_file) && CreateDirectoryW(temp_file, 0));
  root = str8_from_16(scratch.arena, str16_cstring((U16 *)temp_file));
#else
  U8 *path = push_cstr(scratch.arena, str8_lit("build/ipc-private.XXXXXX")).str;
  Check(mkdtemp((char *)path) != 0);
  root = str8_cstring((char *)path);
#endif
  String8 directory = str8f(scratch.arena, "%S/endpoint", root);
  IPC_Server *server = ipc_server_open_local(directory, 0);
  Check(server != 0);
  Check(ipc_server_open_local(directory, 0) == 0);
#if OS_WINDOWS
  AlignType(8) U8 public_sid[SECURITY_MAX_SID_SIZE];
  DWORD public_sid_size = sizeof(public_sid);
  Check(CreateWellKnownSid(WinWorldSid, 0, public_sid, &public_sid_size));
  AlignType(8) U8 public_acl[sizeof(ACL)+sizeof(ACCESS_ALLOWED_ACE)+SECURITY_MAX_SID_SIZE];
  Check(InitializeAcl((PACL)public_acl, sizeof(public_acl), ACL_REVISION));
  Check(AddAccessAllowedAce((PACL)public_acl, ACL_REVISION, FILE_ALL_ACCESS, public_sid));
  Check(SetNamedSecurityInfoW(temp_file, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION|PROTECTED_DACL_SECURITY_INFORMATION,
                             0, 0, (PACL)public_acl, 0) == ERROR_SUCCESS);
#else
  Check(chmod((char *)push_cstr(scratch.arena, root).str, 0755) == 0);
#endif
  Check(ipc_server_open_local(root, 0) == 0);
  IPC_Response rejected = ipc_request_local(scratch.arena, root, get_process_info()->pid, str8_lit("not private"), now_time_us()+Million(1));
  Check(rejected.status == IPC_Status_Unavailable && rejected.data.size == 0);
  RequestFixture fixture = {server, str8_lit("private request"), str8_lit("authenticated reply")};
  Thread thread = thread_launch(request_server_thread, &fixture);
  Check(thread.u64[0] != 0);
  IPC_Response response = ipc_request_local(scratch.arena, directory, get_process_info()->pid, fixture.request, now_time_us()+Million(3));
  Check(response.status == IPC_Status_Success && str8_match(response.data, fixture.reply, 0));
  Check(thread_join(thread, now_time_us()+Million(3)));
  ipc_server_close(server);
  response = ipc_request_local(scratch.arena, directory, get_process_info()->pid, fixture.request, now_time_us()+Million(1));
  Check(response.status == IPC_Status_Unavailable && response.data.size == 0);
  server = ipc_server_open_local(directory, 0);
  Check(server != 0);
  fixture = (RequestFixture){server, str8_lit("fresh request"), str8_lit("fresh reply")};
  thread = thread_launch(request_server_thread, &fixture);
  Check(thread.u64[0] != 0);
  response = ipc_request_local(scratch.arena, directory, get_process_info()->pid, fixture.request, now_time_us()+Million(3));
  Check(response.status == IPC_Status_Success && str8_match(response.data, fixture.reply, 0));
  Check(thread_join(thread, now_time_us()+Million(3)));
  ipc_server_close(server);
  scratch_end(scratch);
}

internal void
test_transaction_ids(void)
{
  Temp scratch = scratch_begin(0, 0);
  IPC_Server *server = ipc_server_open(0, instance_id, auth_key, 0);
  Check(server != 0);
  U64 ids[2] = {0};
  for(U64 idx = 0; idx < 2; idx += 1)
  {
    RequestFixture fixture = {server, str8_lit("request"), str8_lit("reply")};
    Thread thread = thread_launch(request_server_thread, &fixture);
    Check(thread.u64[0] != 0);
    IPC_Response response = ipc_request(scratch.arena, ipc_server_endpoint(server), fixture.request, now_time_us()+Million(3));
    Check(response.status == IPC_Status_Success && str8_match(response.data, fixture.reply, 0));
    Check(thread_join(thread, now_time_us()+Million(3)));
    ids[idx] = fixture.request_id;
  }
  Check(ids[0] != 0 && ids[1] != 0 && ids[0] != ids[1]);
  ipc_server_close(server);
  scratch_end(scratch);
}

internal void
test_random_instances(void)
{
  Temp scratch = scratch_begin(0, 0);
  IPC_Server *servers[2] = {ipc_server_open_random(0, 0), ipc_server_open_random(0, 0)};
  Check(servers[0] != 0 && servers[1] != 0);
  IPC_Endpoint endpoints[2] = {ipc_server_endpoint(servers[0]), ipc_server_endpoint(servers[1])};
  for(U64 idx = 0; idx < 2; idx += 1)
  {
    U128 id = endpoints[idx].instance_id;
    U256 key = endpoints[idx].auth_key;
    Check((id.u64[0] | id.u64[1]) != 0);
    Check((key.u64[0] | key.u64[1] | key.u64[2] | key.u64[3]) != 0);
  }
  Check(!MemoryMatch(endpoints[0].instance_id.u8, endpoints[1].instance_id.u8, sizeof(U128)));
  Check(!MemoryMatch(endpoints[0].auth_key.u8, endpoints[1].auth_key.u8, sizeof(U256)));
  RequestFixture fixtures[2] = {{servers[0], str8_lit("first"), str8_lit("A")},
                                {servers[1], str8_lit("second"), str8_lit("B")}};
  for(U64 idx = 0; idx < 2; idx += 1)
  {
    Thread thread = thread_launch(request_server_thread, &fixtures[idx]);
    Check(thread.u64[0] != 0);
    IPC_Response response = ipc_request(scratch.arena, endpoints[idx], fixtures[idx].request, now_time_us()+Million(3));
    Check(response.status == IPC_Status_Success && str8_match(response.data, fixtures[idx].reply, 0));
    Check(thread_join(thread, now_time_us()+Million(3)));
    ipc_server_close(servers[idx]);
  }
  scratch_end(scratch);
}

internal void
test_recycled_endpoint(void)
{
  Temp scratch = scratch_begin(0, 0);
  IPC_Server *old = ipc_server_open(0, instance_id, auth_key, 0);
  Check(old != 0);
  IPC_Endpoint stale = ipc_server_endpoint(old);
  ipc_server_close(old);
  U128 fresh = instance_id;
  fresh.u8[0] ^= 128;
  IPC_Server *server = ipc_server_open(stale.address.port, fresh, auth_key, 0);
  Check(server != 0);
  RequestFixture fixture = {server, str8_lit("fresh"), str8_lit("new instance")};
  Thread thread = thread_launch(request_server_thread, &fixture);
  Check(thread.u64[0] != 0);
  IPC_Response rejected = ipc_request(scratch.arena, stale, str8_lit("stale"), now_time_us()+Million(1));
  Check(rejected.status == IPC_Status_Disconnected && rejected.data.size == 0);
  IPC_Response response = ipc_request(scratch.arena, ipc_server_endpoint(server), fixture.request, now_time_us()+Million(3));
  Check(response.status == IPC_Status_Success && str8_match(response.data, fixture.reply, 0));
  Check(thread_join(thread, now_time_us()+Million(3)));
  ipc_server_close(server);
  scratch_end(scratch);
}

typedef struct ConcurrentClient ConcurrentClient;
struct ConcurrentClient
{
  Barrier start;
  IPC_Endpoint endpoint;
  String8 request;
  String8 reply;
  B32 passed;
};

internal void
concurrent_client_thread(void *ptr)
{
  ConcurrentClient *client = ptr;
  Temp scratch = scratch_begin(0, 0);
  barrier_wait(client->start);
  IPC_Response response = ipc_request(scratch.arena, client->endpoint, client->request, now_time_us()+Million(3));
  client->passed = response.status == IPC_Status_Success && str8_match(response.data, client->reply, 0);
  scratch_end(scratch);
}

internal void
test_concurrent_clients(void)
{
  IPC_Server *servers[2] = {ipc_server_open(0, instance_id, auth_key, 0),
                            ipc_server_open(0, instance_id, auth_key, 0)};
  Check(servers[0] != 0 && servers[1] != 0);
  Barrier start = barrier_alloc(3);
  Check(start.u64[0] != 0);
  RequestFixture replies[2] = {{servers[0], str8_lit("first"), str8_lit("A")},
                                {servers[1], str8_lit("second"), str8_lit("B")}};
  ConcurrentClient clients[2] = {0};
  Thread server_threads[2], client_threads[2];
  for(U64 idx = 0; idx < 2; idx += 1)
  {
    clients[idx] = (ConcurrentClient){start, ipc_server_endpoint(servers[idx]), replies[idx].request, replies[idx].reply};
    server_threads[idx] = thread_launch(request_server_thread, &replies[idx]);
    client_threads[idx] = thread_launch(concurrent_client_thread, &clients[idx]);
    Check(server_threads[idx].u64[0] != 0 && client_threads[idx].u64[0] != 0);
  }
  barrier_wait(start);
  for(U64 idx = 0; idx < 2; idx += 1)
  {
    Check(thread_join(client_threads[idx], now_time_us()+Million(3)));
    Check(thread_join(server_threads[idx], now_time_us()+Million(3)));
    Check(clients[idx].passed);
    ipc_server_close(servers[idx]);
  }
  barrier_release(start);
}

internal void
test_client_rejections(void)
{
  Temp scratch = scratch_begin(0, 0);
  Check(ipc_server_open(0, (U128){0}, auth_key, 0) == 0);
  Check(ipc_server_open(0, instance_id, (U256){0}, 0) == 0);
  IPC_Server *server = ipc_server_open(0, instance_id, auth_key, 0);
  Check(server != 0);
  IPC_Endpoint endpoint = ipc_server_endpoint(server);
  U8 byte = 0;
  IPC_Response response = ipc_request(scratch.arena, endpoint, str8(&byte, MB(64)+1), now_time_us()+Million(3));
  Check(response.status == IPC_Status_TooLarge && response.data.size == 0);
  response = ipc_request_local(scratch.arena, str8_zero(), get_process_info()->pid, str8(&byte, MB(64)+1), now_time_us()+Million(3));
  Check(response.status == IPC_Status_TooLarge && response.data.size == 0);
  response = ipc_request(scratch.arena, endpoint, str8_lit("not sent"), 0);
  Check(response.status == IPC_Status_Timeout && response.data.size == 0);
  endpoint.address.address_u8[0] = 128;
  response = ipc_request(scratch.arena, endpoint, str8_lit("not sent"), now_time_us()+Million(3));
  Check(response.status == IPC_Status_Unavailable && response.data.size == 0);
  endpoint = ipc_server_endpoint(server);
  endpoint.auth_key = (U256){0};
  response = ipc_request(scratch.arena, endpoint, str8_lit("not sent"), now_time_us()+Million(3));
  Check(response.status == IPC_Status_Unavailable && response.data.size == 0);
  IPC_Message request = {0};
  Check(!ipc_server_receive(scratch.arena, server, &request, 0));
  ipc_server_close(server);
  scratch_end(scratch);
}

typedef struct NativeServer NativeServer;
struct NativeServer
{
  NativeSocket listener;
  IPC_Endpoint endpoint;
  U64 reply_size;
  U64 variant;
  U32 delay_ms;
};

internal void
native_server_thread(void *ptr)
{
  NativeServer *server = ptr;
  NativeSocket peer = accept(server->listener, 0, 0);
  Check(peer != NativeSocket_Invalid);
  native_configure(peer);
  U8 request[sizeof(wire_request)], expected[sizeof(wire_request)];
  native_read(peer, request, sizeof(request));
  MemoryCopy(expected, wire_request, sizeof(expected));
  U8 id_bits = 0;
  for(U64 idx = 8; idx < 16; idx += 1) { id_bits |= request[idx]; }
  Check(id_bits != 0);
  MemoryCopy(expected+8, request+8, 8);
  test_sign_frame(expected, sizeof(expected));
  Check(MemoryMatch(request, expected, sizeof(request)));
  U8 reply[sizeof(wire_reply)];
  MemoryCopy(reply, wire_reply, sizeof(reply));
  MemoryCopy(reply+8, request+8, 8);
  switch(server->variant)
  {
    case 0: reply[8] ^= 1; break;
    case 1: reply[24] ^= 128; break;
    case 2: reply[16] = 1; reply[19] = 4; break;
    case 3: reply[5] = 1; break;
  }
  test_sign_frame(reply, sizeof(reply));
  if(server->variant == 7) { reply[40] ^= 1; }
  if(server->variant == 8) { reply[72] ^= 1; }
  if(server->variant == 9) { reply[sizeof(reply)-1] ^= 1; }
  native_write(peer, str8(reply, server->reply_size));
  if(server->delay_ms != 0) { sleep_ms(server->delay_ms); }
  native_close(peer);
}

internal NativeServer
native_server_open(U64 reply_size, U32 delay_ms)
{
  NativeServer server = {0};
  server.listener = socket(AF_INET, SOCK_STREAM, 0);
  Check(server.listener != NativeSocket_Invalid);
  native_configure(server.listener);
  struct sockaddr_in addr = {0};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  Check(bind(server.listener, (struct sockaddr *)&addr, sizeof(addr)) == 0);
  Check(listen(server.listener, 4) == 0);
#if OS_WINDOWS
  int size = sizeof(addr);
#else
  socklen_t size = sizeof(addr);
#endif
  Check(getsockname(server.listener, (struct sockaddr *)&addr, &size) == 0);
  server.endpoint.address.address_u8[0] = 127;
  server.endpoint.address.address_u8[3] = 1;
  server.endpoint.address.port = ntohs(addr.sin_port);
  server.endpoint.instance_id = instance_id;
  server.endpoint.auth_key = auth_key;
  server.reply_size = reply_size;
  server.variant = max_U64;
  server.delay_ms = delay_ms;
  return server;
}

internal void
test_reply_failures(void)
{
  Temp scratch = scratch_begin(0, 0);
  for(U64 variant = 0; variant < 10; variant += 1)
  {
    U64 size = sizeof(wire_reply);
    IPC_Status expected = IPC_Status_InvalidReply;
    switch(variant)
    {
      case 4: size = 7; expected = IPC_Status_Disconnected; break;
      case 5: size = 74; expected = IPC_Status_Disconnected; break;
      case 6: size = sizeof(wire_reply)-1; expected = IPC_Status_Disconnected; break;
    }
    NativeServer server = native_server_open(size, 0);
    server.variant = variant;
    Thread thread = thread_launch(native_server_thread, &server);
    Check(thread.u64[0] != 0);
    IPC_Response response = ipc_request(scratch.arena, server.endpoint, str8(wire_request+72, 4), now_time_us()+Million(3));
    Check(response.status == expected && response.data.size == 0);
    Check(thread_join(thread, now_time_us()+Million(3)));
    native_close(server.listener);
  }
  scratch_end(scratch);
}

internal void
test_reply_deadline(void)
{
  Temp scratch = scratch_begin(0, 0);
  NativeServer server = native_server_open(74, 500);
  Thread thread = thread_launch(native_server_thread, &server);
  Check(thread.u64[0] != 0);
  U64 begin_us = now_time_us();
  IPC_Response response = ipc_request(scratch.arena, server.endpoint, str8(wire_request+72, 4), begin_us+200000);
  Check(response.status == IPC_Status_Timeout && response.data.size == 0);
  Check(now_time_us()-begin_us < Million(1));
  Check(thread_join(thread, now_time_us()+Million(3)));
  native_close(server.listener);
  scratch_end(scratch);
}

internal U64
descriptor_count(void)
{
#if OS_WINDOWS
  DWORD count = 0;
  Check(GetProcessHandleCount(GetCurrentProcess(), &count));
  return count;
#elif OS_LINUX
  DIR *directory = opendir("/proc/self/fd");
  Check(directory != 0);
  U64 count = 0;
  while(readdir(directory) != 0) { count += 1; }
  closedir(directory);
  return count;
#else
  int size = proc_pidinfo(getpid(), PROC_PIDLISTFDS, 0, 0, 0);
  Check(size > 0);
  int cap = size+32*sizeof(struct proc_fdinfo);
  void *buffer = malloc(cap);
  Check(buffer != 0);
  int filled = proc_pidinfo(getpid(), PROC_PIDLISTFDS, 0, buffer, cap);
  Check(filled > 0 && filled < cap);
  free(buffer);
  return (U64)filled/sizeof(struct proc_fdinfo);
#endif
}

internal void
test_public_client(void)
{
  Temp scratch = scratch_begin(0, 0);
  IPC_Server *server = ipc_server_open(0, instance_id, auth_key, 0);
  Check(server != 0);
  RequestFixture fixture = {server, str8_lit("evaluate \"a;b\"\r\n\0\xff"), str8_lit("{\"status\":\"ready\",\"value\":{\"base64\":\"AP8=\"}}")};
  Thread thread = thread_launch(request_server_thread, &fixture);
  Check(thread.u64[0] != 0);
  IPC_Response response = ipc_request(scratch.arena, ipc_server_endpoint(server), fixture.request, now_time_us()+Million(3));
  Check(response.status == IPC_Status_Success && str8_match(response.data, fixture.reply, 0));
  Check(thread_join(thread, now_time_us()+Million(3)));
  ipc_server_close(server);
  scratch_end(scratch);
}

typedef struct RejectedKeyFixture RejectedKeyFixture;
struct RejectedKeyFixture
{
  IPC_Server *server;
  B32 received;
  U64 message_id;
};

internal void
rejected_key_server_thread(void *ptr)
{
  RejectedKeyFixture *fixture = ptr;
  Temp scratch = scratch_begin(0, 0);
  IPC_Message request = {.id = 42};
  U64 endt_us = now_time_us()+200000;
  fixture->received = ipc_server_receive(scratch.arena, fixture->server, &request, endt_us);
  fixture->message_id = request.id;
  if(fixture->received)
  {
    ipc_server_reply(fixture->server, request, str8_lit("unauthorized"), endt_us);
  }
  scratch_end(scratch);
}

internal void
test_wrong_key_isolation(void)
{
  Temp scratch = scratch_begin(0, 0);
  IPC_Server *server = ipc_server_open(0, instance_id, auth_key, 0);
  Check(server != 0);
  IPC_Endpoint wrong = ipc_server_endpoint(server);
  wrong.auth_key.u8[0] ^= 128;
  RejectedKeyFixture rejected = {.server = server};
  Thread thread = thread_launch(rejected_key_server_thread, &rejected);
  Check(thread.u64[0] != 0);
  IPC_Response response = ipc_request(scratch.arena, wrong, str8_lit("not a command"), now_time_us()+Million(1));
  Check(thread_join(thread, now_time_us()+Million(1)));
  Check(!rejected.received && rejected.message_id == 42);
  Check(response.status != IPC_Status_Success && response.data.size == 0);

  RequestFixture valid = {server, str8_lit("valid peer"), str8_lit("still ready")};
  thread = thread_launch(request_server_thread, &valid);
  Check(thread.u64[0] != 0);
  response = ipc_request(scratch.arena, ipc_server_endpoint(server), valid.request, now_time_us()+Million(3));
  Check(response.status == IPC_Status_Success && str8_match(response.data, valid.reply, 0));
  Check(thread_join(thread, now_time_us()+Million(3)));
  ipc_server_close(server);
  scratch_end(scratch);
}

internal void
test_empty_request_reply(void)
{
  Temp scratch = scratch_begin(0, 0);
  IPC_Server *server = ipc_server_open(0, instance_id, auth_key, 0);
  Check(server != 0);
  RequestFixture fixture = {server, str8_zero(), str8_zero()};
  Thread thread = thread_launch(request_server_thread, &fixture);
  Check(thread.u64[0] != 0);
  IPC_Response response = ipc_request(scratch.arena, ipc_server_endpoint(server), str8_zero(), now_time_us()+Million(3));
  Check(response.status == IPC_Status_Success && response.data.size == 0);
  Check(thread_join(thread, now_time_us()+Million(3)));
  ipc_server_close(server);
  scratch_end(scratch);
}

internal void
test_payload_tamper_isolation(void)
{
  Temp scratch = scratch_begin(0, 0);
  IPC_Server *server = ipc_server_open(0, instance_id, auth_key, 0);
  Check(server != 0);
  SOCK_Endpoint address = ipc_server_endpoint(server).address;
  NativeSocket bad = native_connect(address);
  U8 tampered[sizeof(wire_request)];
  MemoryCopy(tampered, wire_request, sizeof(tampered));
  tampered[72] = 'z';
  native_write(bad, str8(tampered, sizeof(tampered)));
  NativeSocket good = native_connect(address);
  native_write(good, str8(wire_request, sizeof(wire_request)));
  IPC_Message request = {0};
  Check(ipc_server_receive(scratch.arena, server, &request, now_time_us()+Million(3)));
  Check(str8_match(request.data, str8(wire_request+72, 4), 0));
  Check(!ipc_server_receive(scratch.arena, server, &request, now_time_us()+20000));
  U8 byte;
  Check(recv(bad, (char *)&byte, 1, 0) == 0);
  native_close(bad);
  native_close(good);
  ipc_server_close(server);
  scratch_end(scratch);
}

global U64 cases_run;

internal void
run_case(CmdLine *cmdline, char *name, void (*function)(void))
{
  String8 selected = cmd_line_string(cmdline, str8_lit("case"));
  if(selected.size == 0 || str8_match(selected, str8_cstring(name), 0))
  {
    function();
    cases_run += 1;
    fprintf(stdout, "PASS %s\n", name);
    fflush(stdout);
  }
}

internal void
entry_point(CmdLine *cmdline)
{
  sock_init();
#if OS_WINDOWS
  warm_native_socket_runtime();
  // Native BCrypt controls also retain process-wide initialization handles.
  // Prime the production byte counts without creating any RAD server/client.
  U8 entropy[48];
  Check(BCryptGenRandom(0, entropy, sizeof(entropy), BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0);
  Check(BCryptGenRandom(0, entropy, sizeof(U64), BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0);
  // A native-only GetSecurityInfo control retains its own initialization handles.
  WCHAR executable[32768];
  DWORD executable_size = GetModuleFileNameW(0, executable, ArrayCount(executable));
  Check(executable_size != 0 && executable_size < ArrayCount(executable));
  HANDLE security_file = CreateFileW(executable, READ_CONTROL, FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
                                    0, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, 0);
  Check(security_file != INVALID_HANDLE_VALUE);
  PSID owner = 0;
  PACL acl = 0;
  PSECURITY_DESCRIPTOR descriptor = 0;
  Check(GetSecurityInfo(security_file, SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION|DACL_SECURITY_INFORMATION,
                        &owner, 0, &acl, 0, &descriptor) == ERROR_SUCCESS);
  LocalFree(descriptor);
  Check(CloseHandle(security_file));
#endif
  U64 before = descriptor_count();
  run_case(cmdline, "native_wire", test_native_wire);
  run_case(cmdline, "fragmented_request", test_fragmented_request);
  run_case(cmdline, "coalesced_requests", test_coalesced_requests);
  run_case(cmdline, "coalesced_empty_burst", test_coalesced_empty_burst);
  run_case(cmdline, "invalid_peer_isolation", test_invalid_peer_isolation);
  run_case(cmdline, "truncated_request", test_truncated_request);
  run_case(cmdline, "public_client", test_public_client);
  run_case(cmdline, "wrong_key_isolation", test_wrong_key_isolation);
  run_case(cmdline, "payload_tamper_isolation", test_payload_tamper_isolation);
  run_case(cmdline, "empty_request_reply", test_empty_request_reply);
  run_case(cmdline, "large_request_reply", test_large_request_reply);
  run_case(cmdline, "instances", test_instances);
  run_case(cmdline, "random_instances", test_random_instances);
  run_case(cmdline, "private_discovery", test_private_discovery);
  run_case(cmdline, "transaction_ids", test_transaction_ids);
  run_case(cmdline, "concurrent_clients", test_concurrent_clients);
  run_case(cmdline, "recycled_endpoint", test_recycled_endpoint);
  run_case(cmdline, "client_rejections", test_client_rejections);
  run_case(cmdline, "reply_failures", test_reply_failures);
  run_case(cmdline, "reply_deadline", test_reply_deadline);
  Check(cases_run != 0);
  Check(descriptor_count() == before);
}
