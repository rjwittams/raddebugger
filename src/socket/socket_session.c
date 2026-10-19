// Copyright (c) 2026 Robert Wittams
// Licensed under the MIT license (https://opensource.org/license/mit/)

////////////////////////////////
//~ Socket Session Queues

internal void
sock_io_wake(SOCK_IO_Session *s)
{
  U8 byte = 0;
  sock_os_send(s->wakeup[1], &byte, sizeof(byte));
}

internal SOCK_IO_Packet *
sock_io_packet_alloc(SOCK_Endpoint endpoint, String8 data)
{
  SOCK_IO_Packet *p = malloc(sizeof(*p)+data.size);
  if(p != 0)
  {
    *p = (SOCK_IO_Packet){0};
    p->endpoint = endpoint;
    p->data = str8((U8 *)(p+1), data.size);
    if(data.size != 0) { MemoryCopy(p->data.str, data.str, data.size); }
  }
  return p;
}

internal void
sock_io_send_release(SOCK_IO_Session *s, SOCK_IO_Packet *p)
{
  sock_os_mutex_take(&s->mutex);
  s->send_bytes -= sizeof(*p)+p->data.size;
  sock_os_cond_broadcast(&s->cv);
  sock_os_mutex_drop(&s->mutex);
  free(p);
}

internal U64
sock_io_recv_space(SOCK_IO_Session *s)
{
  sock_os_mutex_take(&s->mutex);
  U64 result = sock_io_queue_limit-s->recv_bytes;
  sock_os_mutex_drop(&s->mutex);
  return result;
}

internal B32
sock_io_receive(SOCK_IO_Session *s, SOCK_Endpoint endpoint, String8 data)
{
  SOCK_IO_Packet *p = sock_io_packet_alloc(endpoint, data);
  sock_os_mutex_take(&s->mutex);
  B32 result = p != 0 && sizeof(*p)+data.size <= sock_io_queue_limit-s->recv_bytes;
  if(result)
  {
    SLLQueuePush(s->first_recv, s->last_recv, p);
    s->recv_bytes += sizeof(*p)+data.size;
  }
  else
  {
    s->stopping = 1;
    free(p);
  }
  sock_os_cond_broadcast(&s->cv);
  sock_os_mutex_drop(&s->mutex);
  if(result && s->wakeup_hook != 0) { s->wakeup_hook(); }
  return result;
}

// Connections belong only to the I/O thread; callers exchange owned packets.
internal void
sock_io_connection_close(SOCK_IO_Session *s, SOCK_IO_Connection **first, SOCK_IO_Connection *c)
{
  SOCK_IO_Connection **link = first;
  while(*link != c) { link = &(*link)->next; }
  *link = c->next;
  sock_os_close(c->fd);
  for(SOCK_IO_Packet *p = c->first_send, *next = 0; p != 0; p = next)
  {
    next = p->next;
    sock_io_send_release(s, p);
  }
  free(c);
}

////////////////////////////////
//~ Session I/O Thread

internal void
sock_io_thread(void *ptr)
{
  SOCK_IO_Session *s = (SOCK_IO_Session *)ptr;
  SOCK_IO_Connection *first = 0;
  U64 connection_count = 0;
  ThreadNameF("socket_io");
  for(;;)
  {
    Temp scratch = scratch_begin(0, 0);
    sock_os_mutex_take(&s->mutex);
    B32 stopping = s->stopping;
    SOCK_IO_Packet *sends = s->first_send;
    s->first_send = s->last_send = 0;
    sock_os_mutex_drop(&s->mutex);

    for(SOCK_IO_Packet *p = sends, *next = 0; p != 0; p = next)
    {
      next = p->next;
      p->next = 0;
      SOCK_IO_Connection *c = first;
      while(c != 0 && !MemoryMatchStruct(&c->endpoint, &p->endpoint)) { c = c->next; }
      if(!stopping && c == 0 && connection_count < sock_io_connection_limit)
      {
        SOCK_OS_Socket fd = sock_os_socket();
        if(fd != SOCK_OS_SocketInvalid)
        {
          struct sockaddr_in addr = {0};
          addr.sin_family = AF_INET;
          addr.sin_port = htons(p->endpoint.port);
          MemoryCopy(&addr.sin_addr, p->endpoint.address_u8, sizeof(addr.sin_addr));
          if(sock_os_prepare(fd))
          {
            int code = connect(fd, (struct sockaddr *)&addr, sizeof(addr));
            if(code == 0 || sock_os_pending())
            {
              c = calloc(1, sizeof(*c));
              if(c != 0)
              {
                c->fd = fd;
                c->endpoint = p->endpoint;
                c->connecting = code != 0;
                SLLStackPush(first, c);
                connection_count += 1;
              }
            }
          }
          if(c == 0) { sock_os_close(fd); }
        }
      }
      if(!stopping && c != 0) { SLLQueuePush(c->first_send, c->last_send, p); }
      else
      {
        if(!stopping) { sock_io_receive(s, p->endpoint, str8_zero()); }
        sock_io_send_release(s, p);
      }
    }
    if(stopping) { scratch_end(scratch); break; }

    SOCK_OS_PollFD *fds = push_array(scratch.arena, SOCK_OS_PollFD, connection_count+2);
    SOCK_IO_Connection **connections = push_array(scratch.arena, SOCK_IO_Connection *, connection_count+2);
    U64 count = 1;
    U64 space = sock_io_recv_space(s);
    fds[0] = (SOCK_OS_PollFD){s->wakeup[0], POLLIN, 0};
    if(connection_count < sock_io_connection_limit && space > sizeof(SOCK_IO_Packet))
    {
      fds[count++] = (SOCK_OS_PollFD){s->listener, POLLIN, 0};
    }
    for(SOCK_IO_Connection *c = first; c != 0; c = c->next)
    {
      short events = 0;
      if(!c->connecting && space > sizeof(SOCK_IO_Packet)) { events |= POLLIN; }
      if(c->connecting || c->first_send != 0) { events |= POLLOUT; }
      if(c->read_hangup && space <= sizeof(SOCK_IO_Packet)) { events = 0; }
      // Omit backpressured readers, including their unmaskable HUP events.
      if(events != 0)
      {
        fds[count] = (SOCK_OS_PollFD){c->fd, events, 0};
        connections[count] = c;
        count += 1;
      }
    }
    int ready = sock_os_poll(fds, count);
    if(ready < 0)
    {
      sock_os_mutex_take(&s->mutex);
      s->stopping = 1;
      sock_os_cond_broadcast(&s->cv);
      sock_os_mutex_drop(&s->mutex);
      scratch_end(scratch);
      break;
    }
    if(fds[0].revents != 0)
    {
      U8 bytes[128];
      while(sock_os_recv(s->wakeup[0], bytes, sizeof(bytes)) > 0) {}
    }
    for(U64 idx = 1; idx < count; idx += 1)
    {
      SOCK_IO_Connection *c = connections[idx];
      short events = fds[idx].revents;
      if(events == 0) { continue; }
      if(c == 0)
      {
        if(events & POLLIN)
        {
          struct sockaddr_in addr = {0};
          SOCK_OS_SocketLen size = sizeof(addr);
          SOCK_OS_Socket fd = sock_os_accept(s->listener, (struct sockaddr *)&addr, &size);
          if(fd != SOCK_OS_SocketInvalid)
          {
            SOCK_IO_Connection *accepted = 0;
            if(sock_os_prepare(fd)) { accepted = calloc(1, sizeof(*accepted)); }
            if(accepted != 0)
            {
              accepted->fd = fd;
              accepted->endpoint.port = ntohs(addr.sin_port);
              MemoryCopy(accepted->endpoint.address_u8, &addr.sin_addr, sizeof(addr.sin_addr));
              SLLStackPush(first, accepted);
              connection_count += 1;
            }
            else { sock_os_close(fd); }
          }
        }
        continue;
      }
      if(events & (POLLHUP|POLLERR)) { c->read_hangup = 1; }
      B32 disconnected = (events & POLLNVAL) != 0;
      if(c->connecting && events & (POLLOUT|POLLERR|POLLHUP))
      {
        int error = 0;
        SOCK_OS_SocketLen size = sizeof(error);
        disconnected = getsockopt(c->fd, SOL_SOCKET, SO_ERROR, (char *)&error, &size) != 0 || error != 0;
        c->connecting = 0;
      }
      // Drain data before acting on HUP; recv(0) ends this endpoint once.
      space = sock_io_recv_space(s);
      if(!disconnected && !c->connecting && events & (POLLIN|POLLHUP|POLLERR) && space > sizeof(SOCK_IO_Packet))
      {
        U8 bytes[4096];
        U64 cap = Min(sizeof(bytes), space-sizeof(SOCK_IO_Packet));
        S64 n = sock_os_recv(c->fd, bytes, cap);
        if(n > 0) { sock_io_receive(s, c->endpoint, str8(bytes, (U64)n)); }
        else if(n == 0 || !sock_os_would_block()) { disconnected = 1; }
      }
      if(!disconnected && !c->connecting && events & POLLOUT && c->first_send != 0)
      {
        SOCK_IO_Packet *p = c->first_send;
        S64 n = sock_os_send(c->fd, p->data.str+p->pos, p->data.size-p->pos);
        if(n > 0)
        {
          p->pos += (U64)n;
          if(p->pos == p->data.size)
          {
            SLLQueuePop(c->first_send, c->last_send);
            sock_io_send_release(s, p);
          }
        }
        else if(n == 0 || !sock_os_would_block()) { disconnected = 1; }
      }
      if(disconnected)
      {
        sock_io_receive(s, c->endpoint, str8_zero());
        sock_io_connection_close(s, &first, c);
        connection_count -= 1;
      }
    }
    scratch_end(scratch);
  }
  while(first != 0) { sock_io_connection_close(s, &first, first); }
}

////////////////////////////////
//~ Session Interface

// Progress is per-session rather than tied to the application's async lanes.
internal void sock_init(void){}
internal void sock_async_tick(void){}

internal SOCK_Session
sock_session_open(U16 listener_port, SOCK_WakeupFunctionType *wakeup_hook)
{
  SOCK_Session result = {0};
  SOCK_OS_Socket fd = sock_os_socket();
  if(fd != SOCK_OS_SocketInvalid)
  {
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(listener_port);
    if(sock_os_prepare(fd) && sock_os_listener_options(fd) &&
       bind(fd, (struct sockaddr *)&addr, sizeof(addr)) == 0 && listen(fd, SOMAXCONN) == 0)
    {
      SOCK_IO_Session *s = calloc(1, sizeof(*s));
      if(s != 0)
      {
        s->listener = fd;
        s->wakeup_hook = wakeup_hook;
        s->wakeup[0] = s->wakeup[1] = SOCK_OS_SocketInvalid;
        if(sock_os_pair(s->wakeup) &&
           sock_os_prepare(s->wakeup[0]) && sock_os_prepare(s->wakeup[1]))
        {
          if(sock_os_mutex_init(&s->mutex))
          {
            if(sock_os_cond_init(&s->cv))
            {
              s->io_thread = thread_launch(sock_io_thread, s);
              if(s->io_thread.u64[0] != 0) { result.u64[0] = (U64)s; }
              else { sock_os_cond_destroy(&s->cv); }
            }
            if(result.u64[0] == 0) { sock_os_mutex_destroy(&s->mutex); }
          }
        }
        if(result.u64[0] == 0)
        {
          if(s->wakeup[0] != SOCK_OS_SocketInvalid) { sock_os_close(s->wakeup[0]); }
          if(s->wakeup[1] != SOCK_OS_SocketInvalid) { sock_os_close(s->wakeup[1]); }
          free(s);
        }
      }
    }
    if(result.u64[0] == 0) { sock_os_close(fd); }
  }
  return result;
}

internal void
sock_session_close(SOCK_Session session)
{
  SOCK_IO_Session *s = (SOCK_IO_Session *)session.u64[0];
  if(s != 0)
  {
    sock_os_mutex_take(&s->mutex);
    s->stopping = 1;
    sock_os_cond_broadcast(&s->cv);
    sock_os_mutex_drop(&s->mutex);
    sock_io_wake(s);
    thread_join(s->io_thread, max_U64);
    sock_os_close(s->listener);
    sock_os_close(s->wakeup[0]);
    sock_os_close(s->wakeup[1]);
    for(SOCK_IO_Packet *p = s->first_send, *next = 0; p != 0; p = next) { next = p->next; free(p); }
    for(SOCK_IO_Packet *p = s->first_recv, *next = 0; p != 0; p = next) { next = p->next; free(p); }
    sock_os_cond_destroy(&s->cv);
    sock_os_mutex_destroy(&s->mutex);
    free(s);
  }
}

internal SOCK_Endpoint
sock_session_listener_endpoint(SOCK_Session session)
{
  SOCK_Endpoint result = {0};
  SOCK_IO_Session *s = (SOCK_IO_Session *)session.u64[0];
  if(s != 0)
  {
    struct sockaddr_in addr = {0};
    SOCK_OS_SocketLen size = sizeof(addr);
    if(getsockname(s->listener, (struct sockaddr *)&addr, &size) == 0)
    {
      result.port = ntohs(addr.sin_port);
      MemoryCopy(result.address_u8, &addr.sin_addr, sizeof(addr.sin_addr));
    }
  }
  return result;
}

// Success means the whole buffer was queued, not acknowledged by the peer.
internal B32
sock_send(SOCK_Session session, SOCK_Protocol protocol, SOCK_Endpoint endpoint, String8 data, U64 endt_us)
{
  SOCK_IO_Session *s = (SOCK_IO_Session *)session.u64[0];
  if(s == 0 || protocol != SOCK_Protocol_TCP || endpoint.kind != SOCK_EndpointKind_IPv4 || endpoint.port == 0 ||
     data.size > sock_io_queue_limit-sizeof(SOCK_IO_Packet)) { return 0; }
  if(data.size == 0) { return 1; }
  U64 size = sizeof(SOCK_IO_Packet)+data.size;
  B32 result = 0;
  sock_os_mutex_take(&s->mutex);
  while(!s->stopping && size > sock_io_queue_limit-s->send_bytes)
  {
    if(!sock_os_wait(&s->cv, &s->mutex, endt_us)) { break; }
  }
  if(!s->stopping && size <= sock_io_queue_limit-s->send_bytes)
  {
    SOCK_IO_Packet *p = sock_io_packet_alloc(endpoint, data);
    if(p != 0)
    {
      SLLQueuePush(s->first_send, s->last_send, p);
      s->send_bytes += size;
      result = 1;
    }
  }
  sock_os_mutex_drop(&s->mutex);
  if(result) { sock_io_wake(s); }
  return result;
}

// Each result is a stream chunk. A zero-length result is endpoint EOF.
internal B32
sock_recv(Arena *arena, SOCK_Session session, SOCK_Protocol *protocol_out, SOCK_Endpoint *endpoint_out, String8 *data_out, U64 endt_us)
{
  SOCK_IO_Session *s = (SOCK_IO_Session *)session.u64[0];
  if(s == 0) { return 0; }
  sock_os_mutex_take(&s->mutex);
  while(!s->stopping && s->first_recv == 0)
  {
    if(!sock_os_wait(&s->cv, &s->mutex, endt_us)) { break; }
  }
  SOCK_IO_Packet *p = s->first_recv;
  if(p != 0)
  {
    SLLQueuePop(s->first_recv, s->last_recv);
    s->recv_bytes -= sizeof(*p)+p->data.size;
  }
  sock_os_mutex_drop(&s->mutex);
  if(p == 0) { return 0; }
  *protocol_out = SOCK_Protocol_TCP;
  *endpoint_out = p->endpoint;
  *data_out = str8_copy(arena, p->data);
  free(p);
  sock_io_wake(s);
  return 1;
}
