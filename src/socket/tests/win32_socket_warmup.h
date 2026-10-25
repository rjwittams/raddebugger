// Copyright (c) 2026 Robert Wittams
// Licensed under the MIT license (https://opensource.org/license/mit/)

#ifndef SOCKET_TEST_WIN32_WARMUP_H
#define SOCKET_TEST_WIN32_WARMUP_H

#if OS_WINDOWS
typedef struct NativeConnectWarmup NativeConnectWarmup;
struct NativeConnectWarmup { SOCKET socket; struct sockaddr_in address; };

internal DWORD WINAPI
native_connect_warmup(void *ptr)
{
  NativeConnectWarmup *c = (NativeConnectWarmup *)ptr;
  int code = connect(c->socket, (struct sockaddr *)&c->address, sizeof(c->address));
  Check(code == 0 || WSAGetLastError() == WSAEWOULDBLOCK);
  WSAPOLLFD descriptor = {c->socket, POLLOUT, 0};
  Check(WSAPoll(&descriptor, 1, 3000) == 1);
  int error = 0, size = sizeof(error);
  Check(getsockopt(c->socket, SOL_SOCKET, SO_ERROR, (char *)&error, &size) == 0 && error == 0);
  return 0;
}

// Native controls retain process-wide handles on first nonblocking connect.
// Prime that runtime before counting; no RAD session participates in warm-up.
internal void
warm_native_socket_runtime(void)
{
  SOCKET listener = WSASocketA(AF_INET, SOCK_STREAM, IPPROTO_TCP, 0, 0, WSA_FLAG_NO_HANDLE_INHERIT);
  SOCKET client = WSASocketA(AF_INET, SOCK_STREAM, IPPROTO_TCP, 0, 0, WSA_FLAG_NO_HANDLE_INHERIT);
  Check(listener != INVALID_SOCKET && client != INVALID_SOCKET);
  NativeConnectWarmup c = {0};
  c.socket = client;
  c.address.sin_family = AF_INET;
  c.address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  Check(bind(listener, (struct sockaddr *)&c.address, sizeof(c.address)) == 0 && listen(listener, 4) == 0);
  int size = sizeof(c.address);
  Check(getsockname(listener, (struct sockaddr *)&c.address, &size) == 0);
  WSAPOLLFD descriptor = {listener, POLLIN, 0};
  Check(WSAPoll(&descriptor, 1, 0) == 0);
  u_long nonblocking = 1;
  Check(ioctlsocket(client, FIONBIO, &nonblocking) == 0);
  HANDLE thread = CreateThread(0, 0, native_connect_warmup, &c, 0, 0);
  Check(thread != 0 && WaitForSingleObject(thread, 4000) == WAIT_OBJECT_0 && CloseHandle(thread));
  SOCKET accepted = accept(listener, 0, 0);
  Check(accepted != INVALID_SOCKET);
  Check(closesocket(accepted) == 0 && closesocket(client) == 0 && closesocket(listener) == 0);
}
#endif

#endif // SOCKET_TEST_WIN32_WARMUP_H
