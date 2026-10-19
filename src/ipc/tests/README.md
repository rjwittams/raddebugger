# Framed command transport probes

From the source root, run `bash src/ipc/tests/run_posix_requests.sh`, or
`src\ipc\tests\run_windows_requests.cmd` in an MSVC x64 environment.
`--case:NAME` selects one case; unknown names fail. The outer timeout is
30 seconds. `CC` and `IPC_TEST_CFLAGS` control POSIX compilation;
Windows accepts additional `IPC_TEST_CFLAGS` as MSVC arguments.

Tests cross the public server/request interface and native TCP sockets.
Literal wire bytes check the format independently of the encoder. Cases cover
fragmented/coalesced input, a 103-frame empty burst, malformed/wrong-instance
peer isolation, truncated input/replies, mismatched reply ids, deadlines,
simultaneous instances, stale instance tags after endpoint reuse and explicit
client-side rejection. A 20 MiB request and 24 MiB reply cross the underlying
16 MiB queue budgets. Resource counts must return to exact equality; Windows
uses the socket fixture's native-only Winsock warm-up before counting.

## Construction contract

This is a local transport, not diagnostic dispatch. `IPC_Endpoint` holds an
IPv4 loopback address and a caller-provided 16-byte nonzero instance id.
The id travels on the wire; it is not an authentication secret or protection
against a process impersonating a stale endpoint. Authentication, safe endpoint
publication/discovery and application/CLI integration remain separate gates.
These tests use fixed ids and do not claim those gates.

The wire header is 40 bytes: `RDIP`, version 1, kind 1 (request) or 2 (reply),
two reserved zero bytes, little-endian 64-bit nonzero request id, little-endian
64-bit payload length, then the instance id's 16 bytes. Payloads are opaque;
no semicolon splitting, UTF-8 conversion or JSON interpretation occurs here.
A frame allows at most 64 MiB. Server-side partial payloads share a 64 MiB
budget across 64 peer slots, in addition to one retained receive chunk and
the socket layer's bounded queues. Completed requests return one at a time,
retaining unread bytes rather than creating an unbounded decoded queue.

The owner serializes server calls, including reply, and closes after its
calls have finished. Reply success means queued bytes, not delivery. Peer
references pin replies to the original connection. Malformed or over-budget
input aborts only that peer; EOF releases its partial state. Partial input
can retain budget until disconnect or server close; no idle-peer timer is
claimed. Nonblocking receives use deadline zero; partial data never becomes
a command and false receives leave output arguments unchanged.

`ipc_request` owns one connection per transaction, sharing a single absolute
deadline across send and receive. It returns complete bytes only on success;
errors distinguish timeout, disconnect, invalid reply, oversize and unavailable
transport. A failed call never returns a partial reply. Zero deadline is an
immediate timeout for a transaction, not an asynchronous request handle.

No graphical debugger, live target or authorization prompt runs in this suite.
Existing snapshot/base64 schemas and command parsing remain unchanged because
this construction slice is not yet connected to the application loop.
