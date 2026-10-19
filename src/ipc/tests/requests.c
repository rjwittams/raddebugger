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
// Independent wire oracle, not produced by the implementation's encoder.
read_only global U8 wire_request[] =
{
  'R','D','I','P', 1,1,0,0, 8,7,6,5,4,3,2,1,
  4,0,0,0,0,0,0,0, 1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,
  'a',0,';',255,
};
read_only global U8 wire_reply[] =
{
  'R','D','I','P', 1,2,0,0, 8,7,6,5,4,3,2,1,
  6,0,0,0,0,0,0,0, 1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,
  'd','o','n','e',13,10,
};

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
  IPC_Server *server = ipc_server_open(0, instance_id, 0);
  Check(server != 0);
  IPC_Endpoint endpoint = ipc_server_endpoint(server);
  Check(endpoint.address.port != 0 && endpoint.address.address_u8[0] == 127);
  NativeSocket peer = native_connect(endpoint.address);
  native_write(peer, str8(wire_request, sizeof(wire_request)));
  IPC_Message request = {0};
  Check(ipc_server_receive(scratch.arena, server, &request, now_time_us()+Million(3)));
  Check(request.id == 0x0102030405060708 && request.peer.id != 0);
  Check(str8_match(request.data, str8(wire_request+40, 4), 0));
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
  IPC_Server *server = ipc_server_open(0, instance_id, 0);
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
  Check(request.id == 0x0102030405060708 && str8_match(request.data, str8(wire_request+40, 4), 0));
  native_close(peer);
  ipc_server_close(server);
  scratch_end(scratch);
}

internal void
test_coalesced_requests(void)
{
  Temp scratch = scratch_begin(0, 0);
  IPC_Server *server = ipc_server_open(0, instance_id, 0);
  Check(server != 0);
  NativeSocket peer = native_connect(ipc_server_endpoint(server).address);
  U8 bytes[2*sizeof(wire_request)];
  MemoryCopy(bytes, wire_request, sizeof(wire_request));
  MemoryCopy(bytes+sizeof(wire_request), wire_request, sizeof(wire_request));
  bytes[sizeof(wire_request)+8] = 9;
  bytes[sizeof(wire_request)+40] = 'b';
  native_write(peer, str8(bytes, sizeof(bytes)));
  IPC_Message first = {0}, second = {0};
  Check(ipc_server_receive(scratch.arena, server, &first, now_time_us()+Million(3)));
  Check(ipc_server_receive(scratch.arena, server, &second, now_time_us()+Million(3)));
  Check(first.id == 0x0102030405060708 && second.id == 0x0102030405060709);
  Check(str8_match(first.data, str8(wire_request+40, 4), 0));
  Check(str8_match(second.data, str8_lit("b\0;\xff"), 0));
  Check(ipc_server_reply(server, second, str8_lit("done\r\n"), now_time_us()+Million(3)));
  Check(ipc_server_reply(server, first, str8_lit("done\r\n"), now_time_us()+Million(3)));
  U8 responses[2*sizeof(wire_reply)], expected[2*sizeof(wire_reply)];
  MemoryCopy(expected, wire_reply, sizeof(wire_reply));
  MemoryCopy(expected+sizeof(wire_reply), wire_reply, sizeof(wire_reply));
  expected[8] = 9;
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
  IPC_Server *server = ipc_server_open(0, instance_id, 0);
  Check(server != 0);
  NativeSocket peer = native_connect(ipc_server_endpoint(server).address);
  U8 bytes[103*40];
  for(U64 idx = 0; idx < 103; idx += 1)
  {
    MemoryCopy(bytes+idx*40, wire_request, 40);
    MemoryZero(bytes+idx*40+8, 16);
    bytes[idx*40+8] = (U8)(idx+1);
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
  for(U64 variant = 0; variant < 7; variant += 1)
  {
    IPC_Server *server = ipc_server_open(0, instance_id, 0);
    Check(server != 0);
    SOCK_Endpoint address = ipc_server_endpoint(server).address;
    NativeSocket bad = native_connect(address);
    U8 bytes[sizeof(wire_request)];
    MemoryCopy(bytes, wire_request, sizeof(bytes));
    bytes[40] = 'x';
    switch(variant)
    {
      case 0: bytes[0] = 'X'; break;
      case 1: bytes[4] = 2; break;
      case 2: bytes[5] = 2; break;
      case 3: bytes[6] = 1; break;
      case 4: MemoryZero(bytes+8, 8); break;
      case 5: bytes[16] = 1; bytes[19] = 4; break; // 64 MiB + 1.
      case 6: bytes[24] ^= 128; break;
    }
    native_write(bad, str8(bytes, sizeof(bytes)));
    NativeSocket good = native_connect(address);
    native_write(good, str8(wire_request, sizeof(wire_request)));
    IPC_Message request = {0};
    Check(ipc_server_receive(scratch.arena, server, &request, now_time_us()+Million(3)));
    Check(str8_match(request.data, str8(wire_request+40, 4), 0));
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
  for(U64 variant = 0; variant < 2; variant += 1)
  {
    IPC_Server *server = ipc_server_open(0, instance_id, 0);
    Check(server != 0);
    SOCK_Endpoint address = ipc_server_endpoint(server).address;
    NativeSocket bad = native_connect(address);
    native_write(bad, str8(wire_request, variant == 0 ? 20 : 43));
    native_close(bad);
    NativeSocket good = native_connect(address);
    native_write(good, str8(wire_request, sizeof(wire_request)));
    IPC_Message request = {0};
    Check(ipc_server_receive(scratch.arena, server, &request, now_time_us()+Million(3)));
    Check(str8_match(request.data, str8(wire_request+40, 4), 0));
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
};

internal void
request_server_thread(void *ptr)
{
  RequestFixture *fixture = ptr;
  Temp scratch = scratch_begin(0, 0);
  U64 endt_us = now_time_us()+(fixture->timeout_us != 0 ? fixture->timeout_us : Million(3));
  IPC_Message request = {0};
  Check(ipc_server_receive(scratch.arena, fixture->server, &request, endt_us));
  Check(str8_match(request.data, fixture->request, 0));
  Check(ipc_server_reply(fixture->server, request, fixture->reply, endt_us));
  scratch_end(scratch);
}

internal void
test_large_request_reply(void)
{
  Temp scratch = scratch_begin(0, 0);
  IPC_Server *server = ipc_server_open(0, instance_id, 0);
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
  IPC_Server *a = ipc_server_open(0, instance_id, 0);
  IPC_Server *b = ipc_server_open(0, other, 0);
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
test_recycled_endpoint(void)
{
  Temp scratch = scratch_begin(0, 0);
  IPC_Server *old = ipc_server_open(0, instance_id, 0);
  Check(old != 0);
  IPC_Endpoint stale = ipc_server_endpoint(old);
  ipc_server_close(old);
  U128 fresh = instance_id;
  fresh.u8[0] ^= 128;
  IPC_Server *server = ipc_server_open(stale.address.port, fresh, 0);
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

internal void
test_client_rejections(void)
{
  Temp scratch = scratch_begin(0, 0);
  Check(ipc_server_open(0, (U128){0}, 0) == 0);
  IPC_Server *server = ipc_server_open(0, instance_id, 0);
  Check(server != 0);
  IPC_Endpoint endpoint = ipc_server_endpoint(server);
  U8 byte = 0;
  IPC_Response response = ipc_request(scratch.arena, endpoint, str8(&byte, MB(64)+1), now_time_us()+Million(3));
  Check(response.status == IPC_Status_TooLarge && response.data.size == 0);
  response = ipc_request(scratch.arena, endpoint, str8_lit("not sent"), 0);
  Check(response.status == IPC_Status_Timeout && response.data.size == 0);
  endpoint.address.address_u8[0] = 128;
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
  String8 reply;
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
  MemoryZero(expected+8, 8);
  expected[8] = 1;
  Check(MemoryMatch(request, expected, sizeof(request)));
  native_write(peer, server->reply);
  if(server->delay_ms != 0) { sleep_ms(server->delay_ms); }
  native_close(peer);
}

internal NativeServer
native_server_open(String8 reply, U32 delay_ms)
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
  server.reply = reply;
  server.delay_ms = delay_ms;
  return server;
}

internal void
test_reply_failures(void)
{
  Temp scratch = scratch_begin(0, 0);
  for(U64 variant = 0; variant < 6; variant += 1)
  {
    U8 bytes[sizeof(wire_reply)];
    MemoryCopy(bytes, wire_reply, sizeof(bytes));
    MemoryZero(bytes+8, 8);
    bytes[8] = 1;
    U64 size = sizeof(bytes);
    IPC_Status expected = IPC_Status_InvalidReply;
    switch(variant)
    {
      case 0: bytes[8] = 2; break;
      case 1: bytes[24] ^= 128; break;
      case 2: bytes[16] = 1; bytes[19] = 4; break;
      case 3: bytes[5] = 1; break;
      case 4: size = 7; expected = IPC_Status_Disconnected; break;
      case 5: size = 42; expected = IPC_Status_Disconnected; break;
    }
    NativeServer server = native_server_open(str8(bytes, size), 0);
    Thread thread = thread_launch(native_server_thread, &server);
    Check(thread.u64[0] != 0);
    IPC_Response response = ipc_request(scratch.arena, server.endpoint, str8(wire_request+40, 4), now_time_us()+Million(3));
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
  U8 bytes[sizeof(wire_reply)];
  MemoryCopy(bytes, wire_reply, sizeof(bytes));
  MemoryZero(bytes+8, 8);
  bytes[8] = 1;
  NativeServer server = native_server_open(str8(bytes, 42), 500);
  Thread thread = thread_launch(native_server_thread, &server);
  Check(thread.u64[0] != 0);
  U64 begin_us = now_time_us();
  IPC_Response response = ipc_request(scratch.arena, server.endpoint, str8(wire_request+40, 4), begin_us+200000);
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
  IPC_Server *server = ipc_server_open(0, instance_id, 0);
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
#endif
  U64 before = descriptor_count();
  run_case(cmdline, "native_wire", test_native_wire);
  run_case(cmdline, "fragmented_request", test_fragmented_request);
  run_case(cmdline, "coalesced_requests", test_coalesced_requests);
  run_case(cmdline, "coalesced_empty_burst", test_coalesced_empty_burst);
  run_case(cmdline, "invalid_peer_isolation", test_invalid_peer_isolation);
  run_case(cmdline, "truncated_request", test_truncated_request);
  run_case(cmdline, "public_client", test_public_client);
  run_case(cmdline, "large_request_reply", test_large_request_reply);
  run_case(cmdline, "instances", test_instances);
  run_case(cmdline, "recycled_endpoint", test_recycled_endpoint);
  run_case(cmdline, "client_rejections", test_client_rejections);
  run_case(cmdline, "reply_failures", test_reply_failures);
  run_case(cmdline, "reply_deadline", test_reply_deadline);
  Check(cases_run != 0);
  Check(descriptor_count() == before);
}
