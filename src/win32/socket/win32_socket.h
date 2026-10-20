// Copyright (c) 2026 Robert Wittams
// Licensed under the MIT license (https://opensource.org/license/mit/)

#ifndef WIN32_SOCKET_H
#define WIN32_SOCKET_H

#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32")

typedef SOCKET SOCK_OS_Socket;
typedef int SOCK_OS_SocketLen;
typedef WSAPOLLFD SOCK_OS_PollFD;
typedef SRWLOCK SOCK_OS_Mutex;
typedef CONDITION_VARIABLE SOCK_OS_CondVar;
#define SOCK_OS_SocketInvalid INVALID_SOCKET

#include "socket/socket_session.h"

#endif // WIN32_SOCKET_H
