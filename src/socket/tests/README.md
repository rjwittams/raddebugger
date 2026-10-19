# POSIX socket session probes

Run on macOS or Linux from the repository root:

```sh
bash src/socket/tests/run_posix_sessions.sh
bash src/socket/tests/run_posix_sessions.sh --case:large_duplex
SOCKET_TEST_CFLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' \
  ASAN_OPTIONS=detect_leaks=0:abort_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  bash src/socket/tests/run_posix_sessions.sh
```

The executable has a 30-second outer timeout. An unknown case fails, rather
than succeeding without running a test. `CC` selects the compiler; additional
compile/link flags go in `SOCKET_TEST_CFLAGS`.
The Darwin-only `run_mac_sessions.sh` command remains an alias for this runner.

These tests cross the public session interface using native TCP peers or a
second session. They do not inspect provider queues. They cover listener
addresses and instance separation, byte delivery in both directions, binary
8 MiB full-duplex traffic, queue deadlines/backpressure, EOF/reset/refusal,
reentrant wakeup and close/bind-failure resource cleanup. The backpressured
disconnect case also checks that an idle session does not busy-poll.

## Provider contract at this checkpoint

The shared macOS/Linux provider supports IPv4 TCP. Listeners bind only to `127.0.0.1`;
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
GUI coverage. This runner does not validate Windows transport behavior.
The POSIX I/O worker is shared, with Linux `MSG_NOSIGNAL` sends and monotonic
condition waits; Darwin retains `SO_NOSIGPIPE` and relative condition waits.
Neither provider changes the process's SIGPIPE disposition.

## Portable local-listener checks

On Windows, first enter an MSVC x64 developer environment. On any supported
host, run:

```sh
python3 src/socket/tests/run_local_listeners.py
python3 src/socket/tests/run_local_listeners.py --case occupied_port
```

Use `py -3` instead of `python3` when that is the Windows Python launcher.
The runner builds the native fixture and gives each case its own process and
15-second deadline. An unknown case fails. POSIX builds accept `CC`; additional
compile/link flags go in `SOCKET_TEST_CFLAGS`.

The four cases check loopback-only listener addresses, 64 rejected duplicate
opens without native handle/descriptor growth, independent ephemeral endpoints
with native-peer byte delivery, and refusal of a native reuse-bind contender
at the same address. Windows listeners use `SO_EXCLUSIVEADDRUSE`; no listener
uses `SO_REUSEPORT`. Failed socket/bind/listen construction returns a zero
session instead of silently listening on a different port.

Windows' successful-session close function remains an upstream TODO. Each
listener case exits its process; that runner does not assert successful-session
resource cleanup or prove large sends, EOF handling, application async-lane
progress, IPC authentication or complete framed requests/replies. The POSIX
session suite above exercises stream/shutdown behavior and descriptor cleanup
on macOS/Linux. Windows IOCP/thread setup failure paths remain outside these
listener checks.
