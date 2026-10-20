// Copyright (c) 2026 Robert Wittams
// Licensed under the MIT license (https://opensource.org/license/mit/)

#ifndef IPC_H
#define IPC_H

typedef struct IPC_Endpoint IPC_Endpoint;
struct IPC_Endpoint
{
  SOCK_Endpoint address;
  U128 instance_id;
  U256 auth_key;
};

typedef struct IPC_Message IPC_Message;
struct IPC_Message
{
  SOCK_Peer peer;
  U64 id;
  String8 data;
};

typedef struct IPC_Server IPC_Server;

// The default registry is per-user and machine-local; this creates no registration.
internal String8 ipc_default_directory(Arena *arena);

typedef enum IPC_Status
{
  IPC_Status_None,
  IPC_Status_Success,
  IPC_Status_Unavailable,
  IPC_Status_Timeout,
  IPC_Status_Disconnected,
  IPC_Status_InvalidReply,
  IPC_Status_TooLarge,
} IPC_Status;

typedef struct IPC_Response IPC_Response;
struct IPC_Response
{
  IPC_Status status;
  String8 data;
};

// Generate a fresh identity/key with native entropy; fail closed on failure.
internal IPC_Server *ipc_server_open_random(U16 port, SOCK_WakeupFunctionType *wakeup);
// Publish a fresh server for this PID in a private directory; close unpublishes it.
internal IPC_Server *ipc_server_open_local(String8 directory, SOCK_WakeupFunctionType *wakeup);
// Explicit configuration requires a nonzero instance id and secret key.
// Private endpoint publication precedes app activation.
// The owner serializes calls and closes only after those calls have finished.
internal IPC_Server *ipc_server_open(U16 port, U128 instance_id, U256 auth_key, SOCK_WakeupFunctionType *wakeup);
internal IPC_Endpoint ipc_server_endpoint(IPC_Server *server);
internal void ipc_server_close(IPC_Server *server);
// A false receive leaves outputs unchanged; partial frames are never commands.
internal B32 ipc_server_receive(Arena *arena, IPC_Server *server, IPC_Message *out, U64 endt_us);
internal B32 ipc_server_reply(IPC_Server *server, IPC_Message request, String8 data, U64 endt_us);
// One connection/transaction per call, with one deadline shared by send/receive.
// Errors return no partial reply; successful bytes belong to the caller arena.
internal IPC_Response ipc_request(Arena *arena, IPC_Endpoint endpoint, String8 data, U64 endt_us);
// Discover an exact live PID through private local registration, without fallback.
internal IPC_Response ipc_request_local(Arena *arena, String8 directory, U32 pid, String8 data, U64 endt_us);

#endif // IPC_H
