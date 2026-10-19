// Copyright (c) 2026 Robert Wittams
// Licensed under the MIT license (https://opensource.org/license/mit/)

#ifndef POSIX_SOCKET_H
#define POSIX_SOCKET_H

#include <sys/socket.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>

typedef int SOCK_OS_Socket;
typedef socklen_t SOCK_OS_SocketLen;
typedef struct pollfd SOCK_OS_PollFD;
typedef pthread_mutex_t SOCK_OS_Mutex;
typedef pthread_cond_t SOCK_OS_CondVar;
#define SOCK_OS_SocketInvalid -1

#if OS_MAC
# define SOCK_OS_RETRY(expr) MAC_RETRY_ON_EINTR(expr)
# define SOCK_OS_SEND_FLAGS 0
#else
# define SOCK_OS_RETRY(expr) LNX_RETRY_ON_EINTR(expr)
# define SOCK_OS_SEND_FLAGS MSG_NOSIGNAL
#endif

#include "socket/socket_session.h"

#endif // POSIX_SOCKET_H
