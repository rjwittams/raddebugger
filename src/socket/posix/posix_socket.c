// Copyright (c) 2026 Robert Wittams
// Licensed under the MIT license (https://opensource.org/license/mit/)

internal SOCK_OS_Socket sock_os_socket(void){ return socket(AF_INET, SOCK_STREAM, 0); }
internal void sock_os_close(SOCK_OS_Socket fd){ close(fd); }
internal B32 sock_os_pending(void){ return errno == EINPROGRESS; }
internal B32 sock_os_would_block(void){ return errno == EAGAIN || errno == EWOULDBLOCK; }
internal S64 sock_os_send(SOCK_OS_Socket fd, void *data, U64 size){ return SOCK_OS_RETRY(send(fd, data, size, SOCK_OS_SEND_FLAGS)); }
internal S64 sock_os_recv(SOCK_OS_Socket fd, void *data, U64 size){ return SOCK_OS_RETRY(recv(fd, data, size, 0)); }
internal SOCK_OS_Socket sock_os_accept(SOCK_OS_Socket fd, struct sockaddr *addr, SOCK_OS_SocketLen *size){ return SOCK_OS_RETRY(accept(fd, addr, size)); }
internal int sock_os_poll(SOCK_OS_PollFD *fds, U64 count){ return SOCK_OS_RETRY(poll(fds, (nfds_t)count, -1)); }
internal B32 sock_os_pair(SOCK_OS_Socket pair[2]){ return socketpair(AF_UNIX, SOCK_DGRAM, 0, pair) == 0; }
internal B32 sock_os_mutex_init(SOCK_OS_Mutex *mutex){ return pthread_mutex_init(mutex, 0) == 0; }
internal void sock_os_mutex_destroy(SOCK_OS_Mutex *mutex){ pthread_mutex_destroy(mutex); }
internal void sock_os_mutex_take(SOCK_OS_Mutex *mutex){ pthread_mutex_lock(mutex); }
internal void sock_os_mutex_drop(SOCK_OS_Mutex *mutex){ pthread_mutex_unlock(mutex); }
internal void sock_os_cond_destroy(SOCK_OS_CondVar *cv){ pthread_cond_destroy(cv); }
internal void sock_os_cond_broadcast(SOCK_OS_CondVar *cv){ pthread_cond_broadcast(cv); }

internal B32
sock_os_prepare(SOCK_OS_Socket fd)
{
  int flags = fcntl(fd, F_GETFL);
  B32 result = flags != -1 && fcntl(fd, F_SETFL, flags|O_NONBLOCK) == 0 && fcntl(fd, F_SETFD, FD_CLOEXEC) == 0;
#if OS_MAC
  int no_sigpipe = 1;
  result = result && setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &no_sigpipe, sizeof(no_sigpipe)) == 0;
#endif
  return result;
}

internal B32
sock_os_listener_options(SOCK_OS_Socket fd)
{
  int reuse = 1;
  return setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) == 0;
}

internal B32
sock_os_cond_init(SOCK_OS_CondVar *cv)
{
#if OS_MAC
  return pthread_cond_init(cv, 0) == 0;
#else
  pthread_condattr_t attr;
  if(pthread_condattr_init(&attr) != 0) { return 0; }
  B32 result = pthread_condattr_setclock(&attr, CLOCK_MONOTONIC) == 0 && pthread_cond_init(cv, &attr) == 0;
  pthread_condattr_destroy(&attr);
  return result;
#endif
}

internal B32
sock_os_wait(SOCK_OS_CondVar *cv, SOCK_OS_Mutex *mutex, U64 endt_us)
{
  if(endt_us == max_U64) { return pthread_cond_wait(cv, mutex) == 0; }
  U64 now_us = now_time_us();
  if(endt_us <= now_us) { return 0; }
#if OS_MAC
  U64 left_us = endt_us-now_us;
  struct timespec timeout = {(time_t)(left_us/Million(1)), (long)((left_us%Million(1))*Thousand(1))};
  return pthread_cond_timedwait_relative_np(cv, mutex, &timeout) == 0;
#else
  struct timespec timeout = {(time_t)(endt_us/Million(1)), (long)((endt_us%Million(1))*Thousand(1))};
  return pthread_cond_timedwait(cv, mutex, &timeout) == 0;
#endif
}

#include "socket/socket_session.c"
