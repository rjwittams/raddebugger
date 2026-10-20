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
simultaneous instances, native-random instance/key creation, fresh transaction
ids, stale instance tags after endpoint reuse and explicit client-side rejection.
Wrong-key and payload-tamper cases require rejection
without exposing a command, then a healthy peer. Forged header/payload tags
and modified replies return no bytes. A 20 MiB request and 24 MiB reply cross
the underlying 16 MiB queue budgets. Resource counts must return to exact equality; Windows
uses the socket fixture's native-only Winsock warm-up before counting.

## Construction contract

This is a local transport, not diagnostic dispatch. `IPC_Endpoint` holds an
IPv4 loopback address, a 16-byte nonzero instance id and a 32-byte nonzero
authentication key. `ipc_server_open_random` creates both with native entropy:
`BCryptGenRandom` on Windows, `getrandom(GRND_NONBLOCK)` on Linux and
`getentropy` on macOS. Failure returns no server, without a weak fallback.
Explicit `ipc_server_open` configuration remains available for callers that
already hold an identity/key. Most tests use fixed public fixture keys; the
random-instance case exchanges messages with two fresh identities without
printing their keys. It checks usable, distinct identities, not RNG quality.
Entropy-source failure is not injected by this suite.
The id travels on the wire; the key does not. Private endpoint publication/
discovery is under construction below. Application/CLI integration and
discovery rejection/concurrency qualification remain activation gates.
This authenticates messages, not encrypted transport; it does not protect
against a process that can read the secret.

The public header prefix is 40 bytes: `RDIP`, version 2, kind 1 (request) or 2 (reply),
two reserved zero bytes, little-endian 64-bit nonzero request id, little-endian
64-bit payload length, then the instance id's 16 bytes. A 32-byte HMAC-SHA-256
tag follows, authenticating ASCII `RDIP header v2` (no terminator) followed by
those 40 bytes. Verify this tag and the fields before payload allocation.
After the payload, a second 32-byte tag authenticates ASCII `RDIP payload v2`,
the complete 72-byte header and the payload. Both tags use the per-instance
key. Literal oracle tags come from Python's standard `hmac`/`hashlib`; edits
to native fixtures use a separate one-shot reference composition. Old wire
versions are rejected, not dispatched through a fallback. Payloads are opaque;
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
deadline across send and receive. It generates a fresh native-random nonzero
64-bit request id, requiring that same id in the authenticated reply. Entropy
failure returns unavailable before opening a connection. It returns complete bytes only on success;
errors distinguish timeout, disconnect, invalid reply, oversize and unavailable
transport. A failed call never returns a partial reply. Zero deadline is an
immediate timeout for a transaction, not an asynchronous request handle.
Payload authentication is incremental inside the send/receive loops; the
receiver exposes nothing until the trailing tag is complete and valid.

## Private discovery (under construction)

`ipc_server_open_local` creates or validates a private directory, publishes a
fresh endpoint for the current PID and removes its own registration on close.
A second live publication for that PID fails without disturbing the first;
closing and reopening creates a fresh registration. `ipc_request_local` selects
an explicit PID through that directory, verifies its native process birth and
uses the same authenticated transaction. Oversize input is rejected before
discovery, just as for an explicit endpoint.

POSIX directories/files require current ownership and 0700/0600 modes; symlink,
special-file, multiple-link and macOS extended-ACL objects are rejected.
Windows requires protected, current-user-only full-access DACLs and rejects
reparse/multiple-link objects. Existing unsafe directories are not repaired
with chmod or ACL replacement. The private `.lock` file persists for
cooperating publication operations and contains no credential. Test directories
are retained for inspection.

Registration is a 104-byte `RDIE` v1 record: version/OS/reserved prefix through
byte 7, little-endian PID at 8, 32-byte native process birth at 16, little-endian
port at 48, six reserved zeros, instance id at 56 and key at 72. Windows birth
uses creation FILETIME; macOS uses start seconds/microseconds. Linux hashes
the boot id and start ticks together. This is separate from the RDI format.
Publish complete records atomically, not by rewriting live data. POSIX cleanup
compares the retained file object's device/inode under the publication lock;
Windows deletes through its retained file handle, not a replacement pathname.
No credential or record content is printed by the fixture.

This does not isolate mutually hostile same-user processes, privilege tokens
or login sessions; the caller chooses an exact PID. Privileged attackers and
same-user processes remain outside the private-file threat model. Filesystem
ownership checks and MACs do not provide encryption. The initial public
round-trip/lifecycle case is not full rejection or crash qualification.

No graphical debugger, live target or authorization prompt runs in this suite.
Existing snapshot/base64 schemas and command parsing remain unchanged because
this construction slice is not yet connected to the application loop.
