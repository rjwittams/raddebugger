// Copyright (c) 2026 Robert Wittams
// Licensed under the MIT license (https://opensource.org/license/mit/)

internal SOCK_OS_Socket sock_os_socket(void){ return WSASocketA(AF_INET, SOCK_STREAM, IPPROTO_TCP, 0, 0, WSA_FLAG_NO_HANDLE_INHERIT); }
internal void sock_os_close(SOCK_OS_Socket fd){ closesocket(fd); }
internal B32 sock_os_pending(void){ int e = WSAGetLastError(); return e == WSAEWOULDBLOCK || e == WSAEINPROGRESS; }
internal B32 sock_os_would_block(void){ return WSAGetLastError() == WSAEWOULDBLOCK; }
internal B32 sock_os_mutex_init(SOCK_OS_Mutex *mutex){ InitializeSRWLock(mutex); return 1; }
internal void sock_os_mutex_destroy(SOCK_OS_Mutex *mutex){}
internal void sock_os_mutex_take(SOCK_OS_Mutex *mutex){ AcquireSRWLockExclusive(mutex); }
internal void sock_os_mutex_drop(SOCK_OS_Mutex *mutex){ ReleaseSRWLockExclusive(mutex); }
internal B32 sock_os_cond_init(SOCK_OS_CondVar *cv){ InitializeConditionVariable(cv); return 1; }
internal void sock_os_cond_destroy(SOCK_OS_CondVar *cv){}
internal void sock_os_cond_broadcast(SOCK_OS_CondVar *cv){ WakeAllConditionVariable(cv); }

internal B32
sock_os_prepare(SOCK_OS_Socket fd)
{
  u_long nonblocking = 1;
  return ioctlsocket(fd, FIONBIO, &nonblocking) == 0 && SetHandleInformation((HANDLE)fd, HANDLE_FLAG_INHERIT, 0);
}

internal B32
sock_os_listener_options(SOCK_OS_Socket fd)
{
  BOOL exclusive = TRUE;
  return setsockopt(fd, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, (char *)&exclusive, sizeof(exclusive)) == 0;
}

internal B32
sock_os_pair(SOCK_OS_Socket pair[2])
{
  struct sockaddr_in addresses[2] = {0};
  for(U64 idx = 0; idx < 2; idx += 1)
  {
    pair[idx] = WSASocketA(AF_INET, SOCK_DGRAM, IPPROTO_UDP, 0, 0, WSA_FLAG_NO_HANDLE_INHERIT);
    if(pair[idx] == INVALID_SOCKET) { return 0; }
    addresses[idx].sin_family = AF_INET;
    addresses[idx].sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    int size = sizeof(addresses[idx]);
    if(!sock_os_listener_options(pair[idx]) ||
       bind(pair[idx], (struct sockaddr *)&addresses[idx], sizeof(addresses[idx])) != 0 ||
       getsockname(pair[idx], (struct sockaddr *)&addresses[idx], &size) != 0) { return 0; }
  }
  return connect(pair[0], (struct sockaddr *)&addresses[1], sizeof(addresses[1])) == 0 &&
         connect(pair[1], (struct sockaddr *)&addresses[0], sizeof(addresses[0])) == 0;
}

internal S64
sock_os_send(SOCK_OS_Socket fd, void *data, U64 size)
{
  int result;
  do { result = send(fd, (char *)data, (int)size, 0); }
  while(result == SOCKET_ERROR && WSAGetLastError() == WSAEINTR);
  return result;
}

internal S64
sock_os_recv(SOCK_OS_Socket fd, void *data, U64 size)
{
  int result;
  do { result = recv(fd, (char *)data, (int)size, 0); }
  while(result == SOCKET_ERROR && WSAGetLastError() == WSAEINTR);
  return result;
}

internal SOCK_OS_Socket
sock_os_accept(SOCK_OS_Socket fd, struct sockaddr *addr, SOCK_OS_SocketLen *size)
{
  SOCKET result;
  do { result = accept(fd, addr, size); }
  while(result == INVALID_SOCKET && WSAGetLastError() == WSAEINTR);
  return result;
}

internal int
sock_os_poll(SOCK_OS_PollFD *fds, U64 count)
{
  int result;
  do { result = WSAPoll(fds, (ULONG)count, -1); }
  while(result == SOCKET_ERROR && WSAGetLastError() == WSAEINTR);
  return result;
}

internal B32
sock_os_wait(SOCK_OS_CondVar *cv, SOCK_OS_Mutex *mutex, U64 endt_us)
{
  DWORD milliseconds = INFINITE;
  if(endt_us != max_U64)
  {
    U64 now_us = now_time_us();
    if(endt_us <= now_us) { return 0; }
    U64 left_us = endt_us-now_us;
    U64 left_ms = left_us/1000+(left_us%1000 != 0);
    milliseconds = (DWORD)Min(left_ms, (U64)INFINITE-1);
  }
  if(SleepConditionVariableSRW(cv, mutex, milliseconds, 0)) { return 1; }
  return GetLastError() == ERROR_TIMEOUT && endt_us != max_U64 && now_time_us() < endt_us;
}

#include "socket/socket_session.c"
