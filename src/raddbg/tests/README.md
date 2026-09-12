# Live IPC smoke checks

Build RAD first, then run from the source worktree:

```sh
python3 src/raddbg/tests/run_smoke.py --debugger build/raddbg --case all
```

The runner needs Python 3, Git, and cleat with `launch --tag` support. On
macOS it needs Clang, `xcrun dsymutil` and `xcrun llvm-nm`. RAD must have
permission to debug its own test children. On Windows, run in the MSVC x64
developer environment and pass `--debugger build/raddbg.exe`; fixture builds
use MSVC, `/Zi /Od`, and `/INCREMENTAL:NO` (the inline cases use `/O2`).
Windows also needs PowerShell/CIM to identify RAD beneath cleat's shell;
the lookup only considers the owned session leader and its direct children.
Linux fixture builds use Clang.
A headless build can be supplied as `--sender build/raddbg_non_graphical`.
The sender and debugger must use compatible IPC transports.

Each case builds in a new directory, with one target, its own user/project
configuration, and its own cleat session. It never attaches to an existing
debugger. Cleanup kills attached test targets, verifies their removal from
RAD, then closes that cleat session. Detached C targets run to completion.
The runner captures cleat's screen before closing the session and retains
build artifacts. Recording availability after closure depends on cleat.
This does not isolate RAD's shared symbol cache or user-wide OS permissions.

`--output-root DIRECTORY` retains runs under an existing directory of your
choice; the default is the OS temporary directory. Each result records host,
source revision/status/patch, source and script hashes, compiler commands,
target/debug-info and debugger hashes, and a raw `ipc.jsonl` transcript.
`commands.log` contains compiler and scenario output, with separate
`command-NN.stdout`/`.stderr` files so a daemon inheriting output handles
cannot hold a pipe open indefinitely. `result.json` includes failure and
cleanup details, plus a best-effort `state` query on failure. A dirty source tree is recorded, not silently
identified as its HEAD commit. Binary hashes identify the executable tested;
the runner does not prove that a supplied binary matches the current source.

Exit status is 0 for passed cases, 1 for failure, and 2 for uncovered cases
(missing tool or unsupported host). `all` lists every case; ARM64-only cases
remain visibly uncovered on other hosts. Use `--case c`, `c-exit-37`, or
`c-detach` for the portable C checks.

Cases cover source breakpoint identity/path, restart, Over/Into/Out, leaf and
caller locals, frame selection, exit 0 and exit 37, and detach after a previous
normal exit. The ARM64 cases build five compact/EH-frame variants, checking 46
instruction boundaries, plus cold TLS red-zone preservation. `caller-registers`
checks that an unavailable caller x0 stays unavailable in arithmetic, while
the stopped x0 remains readable. It also checks partial SIMD availability:
the caller's low q8 lane is readable, but its upper lane and whole q8 are not. The
`unwind-unavailable` case deliberately damages the stack: it requires an
explicit pending/error result with no apparently valid frames. It does not
require the existing memory cache to settle an unreadable stack as an error.

These are small C/assembly checks, not the planned five-language regression
matrix. `c-inline` builds an optimized, forced-inline wrapper around a
non-inlined leaf. It requires `leaf`, `wrapper`, `main` in stack order, source
locations, distinct logical frame IDs and shared physical PC/SP for the
inline wrapper and main. It checks the caller's call-site line separately
from the wrapper's body line. `c-inline-nested` requires
`leaf -> wrapper -> outer -> main` at two different call sites, with wrapper
in a header and its call inside a lexical block. It checks each frame's
source file/line, inline depth and shared physical registers, then normal exit.
Both cases pass on macOS ARM64 and Linux x64 with Clang/DWARF, and on Windows
x64 with MSVC/PDB. `all` contains 13 cases; the eight ARM64-only cases remain
uncovered on other hosts. These results do not cover Intel Mac live debugging.

DWARF conversion resolves inline identity through abstract origins and
specifications. It splits the CU's innermost-body lines into the existing
RDI per-frame tables, using inline call-file/line attributes for callers.
The original line sequences still supply source-breakpoint lookup. Lexical
blocks do not terminate the inline chain in stack construction, source lookup,
snapshot output or UI frame naming. No RDI format change is involved.

Incremental Windows linking, broader optimized/inlined behavior, missing
debug info, and other GUI workflows need separate coverage. `eval` still uses the
selected GUI frame and rendered value text. Unwinding now carries byte-level
register validity through the shared register-read path used by evaluation
and GUI value formatting. Unknown bytes render as unavailable; a recovered
zero remains a known value. Frame-explicit evaluation remains follow-up work.

The lower-level `eh_frame_register_availability_rules` torture case exercises
20 ARM64/x64 combinations: same-value and undefined rules, register transfers,
zero recovery, expression evaluation, unknown CFA inputs, partial SIMD saves,
pending memory reads and explicit signal-frame recovery. The existing
`eh_frame_caller_registers` case checks ABI defaults; `pe_leaf_register_availability`
checks Windows x64 leaf defaults and propagation of unknown input bytes.
These synthetic cases do not establish live signal-frame support.
`eh_frame_undefined_return_ends_stack` distinguishes a terminal return-address
rule, as used by Linux `_start`, from a rule that needs unavailable input.
The former completes the stack; the latter remains an unwind error.

## `debug_snapshot` version 1

Send this as the sole command in a serialized IPC request:

```sh
build/raddbg --ipc --pid:PID debug_snapshot
```

It returns one JSON object and does not change frame selection. The engine's
settling loop may revisit the query; the response retains only its latest
observation. Multiple controllers and semicolon-batched snapshot/eval commands
are outside this contract. Command submission is not execution completion.
The client waits for a greater `stop_count`, a stopped target, and a settled
snapshot after each execution command.

Top-level `status` is `ready`, `pending`, or `stale`. `ready` means the
observation is stable, not that unwinding or symbol loading succeeded. Compare
`run_gen`, `stop_count`, `mem_gen`, and `reg_gen` within one debugger lifetime.
Any generation changing during collection makes the response stale. The
60-KiB body limit returns only `{"version":1,"status":"too_large"}`;
there is no truncation or paging. Clients must reject that result explicitly.

`stop` describes the last engine stop event: typed cause, hexadecimal PC,
single reported breakpoint ID (hex digits, `"0"` if absent), and exception
code. To use the breakpoint ID with existing commands, prepend `$`.
`process_id` and `thread_id` are OS IDs for the observed thread; the process
handle supports existing `query:control.HANDLE` commands. `thread_selection`
says whether the thread came from the stop event, GUI selection, or neither.
While running, the last stop is historical and no stack is returned.

`last_process_end` survives launch/restart. Its sequence increments on each
observed process removal; compare it with the pre-run sequence, not just the
exit code. A code is the backend's observed value, not a normalized shell
status. On Mac, signal termination uses the signal number. Synthetic removal
(detach or closing a dump) reports `null`; absent IDs are zero. The last stop
cause can be `error` even following a normal exit in the current engine, so
termination assertions must use this retained process-end record.

`stack.status` is `none`, `pending`, `ready`, or `error`. Stale artifacts are
not returned as current frames. Completed unwind errors retain the engine
flags and physical frame count, but leave the displayed frame array empty,
matching the existing GUI policy. Persistent cache misses can remain pending;
clients need a deadline and must not translate timeout into success.

Frames follow the engine's ordered stack, innermost first, including inline
frames when RDI supplies them. Each has an index ID, physical `unwind_index`,
`inline_depth`, PC/SP/CFA, full module path and module offset, function name,
and source path/line/column plus `is_source`. Inline frames share their
physical frame's registers. IDs are scoped to this debugger and generation
tuple, not permanent handles. PC/SP/CFA and offsets are hexadecimal strings;
zero CFA means the engine did not supply one. These fields make no claim that
arbitrary caller registers are available.

Source paths preserve recorded case and use RAD's debug-info-relative path
resolution. Missing inline line tables produce missing source, not a shifted
location from another inline depth. Empty function/path fields mean no
resolved information. `symbols_pending` means a nonempty debug-info key has no
loaded RDI yet; it does not distinguish loading from permanently missing or
failed conversion, nor does false guarantee that every frame has symbols.
