// Copyright (c) 2026 Robert Wittams
// Licensed under the MIT license (https://opensource.org/license/mit/)

#ifndef SOCKET_SESSION_H
#define SOCKET_SESSION_H

#define SOCK_IO_CONNECTION_LIMIT 64

typedef struct SOCK_IO_Packet SOCK_IO_Packet;
struct SOCK_IO_Packet
{
  SOCK_IO_Packet *next;
  SOCK_Peer peer;
  String8 data;
  U64 pos;
};

typedef struct SOCK_IO_Connection SOCK_IO_Connection;
struct SOCK_IO_Connection
{
  SOCK_IO_Connection *next;
  SOCK_Peer peer;
  SOCK_OS_Socket fd;
  B32 connecting;
  B32 outgoing;
  B32 ended;
  B32 read_hangup;
  SOCK_IO_Packet *first_send;
  SOCK_IO_Packet *last_send;
};

typedef struct SOCK_IO_Session SOCK_IO_Session;
struct SOCK_IO_Session
{
  SOCK_OS_Socket listener;
  SOCK_OS_Socket wakeup[2];
  Thread io_thread;
  SOCK_WakeupFunctionType *wakeup_hook;
  SOCK_OS_Mutex mutex;
  SOCK_OS_CondVar cv;
  B32 stopping;
  U64 send_bytes;
  U64 recv_bytes;
  SOCK_Peer aborts[SOCK_IO_CONNECTION_LIMIT];
  U64 abort_count;
  SOCK_IO_Packet *first_send;
  SOCK_IO_Packet *last_send;
  SOCK_IO_Packet *first_recv;
  SOCK_IO_Packet *last_recv;
};

// Queue memory is bounded; oversized sends fail rather than truncate.
read_only global U64 sock_io_queue_limit = MB(16);
global U64 sock_io_last_peer_id;

#endif // SOCKET_SESSION_H
