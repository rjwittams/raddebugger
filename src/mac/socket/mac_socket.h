// Copyright (c) 2026 Robert Wittams
// Licensed under the MIT license (https://opensource.org/license/mit/)

#ifndef MAC_SOCKET_H
#define MAC_SOCKET_H

#include <sys/socket.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>

typedef struct MAC_SOCK_Packet MAC_SOCK_Packet;
struct MAC_SOCK_Packet
{
  MAC_SOCK_Packet *next;
  SOCK_Endpoint endpoint;
  String8 data;
  U64 pos;
};

typedef struct MAC_SOCK_Connection MAC_SOCK_Connection;
struct MAC_SOCK_Connection
{
  MAC_SOCK_Connection *next;
  SOCK_Endpoint endpoint;
  int fd;
  B32 connecting;
  B32 read_hangup;
  MAC_SOCK_Packet *first_send;
  MAC_SOCK_Packet *last_send;
};

typedef struct MAC_SOCK_Session MAC_SOCK_Session;
struct MAC_SOCK_Session
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
  MAC_SOCK_Packet *first_send;
  MAC_SOCK_Packet *last_send;
  MAC_SOCK_Packet *first_recv;
  MAC_SOCK_Packet *last_recv;
};

// Queue memory is bounded; oversized sends fail rather than truncate.
read_only global U64 mac_sock_queue_limit = MB(16);
read_only global U64 mac_sock_connection_limit = 64;

#endif // MAC_SOCKET_H
