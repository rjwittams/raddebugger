// Copyright (c) 2026 Robert Wittams
// Licensed under the MIT license (https://opensource.org/license/mit/)

#define BUILD_CONSOLE_INTERFACE 1
#define NEED_ASYNC 0
#define SOCKET_INIT_MANUAL 1
#include "base/base_inc.h"
#include "socket/socket_inc.h"
#include "base/base_inc.c"
#include "socket/socket_inc.c"

#include <sys/socket.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/resource.h>
#if OS_MAC
# include <libproc.h>
#endif

#define Check(c) do { if(!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); abort_self(1); } } while(0)

internal int
peer_connect(SOCK_Endpoint ep)
{
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  Check(fd != -1);
  struct timeval timeout = {.tv_sec = 3};
  Check(setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) == 0);
  Check(setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) == 0);
#if OS_MAC
  int no_sigpipe = 1;
  Check(setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &no_sigpipe, sizeof(no_sigpipe)) == 0);
#endif
  struct sockaddr_in addr = {0};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(ep.port);
  MemoryCopy(&addr.sin_addr, ep.address_u8, sizeof(addr.sin_addr));
  Check(connect(fd, (struct sockaddr *)&addr, sizeof(addr)) == 0);
  return fd;
}

internal void
test_listener(void)
{
  SOCK_Session session = sock_session_open(0, 0);
  Check(session.u64[0] != 0);
  SOCK_Endpoint ep = sock_session_listener_endpoint(session);
  Check(ep.kind == SOCK_EndpointKind_IPv4 && ep.port != 0);
  Check(ep.address_u8[0] == 127 && ep.address_u8[1] == 0 &&
        ep.address_u8[2] == 0 && ep.address_u8[3] == 1);
  sock_session_close(session);
}

internal void
test_round_trip(void)
{
  Temp scratch = scratch_begin(0, 0);
  SOCK_Session session = sock_session_open(0, 0);
  Check(session.u64[0] != 0);
  int peer = peer_connect(sock_session_listener_endpoint(session));
  Check(send(peer, "request", 7, 0) == 7);
  SOCK_Protocol protocol = SOCK_Protocol_UDP;
  SOCK_Endpoint from = {0};
  String8 data = {0};
  Check(sock_recv(scratch.arena, session, &protocol, &from, &data, now_time_us()+Million(3)));
  Check(protocol == SOCK_Protocol_TCP && str8_match(data, str8_lit("request"), 0));
  Check(sock_send(session, protocol, from, str8_lit("response"), now_time_us()+Million(3)));
  U8 reply[8];
  U64 received = 0;
  while(received < sizeof(reply))
  {
    ssize_t n = recv(peer, reply+received, sizeof(reply)-received, 0);
    Check(n > 0);
    received += (U64)n;
  }
  Check(MemoryMatch(reply, "response", sizeof(reply)));
  close(peer);
  sock_session_close(session);
  scratch_end(scratch);
}

internal void
test_outgoing_and_eof(void)
{
  Temp scratch = scratch_begin(0, 0);
  SOCK_Session a = sock_session_open(0, 0);
  SOCK_Session b = sock_session_open(0, 0);
  Check(a.u64[0] != 0 && b.u64[0] != 0);
  SOCK_Endpoint ep_a = sock_session_listener_endpoint(a);
  SOCK_Endpoint ep_b = sock_session_listener_endpoint(b);
  Check(ep_a.port != ep_b.port);
  Check(sock_send(a, SOCK_Protocol_TCP, ep_b, str8_lit("first"), now_time_us()+Million(3)));
  SOCK_Protocol protocol;
  SOCK_Endpoint from = {0};
  String8 data = {0};
  Check(sock_recv(scratch.arena, b, &protocol, &from, &data, now_time_us()+Million(3)));
  Check(str8_match(data, str8_lit("first"), 0));
  Check(sock_send(b, protocol, from, str8_lit("second"), now_time_us()+Million(3)));
  Check(sock_recv(scratch.arena, a, &protocol, &from, &data, now_time_us()+Million(3)));
  Check(str8_match(data, str8_lit("second"), 0));
  Check(MemoryMatchStruct(&from, &ep_b));
  sock_session_close(a);
  Check(sock_recv(scratch.arena, b, &protocol, &from, &data, now_time_us()+Million(3)));
  Check(data.size == 0);
  Check(!sock_recv(scratch.arena, b, &protocol, &from, &data, now_time_us()+20000));
  sock_session_close(b);
  scratch_end(scratch);
}

internal void
peer_write(int fd, String8 data)
{
  for(U64 pos = 0; pos < data.size;)
  {
    ssize_t n = send(fd, data.str+pos, data.size-pos, 0);
    Check(n > 0);
    pos += (U64)n;
  }
}

internal void
peer_read(int fd, String8 data)
{
  for(U64 pos = 0; pos < data.size;)
  {
    ssize_t n = recv(fd, data.str+pos, data.size-pos, 0);
    Check(n > 0);
    pos += (U64)n;
  }
}

internal void
test_large_duplex(void)
{
  Temp scratch = scratch_begin(0, 0);
  U64 size = MB(8);
  U8 *request = push_array(scratch.arena, U8, size);
  U8 *response = push_array(scratch.arena, U8, size);
  U8 *received = push_array(scratch.arena, U8, size);
  for(U64 pos = 0; pos < size; pos += 8)
  {
    MemoryCopy(request+pos, "req\0\r\n\xff\x80", 8);
    MemoryCopy(response+pos, "R\0\r\n\xfe\x81XY", 8);
  }
  SOCK_Session session = sock_session_open(0, 0);
  Check(session.u64[0] != 0);
  int peer = peer_connect(sock_session_listener_endpoint(session));
  // Discover the accepted peer without reaching into the provider's storage.
  peer_write(peer, str8_lit("start"));
  SOCK_Protocol protocol;
  SOCK_Endpoint from;
  String8 data;
  Check(sock_recv(scratch.arena, session, &protocol, &from, &data, now_time_us()+Million(3)));
  Check(str8_match(data, str8_lit("start"), 0));
  Check(sock_send(session, protocol, from, str8(response, size), now_time_us()+Million(3)));
  // Finish writing before reading, so stalled output must not stall input.
  peer_write(peer, str8(request, size));
  peer_read(peer, str8(received, size));
  Check(MemoryMatch(received, response, size));
  U64 pos = 0;
  while(pos < size)
  {
    Check(sock_recv(scratch.arena, session, &protocol, &from, &data, now_time_us()+Million(3)));
    Check(data.size > 0 && data.size <= size-pos);
    Check(MemoryMatch(data.str, request+pos, data.size));
    pos += data.size;
  }
  close(peer);
  sock_session_close(session);
  scratch_end(scratch);
}

internal void
test_backpressure_close(void)
{
  Temp scratch = scratch_begin(0, 0);
  SOCK_Session a = sock_session_open(0, 0);
  SOCK_Session b = sock_session_open(0, 0);
  Check(a.u64[0] != 0 && b.u64[0] != 0);
  SOCK_Endpoint ep = sock_session_listener_endpoint(b);
  String8 data = str8(push_array(scratch.arena, U8, MB(1)), MB(1));
  B32 bounded = 0;
  for(U64 idx = 0; idx < 128; idx += 1)
  {
    if(!sock_send(a, SOCK_Protocol_TCP, ep, data, now_time_us()+20000))
    {
      bounded = 1;
      break;
    }
  }
  Check(bounded);
  U64 before = now_time_us();
  sock_session_close(a);
  sock_session_close(b);
  Check(now_time_us()-before < Million(3));
  scratch_end(scratch);
}

internal U64
cpu_time_us(void)
{
  struct rusage usage = {0};
  Check(getrusage(RUSAGE_SELF, &usage) == 0);
  return (usage.ru_utime.tv_sec+usage.ru_stime.tv_sec)*Million(1)+
    usage.ru_utime.tv_usec+usage.ru_stime.tv_usec;
}

internal void
test_duplex_backpressure_disconnect(void)
{
  Temp scratch = scratch_begin(0, 0);
  SOCK_Session a = sock_session_open(0, 0);
  SOCK_Session b = sock_session_open(0, 0);
  Check(a.u64[0] != 0 && b.u64[0] != 0);
  SOCK_Endpoint ep = sock_session_listener_endpoint(b);
  String8 payload = str8(push_array(scratch.arena, U8, MB(1)), MB(1));
  B32 bounded = 0;
  for(U64 idx = 0; idx < 128; idx += 1)
  {
    if(!sock_send(a, SOCK_Protocol_TCP, ep, payload, now_time_us()+20000)) { bounded = 1; break; }
  }
  Check(bounded);
  SOCK_Protocol protocol;
  SOCK_Endpoint from;
  String8 data;
  Check(sock_recv(scratch.arena, b, &protocol, &from, &data, now_time_us()+Million(3)));
  bounded = 0;
  for(U64 idx = 0; idx < 128; idx += 1)
  {
    if(!sock_send(b, protocol, from, payload, now_time_us()+20000)) { bounded = 1; break; }
  }
  Check(bounded);
  sock_session_close(a);
  U64 before = cpu_time_us();
  sleep_ms(100);
  U64 idle_cpu = cpu_time_us()-before;
  sock_session_close(b);
  Check(idle_cpu < 50000);
  scratch_end(scratch);
}

internal U64
descriptor_count(void)
{
#if OS_LINUX
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
test_close_and_bind_failure(void)
{
  U64 before = descriptor_count();
  for(U64 idx = 0; idx < 64; idx += 1)
  {
    SOCK_Session s = sock_session_open(0, 0);
    Check(s.u64[0] != 0);
    SOCK_Endpoint ep = sock_session_listener_endpoint(s);
    SOCK_Session duplicate = sock_session_open(ep.port, 0);
    Check(duplicate.u64[0] == 0);
    sock_session_close(s);
    s = sock_session_open(ep.port, 0);
    Check(s.u64[0] != 0);
    sock_session_close(s);
  }
  Check(descriptor_count() == before);
}

internal void
delayed_peer_write(void *ptr)
{
  sleep_ms(20);
  peer_write(*(int *)ptr, str8_lit("wake"));
}

internal void
test_receive_deadlines(void)
{
  Temp scratch = scratch_begin(0, 0);
  SOCK_Session session = sock_session_open(0, 0);
  Check(session.u64[0] != 0);
  SOCK_Protocol protocol = SOCK_Protocol_UDP;
  SOCK_Endpoint from = {0};
  String8 data = str8_lit("unchanged");
  Check(!sock_recv(scratch.arena, session, &protocol, &from, &data, 0));
  U64 before = now_time_us();
  Check(!sock_recv(scratch.arena, session, &protocol, &from, &data, before+20000));
  Check(now_time_us()-before >= 10000 && now_time_us()-before < Million(3));
  Check(protocol == SOCK_Protocol_UDP && str8_match(data, str8_lit("unchanged"), 0));
  int peer = peer_connect(sock_session_listener_endpoint(session));
  Thread sender = thread_launch(delayed_peer_write, &peer);
  Check(sender.u64[0] != 0);
  Check(sock_recv(scratch.arena, session, &protocol, &from, &data, max_U64));
  Check(str8_match(data, str8_lit("wake"), 0));
  Check(thread_join(sender, max_U64));
  close(peer);
  sock_session_close(session);
  scratch_end(scratch);
}

global U64 hook_session;
global U32 hook_received;

internal SOCK_WAKEUP_FUNCTION_DEF(receive_in_wakeup)
{
  SOCK_Session s = {ins_atomic_u64_eval(&hook_session)};
  Temp scratch = scratch_begin(0, 0);
  SOCK_Protocol protocol;
  SOCK_Endpoint from;
  String8 data;
  if(sock_recv(scratch.arena, s, &protocol, &from, &data, 0) && str8_match(data, str8_lit("hook"), 0))
  {
    ins_atomic_u32_eval_assign(&hook_received, 1);
  }
  scratch_end(scratch);
}

internal void
test_reentrant_wakeup(void)
{
  ins_atomic_u32_eval_assign(&hook_received, 0);
  SOCK_Session session = sock_session_open(0, receive_in_wakeup);
  Check(session.u64[0] != 0);
  ins_atomic_u64_eval_assign(&hook_session, session.u64[0]);
  int peer = peer_connect(sock_session_listener_endpoint(session));
  peer_write(peer, str8_lit("hook"));
  U64 deadline = now_time_us()+Million(3);
  while(!ins_atomic_u32_eval(&hook_received) && now_time_us() < deadline) { sleep_ms(1); }
  Check(ins_atomic_u32_eval(&hook_received));
  sock_session_close(session);
  ins_atomic_u64_eval_assign(&hook_session, 0);
  close(peer);
}

internal void
test_rejected_send_is_atomic(void)
{
  Temp scratch = scratch_begin(0, 0);
  SOCK_Session session = sock_session_open(0, 0);
  Check(session.u64[0] != 0);
  int peer = peer_connect(sock_session_listener_endpoint(session));
  peer_write(peer, str8_lit("start"));
  SOCK_Protocol protocol;
  SOCK_Endpoint from;
  String8 data;
  Check(sock_recv(scratch.arena, session, &protocol, &from, &data, now_time_us()+Million(3)));
  Check(str8_match(data, str8_lit("start"), 0));
  String8 oversized = str8(push_array(scratch.arena, U8, MB(17)), MB(17));
  Check(!sock_send(session, protocol, from, oversized, 0));
  Check(!sock_send(session, SOCK_Protocol_UDP, from, str8_lit("wrong"), 0));
  SOCK_Endpoint ipv6 = from;
  ipv6.kind = SOCK_EndpointKind_IPv6;
  Check(!sock_send(session, protocol, ipv6, str8_lit("wrong"), 0));
  Check(sock_send(session, protocol, from, str8_lit("only"), 0));
  U8 reply[4];
  peer_read(peer, str8(reply, sizeof(reply)));
  Check(MemoryMatch(reply, "only", sizeof(reply)));
  close(peer);
  sock_session_close(session);
  scratch_end(scratch);
}

global U64 cases_run;

internal void
test_instances(void)
{
  Temp scratch = scratch_begin(0, 0);
  SOCK_Session a = sock_session_open(0, 0);
  SOCK_Session b = sock_session_open(0, 0);
  Check(a.u64[0] != 0 && b.u64[0] != 0);
  SOCK_Endpoint ep_a = sock_session_listener_endpoint(a);
  SOCK_Endpoint ep_b = sock_session_listener_endpoint(b);
  Check(ep_a.port != ep_b.port);
  int peer_a = peer_connect(ep_a);
  int peer_b = peer_connect(ep_b);
  peer_write(peer_b, str8_lit("bravo"));
  peer_write(peer_a, str8_lit("alpha"));
  SOCK_Protocol protocol;
  SOCK_Endpoint from;
  String8 data;
  Check(sock_recv(scratch.arena, a, &protocol, &from, &data, now_time_us()+Million(3)));
  Check(str8_match(data, str8_lit("alpha"), 0));
  Check(sock_send(a, protocol, from, str8_lit("A"), now_time_us()+Million(3)));
  Check(sock_recv(scratch.arena, b, &protocol, &from, &data, now_time_us()+Million(3)));
  Check(str8_match(data, str8_lit("bravo"), 0));
  Check(sock_send(b, protocol, from, str8_lit("B"), now_time_us()+Million(3)));
  U8 reply;
  peer_read(peer_a, str8(&reply, 1));
  Check(reply == 'A');
  peer_read(peer_b, str8(&reply, 1));
  Check(reply == 'B');
  close(peer_a);
  close(peer_b);
  sock_session_close(a);
  sock_session_close(b);
  scratch_end(scratch);
}

internal void
test_reset_and_refused(void)
{
  Temp scratch = scratch_begin(0, 0);
  SOCK_Session session = sock_session_open(0, 0);
  Check(session.u64[0] != 0);
  int peer = peer_connect(sock_session_listener_endpoint(session));
  peer_write(peer, str8_lit("before reset"));
  SOCK_Protocol protocol;
  SOCK_Endpoint from;
  String8 data;
  Check(sock_recv(scratch.arena, session, &protocol, &from, &data, now_time_us()+Million(3)));
  Check(str8_match(data, str8_lit("before reset"), 0));
  struct linger reset = {.l_onoff = 1, .l_linger = 0};
  Check(setsockopt(peer, SOL_SOCKET, SO_LINGER, &reset, sizeof(reset)) == 0);
  close(peer);
  Check(sock_recv(scratch.arena, session, &protocol, &from, &data, now_time_us()+Million(3)));
  Check(data.size == 0);
  Check(!sock_recv(scratch.arena, session, &protocol, &from, &data, now_time_us()+20000));
  peer = peer_connect(sock_session_listener_endpoint(session));
  peer_write(peer, str8_lit("after reset"));
  Check(sock_recv(scratch.arena, session, &protocol, &from, &data, now_time_us()+Million(3)));
  Check(str8_match(data, str8_lit("after reset"), 0));
  close(peer);
  sock_session_close(session);

  session = sock_session_open(0, 0);
  Check(session.u64[0] != 0);
  // A bound-but-unlistening TCP socket drops SYNs on Darwin. Close the bind,
  // then establish refusal independently before exercising the provider.
  int reserved = socket(AF_INET, SOCK_STREAM, 0);
  Check(reserved != -1);
  struct sockaddr_in addr = {.sin_family = AF_INET, .sin_addr.s_addr = htonl(INADDR_LOOPBACK)};
  Check(bind(reserved, (struct sockaddr *)&addr, sizeof(addr)) == 0);
  socklen_t size = sizeof(addr);
  Check(getsockname(reserved, (struct sockaddr *)&addr, &size) == 0);
  SOCK_Endpoint refused = sock_endpoint_from_string_port(str8_lit("127.0.0.1"), ntohs(addr.sin_port));
  close(reserved);
  int probe = socket(AF_INET, SOCK_STREAM, 0);
  Check(probe != -1 && fcntl(probe, F_SETFL, O_NONBLOCK) == 0);
  int code = connect(probe, (struct sockaddr *)&addr, sizeof(addr));
  if(code != 0 && errno == EINPROGRESS)
  {
    struct pollfd fd = {probe, POLLOUT, 0};
    Check(poll(&fd, 1, 1000) > 0);
    int error = 0;
    size = sizeof(error);
    Check(getsockopt(probe, SOL_SOCKET, SO_ERROR, &error, &size) == 0 && error == ECONNREFUSED);
  }
  else { Check(code == -1 && errno == ECONNREFUSED); }
  close(probe);
  Check(sock_send(session, SOCK_Protocol_TCP, refused, str8_lit("not delivered"), now_time_us()+Million(3)));
  Check(sock_recv(scratch.arena, session, &protocol, &from, &data, now_time_us()+Million(3)));
  Check(data.size == 0);
  sock_session_close(session);
  scratch_end(scratch);
}

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
  U64 before = descriptor_count();
  run_case(cmdline, "listener", test_listener);
  run_case(cmdline, "round_trip", test_round_trip);
  run_case(cmdline, "outgoing_and_eof", test_outgoing_and_eof);
  run_case(cmdline, "large_duplex", test_large_duplex);
  run_case(cmdline, "backpressure_close", test_backpressure_close);
  run_case(cmdline, "duplex_backpressure_disconnect", test_duplex_backpressure_disconnect);
  run_case(cmdline, "close_and_bind_failure", test_close_and_bind_failure);
  run_case(cmdline, "receive_deadlines", test_receive_deadlines);
  run_case(cmdline, "reentrant_wakeup", test_reentrant_wakeup);
  run_case(cmdline, "rejected_send_is_atomic", test_rejected_send_is_atomic);
  run_case(cmdline, "instances", test_instances);
  run_case(cmdline, "reset_and_refused", test_reset_and_refused);
  Check(cases_run != 0);
  Check(descriptor_count() == before);
}
