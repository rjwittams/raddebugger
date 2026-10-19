// Copyright (c) 2026 Robert Wittams
// Licensed under the MIT license (https://opensource.org/license/mit/)

////////////////////////////////
//~ POSIX Socket and Queue Helpers

internal B32
posix_sock_prepare(int fd)
{
  int flags = fcntl(fd, F_GETFL);
  B32 result = (flags != -1 && fcntl(fd, F_SETFL, flags|O_NONBLOCK) == 0 &&
                fcntl(fd, F_SETFD, FD_CLOEXEC) == 0);
#if OS_MAC
  int no_sigpipe = 1;
  result = result && setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &no_sigpipe, sizeof(no_sigpipe)) == 0;
#endif
  return result;
}

internal void
posix_sock_wake(POSIX_SOCK_Session *s)
{
  U8 byte = 0;
  POSIX_SOCK_RETRY_ON_EINTR(send(s->wakeup[1], &byte, sizeof(byte), POSIX_SOCK_SEND_FLAGS));
}

internal B32
posix_sock_cond_init(pthread_cond_t *cv)
{
#if OS_MAC
  return pthread_cond_init(cv, 0) == 0;
#else
  pthread_condattr_t attr;
  if(pthread_condattr_init(&attr) != 0) { return 0; }
  B32 result = pthread_condattr_setclock(&attr, CLOCK_MONOTONIC) == 0 && pthread_cond_init(cv, &attr) == 0;
  pthread_condattr_destroy(&attr);
  return result;
#endif
}

// Called with the queue mutex held. An infinite deadline uses the untimed wait.
internal B32
posix_sock_wait(POSIX_SOCK_Session *s, U64 endt_us)
{
  if(endt_us == max_U64) { return pthread_cond_wait(&s->cv, &s->mutex) == 0; }
  U64 now_us = now_time_us();
  if(endt_us <= now_us) { return 0; }
#if OS_MAC
  U64 left_us = endt_us-now_us;
  struct timespec timeout = {(time_t)(left_us/Million(1)), (long)((left_us%Million(1))*Thousand(1))};
  return pthread_cond_timedwait_relative_np(&s->cv, &s->mutex, &timeout) == 0;
#else
  struct timespec timeout = {(time_t)(endt_us/Million(1)), (long)((endt_us%Million(1))*Thousand(1))};
  return pthread_cond_timedwait(&s->cv, &s->mutex, &timeout) == 0;
#endif
}

internal POSIX_SOCK_Packet *
posix_sock_packet_alloc(SOCK_Endpoint endpoint, String8 data)
{
  POSIX_SOCK_Packet *p = malloc(sizeof(*p)+data.size);
  if(p != 0)
  {
    *p = (POSIX_SOCK_Packet){0};
    p->endpoint = endpoint;
    p->data = str8((U8 *)(p+1), data.size);
    if(data.size != 0) { MemoryCopy(p->data.str, data.str, data.size); }
  }
  return p;
}

internal void
posix_sock_send_release(POSIX_SOCK_Session *s, POSIX_SOCK_Packet *p)
{
  pthread_mutex_lock(&s->mutex);
  s->send_bytes -= sizeof(*p)+p->data.size;
  pthread_cond_broadcast(&s->cv);
  pthread_mutex_unlock(&s->mutex);
  free(p);
}

internal U64
posix_sock_recv_space(POSIX_SOCK_Session *s)
{
  pthread_mutex_lock(&s->mutex);
  U64 result = posix_sock_queue_limit-s->recv_bytes;
  pthread_mutex_unlock(&s->mutex);
  return result;
}

internal B32
posix_sock_receive(POSIX_SOCK_Session *s, SOCK_Endpoint endpoint, String8 data)
{
  POSIX_SOCK_Packet *p = posix_sock_packet_alloc(endpoint, data);
  pthread_mutex_lock(&s->mutex);
  B32 result = p != 0 && sizeof(*p)+data.size <= posix_sock_queue_limit-s->recv_bytes;
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
  pthread_cond_broadcast(&s->cv);
  pthread_mutex_unlock(&s->mutex);
  if(result && s->wakeup_hook != 0) { s->wakeup_hook(); }
  return result;
}

// Connections belong only to the I/O thread; callers exchange owned packets.
internal void
posix_sock_connection_close(POSIX_SOCK_Session *s, POSIX_SOCK_Connection **first, POSIX_SOCK_Connection *c)
{
  POSIX_SOCK_Connection **link = first;
  while(*link != c) { link = &(*link)->next; }
  *link = c->next;
  close(c->fd);
  for(POSIX_SOCK_Packet *p = c->first_send, *next = 0; p != 0; p = next)
  {
    next = p->next;
    posix_sock_send_release(s, p);
  }
  free(c);
}

////////////////////////////////
//~ Session I/O Thread

internal void
posix_sock_io_thread(void *ptr)
{
  POSIX_SOCK_Session *s = (POSIX_SOCK_Session *)ptr;
  POSIX_SOCK_Connection *first = 0;
  U64 connection_count = 0;
  ThreadNameF("posix_sock_io");
  for(;;)
  {
    Temp scratch = scratch_begin(0, 0);
    pthread_mutex_lock(&s->mutex);
    B32 stopping = s->stopping;
    POSIX_SOCK_Packet *sends = s->first_send;
    s->first_send = s->last_send = 0;
    pthread_mutex_unlock(&s->mutex);

    for(POSIX_SOCK_Packet *p = sends, *next = 0; p != 0; p = next)
    {
      next = p->next;
      p->next = 0;
      POSIX_SOCK_Connection *c = first;
      while(c != 0 && !MemoryMatchStruct(&c->endpoint, &p->endpoint)) { c = c->next; }
      if(!stopping && c == 0 && connection_count < posix_sock_connection_limit)
      {
        int fd = socket(AF_INET, SOCK_STREAM, 0);
        if(fd != -1)
        {
          struct sockaddr_in addr = {0};
          addr.sin_family = AF_INET;
          addr.sin_port = htons(p->endpoint.port);
          MemoryCopy(&addr.sin_addr, p->endpoint.address_u8, sizeof(addr.sin_addr));
          if(posix_sock_prepare(fd))
          {
            int code = connect(fd, (struct sockaddr *)&addr, sizeof(addr));
            if(code == 0 || errno == EINPROGRESS)
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
          if(c == 0) { close(fd); }
        }
      }
      if(!stopping && c != 0) { SLLQueuePush(c->first_send, c->last_send, p); }
      else
      {
        if(!stopping) { posix_sock_receive(s, p->endpoint, str8_zero()); }
        posix_sock_send_release(s, p);
      }
    }
    if(stopping) { scratch_end(scratch); break; }

    struct pollfd *fds = push_array(scratch.arena, struct pollfd, connection_count+2);
    POSIX_SOCK_Connection **connections = push_array(scratch.arena, POSIX_SOCK_Connection *, connection_count+2);
    U64 count = 2;
    U64 space = posix_sock_recv_space(s);
    fds[0] = (struct pollfd){s->wakeup[0], POLLIN, 0};
    fds[1] = (struct pollfd){-1, POLLIN, 0};
    if(connection_count < posix_sock_connection_limit && space > sizeof(POSIX_SOCK_Packet))
    {
      fds[1].fd = s->listener;
    }
    for(POSIX_SOCK_Connection *c = first; c != 0; c = c->next)
    {
      short events = 0;
      if(!c->connecting && space > sizeof(POSIX_SOCK_Packet)) { events |= POLLIN; }
      if(c->connecting || c->first_send != 0) { events |= POLLOUT; }
      if(c->read_hangup && space <= sizeof(POSIX_SOCK_Packet)) { events = 0; }
      // Omit backpressured readers, including their unmaskable HUP events.
      fds[count] = (struct pollfd){events != 0 ? c->fd : -1, events, 0};
      connections[count] = c;
      count += 1;
    }
    int ready = POSIX_SOCK_RETRY_ON_EINTR(poll(fds, (nfds_t)count, -1));
    if(ready < 0)
    {
      pthread_mutex_lock(&s->mutex);
      s->stopping = 1;
      pthread_cond_broadcast(&s->cv);
      pthread_mutex_unlock(&s->mutex);
      scratch_end(scratch);
      break;
    }
    if(fds[0].revents != 0)
    {
      U8 bytes[128];
      while(POSIX_SOCK_RETRY_ON_EINTR(recv(s->wakeup[0], bytes, sizeof(bytes), 0)) > 0) {}
    }
    if(fds[1].revents & POLLIN)
    {
      struct sockaddr_in addr = {0};
      socklen_t size = sizeof(addr);
      int fd = POSIX_SOCK_RETRY_ON_EINTR(accept(s->listener, (struct sockaddr *)&addr, &size));
      if(fd != -1)
      {
        POSIX_SOCK_Connection *c = 0;
        if(posix_sock_prepare(fd)) { c = calloc(1, sizeof(*c)); }
        if(c != 0)
        {
          c->fd = fd;
          c->endpoint.port = ntohs(addr.sin_port);
          MemoryCopy(c->endpoint.address_u8, &addr.sin_addr, sizeof(addr.sin_addr));
          SLLStackPush(first, c);
          connection_count += 1;
        }
        else { close(fd); }
      }
    }
    for(U64 idx = 2; idx < count; idx += 1)
    {
      POSIX_SOCK_Connection *c = connections[idx];
      short events = fds[idx].revents;
      if(events == 0) { continue; }
      if(events & (POLLHUP|POLLERR)) { c->read_hangup = 1; }
      B32 disconnected = (events & POLLNVAL) != 0;
      if(c->connecting && events & (POLLOUT|POLLERR|POLLHUP))
      {
        int error = 0;
        socklen_t size = sizeof(error);
        disconnected = getsockopt(c->fd, SOL_SOCKET, SO_ERROR, &error, &size) != 0 || error != 0;
        c->connecting = 0;
      }
      // Drain data before acting on HUP; recv(0) ends this endpoint once.
      space = posix_sock_recv_space(s);
      if(!disconnected && !c->connecting && events & (POLLIN|POLLHUP|POLLERR) && space > sizeof(POSIX_SOCK_Packet))
      {
        U8 bytes[4096];
        U64 cap = Min(sizeof(bytes), space-sizeof(POSIX_SOCK_Packet));
        ssize_t n = POSIX_SOCK_RETRY_ON_EINTR(recv(c->fd, bytes, cap, 0));
        if(n > 0) { posix_sock_receive(s, c->endpoint, str8(bytes, (U64)n)); }
        else if(n == 0 || (errno != EAGAIN && errno != EWOULDBLOCK)) { disconnected = 1; }
      }
      if(!disconnected && !c->connecting && events & POLLOUT && c->first_send != 0)
      {
        POSIX_SOCK_Packet *p = c->first_send;
        ssize_t n = POSIX_SOCK_RETRY_ON_EINTR(send(c->fd, p->data.str+p->pos, p->data.size-p->pos, POSIX_SOCK_SEND_FLAGS));
        if(n > 0)
        {
          p->pos += (U64)n;
          if(p->pos == p->data.size)
          {
            SLLQueuePop(c->first_send, c->last_send);
            posix_sock_send_release(s, p);
          }
        }
        else if(n == 0 || (errno != EAGAIN && errno != EWOULDBLOCK)) { disconnected = 1; }
      }
      if(disconnected)
      {
        posix_sock_receive(s, c->endpoint, str8_zero());
        posix_sock_connection_close(s, &first, c);
        connection_count -= 1;
      }
    }
    scratch_end(scratch);
  }
  while(first != 0) { posix_sock_connection_close(s, &first, first); }
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
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if(fd != -1)
  {
    int reuse = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(listener_port);
    if(posix_sock_prepare(fd) && bind(fd, (struct sockaddr *)&addr, sizeof(addr)) == 0 && listen(fd, SOMAXCONN) == 0)
    {
      POSIX_SOCK_Session *s = calloc(1, sizeof(*s));
      if(s != 0)
      {
        s->listener = fd;
        s->wakeup_hook = wakeup_hook;
        s->wakeup[0] = s->wakeup[1] = -1;
        if(socketpair(AF_UNIX, SOCK_DGRAM, 0, s->wakeup) == 0 &&
           posix_sock_prepare(s->wakeup[0]) && posix_sock_prepare(s->wakeup[1]))
        {
          if(pthread_mutex_init(&s->mutex, 0) == 0)
          {
            if(posix_sock_cond_init(&s->cv))
            {
              s->io_thread = thread_launch(posix_sock_io_thread, s);
              if(s->io_thread.u64[0] != 0) { result.u64[0] = (U64)s; }
              else { pthread_cond_destroy(&s->cv); }
            }
            if(result.u64[0] == 0) { pthread_mutex_destroy(&s->mutex); }
          }
        }
        if(result.u64[0] == 0)
        {
          if(s->wakeup[0] != -1) { close(s->wakeup[0]); }
          if(s->wakeup[1] != -1) { close(s->wakeup[1]); }
          free(s);
        }
      }
    }
    if(result.u64[0] == 0) { close(fd); }
  }
  return result;
}

internal void
sock_session_close(SOCK_Session session)
{
  POSIX_SOCK_Session *s = (POSIX_SOCK_Session *)session.u64[0];
  if(s != 0)
  {
    pthread_mutex_lock(&s->mutex);
    s->stopping = 1;
    pthread_cond_broadcast(&s->cv);
    pthread_mutex_unlock(&s->mutex);
    posix_sock_wake(s);
    thread_join(s->io_thread, max_U64);
    close(s->listener);
    close(s->wakeup[0]);
    close(s->wakeup[1]);
    for(POSIX_SOCK_Packet *p = s->first_send, *next = 0; p != 0; p = next) { next = p->next; free(p); }
    for(POSIX_SOCK_Packet *p = s->first_recv, *next = 0; p != 0; p = next) { next = p->next; free(p); }
    pthread_cond_destroy(&s->cv);
    pthread_mutex_destroy(&s->mutex);
    free(s);
  }
}

internal SOCK_Endpoint
sock_session_listener_endpoint(SOCK_Session session)
{
  SOCK_Endpoint result = {0};
  POSIX_SOCK_Session *s = (POSIX_SOCK_Session *)session.u64[0];
  if(s != 0)
  {
    struct sockaddr_in addr = {0};
    socklen_t size = sizeof(addr);
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
  POSIX_SOCK_Session *s = (POSIX_SOCK_Session *)session.u64[0];
  if(s == 0 || protocol != SOCK_Protocol_TCP || endpoint.kind != SOCK_EndpointKind_IPv4 || endpoint.port == 0 ||
     data.size > posix_sock_queue_limit-sizeof(POSIX_SOCK_Packet)) { return 0; }
  if(data.size == 0) { return 1; }
  U64 size = sizeof(POSIX_SOCK_Packet)+data.size;
  B32 result = 0;
  pthread_mutex_lock(&s->mutex);
  while(!s->stopping && size > posix_sock_queue_limit-s->send_bytes)
  {
    if(!posix_sock_wait(s, endt_us)) { break; }
  }
  if(!s->stopping && size <= posix_sock_queue_limit-s->send_bytes)
  {
    POSIX_SOCK_Packet *p = posix_sock_packet_alloc(endpoint, data);
    if(p != 0)
    {
      SLLQueuePush(s->first_send, s->last_send, p);
      s->send_bytes += size;
      result = 1;
    }
  }
  pthread_mutex_unlock(&s->mutex);
  if(result) { posix_sock_wake(s); }
  return result;
}

// Each result is a stream chunk. A zero-length result is endpoint EOF.
internal B32
sock_recv(Arena *arena, SOCK_Session session, SOCK_Protocol *protocol_out, SOCK_Endpoint *endpoint_out, String8 *data_out, U64 endt_us)
{
  POSIX_SOCK_Session *s = (POSIX_SOCK_Session *)session.u64[0];
  if(s == 0) { return 0; }
  pthread_mutex_lock(&s->mutex);
  while(!s->stopping && s->first_recv == 0)
  {
    if(!posix_sock_wait(s, endt_us)) { break; }
  }
  POSIX_SOCK_Packet *p = s->first_recv;
  if(p != 0)
  {
    SLLQueuePop(s->first_recv, s->last_recv);
    s->recv_bytes -= sizeof(*p)+p->data.size;
  }
  pthread_mutex_unlock(&s->mutex);
  if(p == 0) { return 0; }
  *protocol_out = SOCK_Protocol_TCP;
  *endpoint_out = p->endpoint;
  *data_out = str8_copy(arena, p->data);
  free(p);
  posix_sock_wake(s);
  return 1;
}
