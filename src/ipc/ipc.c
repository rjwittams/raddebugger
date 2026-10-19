// Copyright (c) 2026 Robert Wittams
// Licensed under the MIT license (https://opensource.org/license/mit/)

#define IPC_HEADER_SIZE 40
#define IPC_PAYLOAD_LIMIT MB(64)
#define IPC_PEER_LIMIT 64

typedef struct IPC_Input IPC_Input;
struct IPC_Input
{
  SOCK_Peer peer;
  U8 header[IPC_HEADER_SIZE];
  U64 header_pos;
  U64 data_pos;
  U64 id;
  String8 data;
};

struct IPC_Server
{
  SOCK_Session socket;
  U128 instance_id;
  IPC_Input inputs[IPC_PEER_LIMIT];
  U64 buffered_bytes;
  Arena *receive_arena;
  SOCK_Peer pending_peer;
  String8 pending;
  U64 pending_pos;
};

internal U64
ipc_u64_from_wire(U8 *bytes)
{
  U64 result = 0;
  for(U64 idx = 0; idx < 8; idx += 1) { result |= (U64)bytes[idx] << (idx*8); }
  return result;
}

internal void
ipc_u64_to_wire(U8 *bytes, U64 value)
{
  for(U64 idx = 0; idx < 8; idx += 1) { bytes[idx] = (U8)(value >> (idx*8)); }
}

internal void
ipc_header(U8 *bytes, U8 kind, U64 id, U64 size, U128 instance_id)
{
  MemoryCopy(bytes, "RDIP", 4);
  bytes[4] = 1;
  bytes[5] = kind;
  bytes[6] = bytes[7] = 0;
  ipc_u64_to_wire(bytes+8, id);
  ipc_u64_to_wire(bytes+16, size);
  MemoryCopy(bytes+24, instance_id.u8, 16);
}

internal B32
ipc_header_valid(U8 *bytes, U8 kind, U128 instance_id)
{
  U8 difference = 0;
  for(U64 idx = 0; idx < 16; idx += 1) { difference |= bytes[24+idx] ^ instance_id.u8[idx]; }
  return difference == 0 && MemoryMatch(bytes, "RDIP", 4) &&
         bytes[4] == 1 && bytes[5] == kind && bytes[6] == 0 && bytes[7] == 0 &&
         ipc_u64_from_wire(bytes+8) != 0 && ipc_u64_from_wire(bytes+16) <= IPC_PAYLOAD_LIMIT;
}

internal void
ipc_discard_peer(IPC_Server *server, SOCK_Peer peer)
{
  for(U64 idx = 0; idx < ArrayCount(server->inputs); idx += 1)
  {
    IPC_Input *input = &server->inputs[idx];
    if(input->peer.id == peer.id)
    {
      server->buffered_bytes -= input->data.size;
      free(input->data.str);
      MemoryZeroStruct(input);
    }
  }
}

// Consume at most one request. Keep unread stream bytes, not a decoded queue.
// Return -1 for a rejected peer, 0 for partial input, or 1 for a complete frame.
internal S32
ipc_consume(Arena *arena, IPC_Server *server, IPC_Message *out)
{
  SOCK_Peer peer = server->pending_peer;
  String8 bytes = server->pending;
  U64 pos = server->pending_pos;
  IPC_Input *input = 0;
  for(U64 idx = 0; idx < ArrayCount(server->inputs); idx += 1)
  {
    if(server->inputs[idx].peer.id == peer.id) { input = &server->inputs[idx]; break; }
  }
  if(input == 0)
  {
    for(U64 idx = 0; idx < ArrayCount(server->inputs); idx += 1)
    {
      if(server->inputs[idx].peer.id == 0) { input = &server->inputs[idx]; input->peer = peer; break; }
    }
  }
  if(input == 0) { return -1; }
  if(input->header_pos < IPC_HEADER_SIZE)
  {
    U64 size = Min(IPC_HEADER_SIZE-input->header_pos, bytes.size-pos);
    MemoryCopy(input->header+input->header_pos, bytes.str+pos, size);
    input->header_pos += size;
    pos += size;
    server->pending_pos = pos;
    if(input->header_pos != IPC_HEADER_SIZE) { return 0; }
    if(!ipc_header_valid(input->header, 1, server->instance_id)) { return -1; }
    input->id = ipc_u64_from_wire(input->header+8);
    U64 payload_size = ipc_u64_from_wire(input->header+16);
    if(payload_size > IPC_PAYLOAD_LIMIT-server->buffered_bytes) { return -1; }
    if(payload_size != 0)
    {
      input->data.str = malloc(payload_size);
      if(input->data.str == 0) { return -1; }
    }
    input->data.size = payload_size;
    server->buffered_bytes += payload_size;
  }
  U64 size = Min(input->data.size-input->data_pos, bytes.size-pos);
  if(size != 0) { MemoryCopy(input->data.str+input->data_pos, bytes.str+pos, size); }
  input->data_pos += size;
  server->pending_pos = pos+size;
  if(input->data_pos != input->data.size) { return 0; }
  *out = (IPC_Message){peer, input->id, str8_copy(arena, input->data)};
  server->buffered_bytes -= input->data.size;
  free(input->data.str);
  input->data = str8_zero();
  input->header_pos = input->data_pos = input->id = 0;
  return 1;
}

internal IPC_Server *
ipc_server_open(U16 port, U128 instance_id, SOCK_WakeupFunctionType *wakeup)
{
  if((instance_id.u64[0] | instance_id.u64[1]) == 0) { return 0; }
  IPC_Server *server = calloc(1, sizeof(*server));
  if(server != 0)
  {
    server->instance_id = instance_id;
    server->socket = sock_session_open(port, wakeup);
    if(server->socket.u64[0] == 0) { free(server); server = 0; }
    else { server->receive_arena = arena_alloc(); }
  }
  return server;
}

internal IPC_Endpoint
ipc_server_endpoint(IPC_Server *server)
{
  IPC_Endpoint result = {0};
  if(server != 0)
  {
    result.address = sock_session_listener_endpoint(server->socket);
    result.instance_id = server->instance_id;
  }
  return result;
}

internal void
ipc_server_close(IPC_Server *server)
{
  if(server != 0)
  {
    sock_session_close(server->socket);
    for(U64 idx = 0; idx < ArrayCount(server->inputs); idx += 1) { free(server->inputs[idx].data.str); }
    arena_release(server->receive_arena);
    free(server);
  }
}

internal B32
ipc_server_receive(Arena *arena, IPC_Server *server, IPC_Message *out, U64 endt_us)
{
  if(server == 0) { return 0; }
  for(;;)
  {
    if(server->pending_pos == server->pending.size)
    {
      arena_clear(server->receive_arena);
      SOCK_Protocol protocol;
      if(!sock_recv(server->receive_arena, server->socket, &protocol, &server->pending_peer, &server->pending, endt_us)) { return 0; }
      server->pending_pos = 0;
      if(server->pending.size == 0)
      {
        ipc_discard_peer(server, server->pending_peer);
        if(endt_us != 0 && now_time_us() >= endt_us) { return 0; }
        continue;
      }
    }
    S32 code = ipc_consume(arena, server, out);
    if(code == 1) { return 1; }
    if(code < 0)
    {
      ipc_discard_peer(server, server->pending_peer);
      sock_peer_abort(server->socket, server->pending_peer, endt_us);
      server->pending_pos = server->pending.size;
    }
    if(endt_us != 0 && now_time_us() >= endt_us) { return 0; }
  }
}

internal B32
ipc_server_reply(IPC_Server *server, IPC_Message request, String8 data, U64 endt_us)
{
  if(server == 0 || request.peer.id == 0 || request.id == 0 || data.size > IPC_PAYLOAD_LIMIT) { return 0; }
  U8 header[IPC_HEADER_SIZE];
  ipc_header(header, 2, request.id, data.size, server->instance_id);
  B32 result = sock_send_peer(server->socket, SOCK_Protocol_TCP, request.peer, str8(header, sizeof(header)), endt_us);
  for(U64 pos = 0; result && pos < data.size;)
  {
    U64 size = Min(KB(64), data.size-pos);
    result = sock_send_peer(server->socket, SOCK_Protocol_TCP, request.peer, str8(data.str+pos, size), endt_us);
    pos += size;
  }
  if(!result) { sock_peer_abort(server->socket, request.peer, endt_us); }
  return result;
}

internal IPC_Response
ipc_request(Arena *arena, IPC_Endpoint endpoint, String8 data, U64 endt_us)
{
  IPC_Response response = {IPC_Status_Unavailable};
  if(data.size > IPC_PAYLOAD_LIMIT) { response.status = IPC_Status_TooLarge; return response; }
  if((endpoint.instance_id.u64[0] | endpoint.instance_id.u64[1]) == 0 ||
     endpoint.address.kind != SOCK_EndpointKind_IPv4 || endpoint.address.port == 0 ||
     endpoint.address.address_u8[0] != 127 || endpoint.address.address_u8[1] != 0 ||
     endpoint.address.address_u8[2] != 0 || endpoint.address.address_u8[3] != 1) { return response; }
  if(now_time_us() >= endt_us) { response.status = IPC_Status_Timeout; return response; }
  SOCK_Session socket = sock_session_open(0, 0);
  if(socket.u64[0] == 0) { return response; }
  U8 header[IPC_HEADER_SIZE];
  ipc_header(header, 1, 1, data.size, endpoint.instance_id);
  B32 sent = sock_send(socket, SOCK_Protocol_TCP, endpoint.address, str8(header, sizeof(header)), endt_us);
  for(U64 pos = 0; sent && pos < data.size;)
  {
    U64 size = Min(KB(64), data.size-pos);
    sent = now_time_us() < endt_us && sock_send(socket, SOCK_Protocol_TCP, endpoint.address, str8(data.str+pos, size), endt_us);
    pos += size;
  }
  String8 payload = {0};
  U64 header_pos = 0;
  U64 payload_pos = 0;
  SOCK_Peer destination = {0};
  if(!sent) { response.status = now_time_us() >= endt_us ? IPC_Status_Timeout : IPC_Status_Unavailable; }
  while(sent)
  {
    if(now_time_us() >= endt_us) { response.status = IPC_Status_Timeout; break; }
    Temp scratch = scratch_begin(&arena, 1);
    SOCK_Protocol protocol;
    SOCK_Peer peer;
    String8 bytes;
    B32 received = sock_recv(scratch.arena, socket, &protocol, &peer, &bytes, endt_us);
    if(!received)
    {
      response.status = now_time_us() >= endt_us ? IPC_Status_Timeout : IPC_Status_Disconnected;
      scratch_end(scratch);
      break;
    }
    if(peer.endpoint.port != endpoint.address.port ||
       !MemoryMatch(peer.endpoint.address_u8, endpoint.address.address_u8, 4))
    {
      sock_peer_abort(socket, peer, endt_us);
      scratch_end(scratch);
      continue;
    }
    if(bytes.size == 0)
    {
      response.status = IPC_Status_Disconnected;
      scratch_end(scratch);
      break;
    }
    if(destination.id == 0) { destination = peer; }
    if(peer.id != destination.id)
    {
      response.status = IPC_Status_InvalidReply;
      scratch_end(scratch);
      break;
    }
    U64 pos = 0;
    if(header_pos < sizeof(header))
    {
      U64 size = Min(sizeof(header)-header_pos, bytes.size);
      MemoryCopy(header+header_pos, bytes.str, size);
      header_pos += size;
      pos += size;
      if(header_pos == sizeof(header))
      {
        if(!ipc_header_valid(header, 2, endpoint.instance_id) || ipc_u64_from_wire(header+8) != 1)
        {
          response.status = IPC_Status_InvalidReply;
          scratch_end(scratch);
          break;
        }
        payload.size = ipc_u64_from_wire(header+16);
        if(payload.size != 0)
        {
          payload.str = malloc(payload.size);
          if(payload.str == 0)
          {
            response.status = IPC_Status_Unavailable;
            scratch_end(scratch);
            break;
          }
        }
      }
    }
    if(header_pos == sizeof(header))
    {
      U64 size = Min(payload.size-payload_pos, bytes.size-pos);
      if(size != 0) { MemoryCopy(payload.str+payload_pos, bytes.str+pos, size); }
      payload_pos += size;
      if(payload_pos == payload.size)
      {
        response.data = str8_copy(arena, payload);
        response.status = IPC_Status_Success;
        scratch_end(scratch);
        break;
      }
    }
    scratch_end(scratch);
    if(now_time_us() >= endt_us) { response.status = IPC_Status_Timeout; break; }
  }
  free(payload.str);
  sock_session_close(socket);
  return response;
}
