// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

internal void sock_init(void){}
internal void sock_async_tick(void){}
internal SOCK_Session sock_session_open(U16 listener_port, SOCK_WakeupFunctionType *wakeup_hook){SOCK_Session s = {0}; return s;}
internal void sock_session_close(SOCK_Session session){}
internal SOCK_Endpoint sock_session_listener_endpoint(SOCK_Session session){SOCK_Endpoint ep = {0}; return ep;}
internal B32 sock_send(SOCK_Session session, SOCK_Protocol protocol, SOCK_Endpoint endpoint, String8 data, U64 endt_us){return 0;}
internal B32 sock_send_peer(SOCK_Session session, SOCK_Protocol protocol, SOCK_Peer peer, String8 data, U64 endt_us){return 0;}
internal B32 sock_peer_abort(SOCK_Session session, SOCK_Peer peer, U64 endt_us){return 0;}
internal B32 sock_recv(Arena *arena, SOCK_Session session, SOCK_Protocol *protocol_out, SOCK_Peer *peer_out, String8 *data_out, U64 endt_us){return 0;}
