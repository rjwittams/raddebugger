// Copyright (c) 2026 Robert Wittams
// Licensed under the MIT license (https://opensource.org/license/mit/)

#define BUILD_CONSOLE_INTERFACE 1
#define NEED_ASYNC 0
#include "base/base_inc.h"
#include "socket/socket_inc.h"
#include "base/base_inc.c"
#include "socket/socket_inc.c"

#if OS_MAC
# include <libproc.h>
#endif

#define Check(c) do { if(!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); exit(1); } } while(0)

#include "win32_socket_warmup.h"

internal U64
native_handle_count(void)
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
#elif OS_MAC
  int size = proc_pidinfo(getpid(), PROC_PIDLISTFDS, 0, 0, 0);
  Check(size > 0);
  int capacity = size+32*sizeof(struct proc_fdinfo);
  void *buffer = malloc(capacity);
  Check(buffer != 0);
  int filled = proc_pidinfo(getpid(), PROC_PIDLISTFDS, 0, buffer, capacity);
  Check(filled > 0 && filled < capacity);
  free(buffer);
  return (U64)filled/sizeof(struct proc_fdinfo);
#endif
}

internal void
test_loopback_listener(void)
{
  SOCK_Session session = sock_session_open(0, 0);
  Check(session.u64[0] != 0);
  SOCK_Endpoint endpoint = sock_session_listener_endpoint(session);
  Check(endpoint.kind == SOCK_EndpointKind_IPv4 && endpoint.port != 0);
  Check(endpoint.address_u8[0] == 127 && endpoint.address_u8[1] == 0 &&
        endpoint.address_u8[2] == 0 && endpoint.address_u8[3] == 1);
  sock_session_close(session);
  fprintf(stdout, "PASS loopback_listener\n");
}

internal void
test_occupied_port(void)
{
  SOCK_Session session = sock_session_open(0, 0);
  Check(session.u64[0] != 0);
  SOCK_Endpoint endpoint = sock_session_listener_endpoint(session);
  Check(endpoint.port != 0);
  U64 before = native_handle_count();
  for(U64 index = 0; index < 64; index += 1)
  {
    SOCK_Session duplicate = sock_session_open(endpoint.port, 0);
    Check(duplicate.u64[0] == 0);
  }
  Check(native_handle_count() == before);
  sock_session_close(session);
  fprintf(stdout, "PASS occupied_port\n");
}

internal void
test_listener_separation(void)
{
  Temp scratch = scratch_begin(0, 0);
  SOCK_Session sessions[2] = {sock_session_open(0, 0), sock_session_open(0, 0)};
  Check(sessions[0].u64[0] != 0 && sessions[1].u64[0] != 0);
  SOCK_Endpoint endpoints[2] = {sock_session_listener_endpoint(sessions[0]), sock_session_listener_endpoint(sessions[1])};
  Check(endpoints[0].port != 0 && endpoints[1].port != 0 && endpoints[0].port != endpoints[1].port);
#if OS_WINDOWS
  SOCKET peers[2];
#else
  int peers[2];
#endif
  for(U64 index = 0; index < 2; index += 1)
  {
#if OS_WINDOWS
    SOCKET peer = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    Check(peer != INVALID_SOCKET);
#else
    int peer = socket(AF_INET, SOCK_STREAM, 0);
    Check(peer != -1);
#endif
    peers[index] = peer;
    struct sockaddr_in address = {0};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(endpoints[index].port);
    Check(connect(peer, (struct sockaddr *)&address, sizeof(address)) == 0);
    char payload[8] = {'A'+(char)index, '\0', '\r', '\n', '\xff', '\x80', 'X', 'Y'};
    Check(send(peer, payload, sizeof(payload), 0) == sizeof(payload));
    U64 received = 0;
    while(received < sizeof(payload))
    {
      SOCK_Protocol protocol;
      SOCK_Endpoint from;
      String8 data;
      Check(sock_recv(scratch.arena, sessions[index], &protocol, &from, &data, now_time_us()+Million(3)));
      Check(protocol == SOCK_Protocol_TCP && data.size > 0 && data.size <= sizeof(payload)-received);
      Check(MemoryMatch(data.str, payload+received, data.size));
      received += data.size;
    }
    SOCK_Protocol protocol;
    SOCK_Endpoint from;
    String8 data;
    Check(!sock_recv(scratch.arena, sessions[1-index], &protocol, &from, &data, 0));
  }
  for(U64 index = 0; index < 2; index += 1)
  {
#if OS_WINDOWS
    closesocket(peers[index]);
#else
    close(peers[index]);
#endif
  }
  sock_session_close(sessions[0]);
  sock_session_close(sessions[1]);
  scratch_end(scratch);
  fprintf(stdout, "PASS listener_separation\n");
}

internal void
test_exclusive_endpoint(void)
{
  SOCK_Session session = sock_session_open(0, 0);
  Check(session.u64[0] != 0);
  SOCK_Endpoint endpoint = sock_session_listener_endpoint(session);
  Check(endpoint.port != 0);
#if OS_WINDOWS
  SOCKET contender = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  Check(contender != INVALID_SOCKET);
#else
  int contender = socket(AF_INET, SOCK_STREAM, 0);
  Check(contender != -1);
#endif
  int reuse = 1;
  Check(setsockopt(contender, SOL_SOCKET, SO_REUSEADDR, (char *)&reuse, sizeof(reuse)) == 0);
  struct sockaddr_in address = {0};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  address.sin_port = htons(endpoint.port);
  int code = bind(contender, (struct sockaddr *)&address, sizeof(address));
#if OS_WINDOWS
  int error = WSAGetLastError();
  closesocket(contender);
  Check(code == SOCKET_ERROR && (error == WSAEADDRINUSE || error == WSAEACCES));
#else
  int error = errno;
  close(contender);
  Check(code == -1 && error == EADDRINUSE);
#endif
  sock_session_close(session);
  fprintf(stdout, "PASS exclusive_endpoint\n");
}

internal void
entry_point(CmdLine *cmdline)
{
#if OS_WINDOWS
  warm_native_socket_runtime();
#endif
  String8 selected = cmd_line_string(cmdline, str8_lit("case"));
  if(str8_match(selected, str8_lit("loopback_listener"), 0)) { test_loopback_listener(); }
  else if(str8_match(selected, str8_lit("occupied_port"), 0)) { test_occupied_port(); }
  else if(str8_match(selected, str8_lit("listener_separation"), 0)) { test_listener_separation(); }
  else if(str8_match(selected, str8_lit("exclusive_endpoint"), 0)) { test_exclusive_endpoint(); }
  else { fprintf(stderr, "Unknown or missing listener test case\n"); exit(1); }
}
