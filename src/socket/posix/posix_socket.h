// Copyright (c) 2026 Robert Wittams
// Licensed under the MIT license (https://opensource.org/license/mit/)

#ifndef POSIX_SOCKET_H
#define POSIX_SOCKET_H

#include <sys/socket.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>

#if OS_MAC
# define POSIX_SOCK_RETRY_ON_EINTR(expr) MAC_RETRY_ON_EINTR(expr)
# define POSIX_SOCK_SEND_FLAGS 0
#else
# define POSIX_SOCK_RETRY_ON_EINTR(expr) LNX_RETRY_ON_EINTR(expr)
# define POSIX_SOCK_SEND_FLAGS MSG_NOSIGNAL
#endif

typedef struct POSIX_SOCK_Packet POSIX_SOCK_Packet;
struct POSIX_SOCK_Packet
{
  POSIX_SOCK_Packet *next;
  SOCK_Endpoint endpoint;
  String8 data;
  U64 pos;
};

typedef struct POSIX_SOCK_Connection POSIX_SOCK_Connection;
struct POSIX_SOCK_Connection
{
  POSIX_SOCK_Connection *next;
  SOCK_Endpoint endpoint;
  int fd;
  B32 connecting;
  B32 read_hangup;
  POSIX_SOCK_Packet *first_send;
  POSIX_SOCK_Packet *last_send;
};

typedef struct POSIX_SOCK_Session POSIX_SOCK_Session;
struct POSIX_SOCK_Session
{
  int listener;
  int wakeup[2];
  Thread io_thread;
  SOCK_WakeupFunctionType *wakeup_hook;
  pthread_mutex_t mutex;
  pthread_cond_t cv;
  B32 stopping;
  U64 send_bytes;
  U64 recv_bytes;
  POSIX_SOCK_Packet *first_send;
  POSIX_SOCK_Packet *last_send;
  POSIX_SOCK_Packet *first_recv;
  POSIX_SOCK_Packet *last_recv;
};

// Queue memory is bounded; oversized sends fail rather than truncate.
read_only global U64 posix_sock_queue_limit = MB(16);
read_only global U64 posix_sock_connection_limit = 64;

#endif // POSIX_SOCKET_H
