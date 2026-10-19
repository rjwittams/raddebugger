# Socket session probes

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

On Windows, enter an MSVC x64 developer environment, then run:

```bat
src\socket\tests\run_windows_sessions.cmd
src\socket\tests\run_windows_sessions.cmd --case:large_duplex
```

That runner uses `py -3` for the same 30-second outer timeout and accepts
additional compiler/linker flags in `SOCKET_TEST_CFLAGS`.

These tests cross the public session interface using native TCP peers or a
second session. They do not inspect provider queues. They cover listener
addresses and instance separation, byte delivery in both directions, binary
8 MiB full-duplex traffic, queue deadlines/backpressure, EOF/reset/refusal,
reentrant wakeup and close/bind-failure resource cleanup. The backpressured
disconnect case also checks that an idle session does not busy-poll.

## Provider contract at this checkpoint

The shared session worker supports IPv4 TCP on macOS, Linux and Windows.
Listeners bind only to `127.0.0.1`;
port zero requests an OS-assigned port. `sock_session_listener_endpoint`
returns that actual endpoint. UDP and IPv6 sends fail explicitly.

`sock_send` accepts the whole buffer into a bounded queue or returns false;
it never silently truncates. Success is not a peer acknowledgement. The
deadline bounds waiting for queue space, not a complete network transaction.
Each send/receive queue has a 16 MiB memory budget, including bookkeeping;
split larger streams into smaller sends. A session admits up to 64 concurrent
connections, including ended peers awaiting EOF queue space. A failed outbound
attempt that never obtained a peer id reports endpoint EOF when the receive
queue has space; callers must still bound transactions with deadlines.

Each successful `sock_recv` returns a stream chunk and `SOCK_Peer` reference,
not a complete command or one matching send call. It preserves arbitrary bytes.
The peer retains a stable, nonzero id across chunks; an outbound failure before
connection creation can return a zero id with EOF. A zero-length chunk ends
the peer, including reset/refusal; read-half-close is treated as
endpoint closure. A false result means no queued result before the deadline,
or a stopped session. Failure leaves output arguments unchanged.

`sock_send` addresses an outbound endpoint and can open/reuse an outgoing
connection. Replies or follow-up writes for a received peer use
`sock_send_peer`, which pins that connection. Stale references never reconnect
or target a new peer that reuses the address. Peer ids do not repeat within
the process and are not network authentication tokens; network endpoints and
RDI layouts remain unchanged.

`sock_peer_abort` queues per-peer teardown and discards unsent output for that
peer. A fixed 64-entry, deduplicated control queue is separate from the data
budget, so stalled output does not consume abort capacity. A stale abort has
no effect on replacement connections. EOF is published after the native socket
closes. If incoming data fills the receive queue, the worker retains the ended
peer until it can enqueue EOF, without stopping unrelated connections.
Acceptance of an abort alone is not a synchronous close barrier.
The owner still observes the session's no-concurrent-close rule below.

Additional cases check stable/distinct ids, isolated abort, abort under data
send/receive backpressure, and stale send/abort after a native client reuses the exact old
address and port. The replacement must receive only its own reply.

The wakeup hook runs on the I/O thread outside the queue mutex and may make
nonblocking receive calls. It must return promptly and must not close the
session from that thread. The owner calls `sock_session_close` after its own
API calls have finished; concurrent use of a handle during/after close is not
supported. Close wakes and joins I/O before releasing descriptors and storage.

This is not command framing, IPC dispatch, authentication, debugger-target or
GUI coverage. The worker shares queue ownership, partial-write progress and
shutdown across OS adapters. Linux uses `MSG_NOSIGNAL` sends and monotonic
condition waits; Darwin retains `SO_NOSIGPIPE` and relative condition waits.
Neither changes the process's SIGPIPE disposition. Windows uses `WSAPoll`,
SRW condition waits and an internal connected UDP wakeup pair bound only to
loopback. This does not add public UDP support. Windows listeners retain
exclusive binding, and accepted/outgoing sockets are non-inheritable.

On Windows the fixture primes native nonblocking-connect runtime state before
counting handles. A standalone native control reproduces the AFD, completion
and event handles retained on first use, without any RAD socket-session code.
Resource assertions then require exact equality, both for the 64-cycle
close/bind case and the whole suite; there is no extra-handle allowance.

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

Each listener case exits its process; that runner alone does not assert
successful-session resource cleanup or prove large sends, EOF handling,
application async-lane progress, IPC authentication or complete framed
requests/replies. The session suite above exercises stream/shutdown behavior
and descriptor/handle cleanup across the three OS adapters. Constructor
allocation/thread failures have not been forced by these probes. Windows
runtime checks currently use Windows 11; older Windows builds' `WSAPoll`
connect-failure behavior is not covered.
