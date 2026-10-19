# Darwin socket session probes

Run on macOS from the repository root:

```sh
bash src/socket/tests/run_mac_sessions.sh
bash src/socket/tests/run_mac_sessions.sh --case:large_duplex
SOCKET_TEST_CFLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' \
  ASAN_OPTIONS=detect_leaks=0:abort_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  bash src/socket/tests/run_mac_sessions.sh
```

The executable has a 30-second outer timeout. An unknown case fails, rather
than succeeding without running a test. `CC` selects the compiler; additional
compile/link flags go in `SOCKET_TEST_CFLAGS`.

These tests cross the public session interface using native TCP peers or a
second session. They do not inspect provider queues. They cover listener
addresses and instance separation, byte delivery in both directions, binary
8 MiB full-duplex traffic, queue deadlines/backpressure, EOF/reset/refusal,
reentrant wakeup and close/bind-failure resource cleanup. The backpressured
disconnect case also checks that an idle session does not busy-poll.

## Provider contract at this checkpoint

The Darwin provider supports IPv4 TCP. Listeners bind only to `127.0.0.1`;
port zero requests an OS-assigned port. `sock_session_listener_endpoint`
returns that actual endpoint. UDP and IPv6 sends fail explicitly.

`sock_send` accepts the whole buffer into a bounded queue or returns false;
it never silently truncates. Success is not a peer acknowledgement. The
deadline bounds waiting for queue space, not a complete network transaction.
Each send/receive queue has a 16 MiB memory budget, including bookkeeping;
split larger streams into smaller sends. A session admits up to 64 concurrent
connections. A failed connection produces an endpoint EOF notification.

Each successful `sock_recv` returns a stream chunk, not a complete command or
one matching send call. It preserves arbitrary bytes. A zero-length chunk
ends the endpoint, including reset/refusal; read-half-close is treated as
endpoint closure. A false result means no queued result before the deadline,
or a stopped session. Failure leaves output arguments unchanged.

The wakeup hook runs on the I/O thread outside the queue mutex and may make
nonblocking receive calls. It must return promptly and must not close the
session from that thread. The owner calls `sock_session_close` after its own
API calls have finished; concurrent use of a handle during/after close is not
supported. Close wakes and joins I/O before releasing descriptors and storage.

This is not command framing, IPC dispatch, authentication, debugger-target or
GUI coverage. Windows/Linux have matching endpoint-query implementations, but
their existing transport behavior is unchanged and is not validated by this
Darwin-only runner.
