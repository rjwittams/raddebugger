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
Linux fixture builds use Clang. `c-elf-exec` and `c-elf-pie` run the ordinary
C stepping/evaluation checks with explicitly non-PIE and PIE executables.
They verify the resulting ELF type before launching; other hosts report
these Linux-loader cases as uncovered. Their `-no-index` variants omit the
optional `.eh_frame_hdr` count/table encodings in the generated executable,
leaving its unwind records intact, then run the same C assertions. These
check ELF unwind-index synthesis for both executable types.
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
(missing tool or unsupported host). `all` runs the built-in cases and adds the
language cases when `--examples` is supplied; ARM64-only cases
remain visibly uncovered on other hosts. Use `--case c`, `c-exit-37`, or
`c-detach` for the portable C checks. `c-memory` also checks that an unmapped
scalar, an unmapped pointer slot and arithmetic requiring an unmapped read
render as `<unavailable>`, while a valid local and a zero-valued expression
remain readable. This does not distinguish a pending cache read from a
permanently unreadable address, or guarantee freshness of successful reads.

Cases cover source breakpoint identity/path, restart, Over/Into/Out, leaf and
caller locals, frame selection, exit 0 and exit 37, and detach after a previous
normal exit. On macOS the basic C cases also require one startup frame mapped
to `/usr/lib/dyld` at the first stop and after stepping; the detach case checks
it again after restart. This checks module discovery, not a symbol-name fallback.
The ARM64 cases check compact/EH-frame instruction boundaries, plus cold
TLS red-zone preservation. `unwind-shrink-wrap` adds a conditional early
return before the frame-record prologue and checks every fall-through
instruction boundary. `unwind-shrink-wrap-body` starts directly in the body,
before a non-restore instruction; `unwind-shrink-wrap-return` takes the early
return without creating a frame. These cover a direct-return guard, not
arbitrary control flow or every compiler's shrink-wrapping pattern. `caller-registers`
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
x64 with MSVC/PDB. `all` contains 25 built-in cases; the eleven ARM64-only cases remain
uncovered on other hosts. The Mach-O `symbol-library` case is also uncovered
on Linux and Windows; the four ELF cases are uncovered outside Linux.
Supplying `--examples` adds the five language cases, for 30 total.
These results do not cover Intel Mac live debugging.

`symbol-library` loads a library built without debug info and stripped of
debug symbols. It requires symbol-table names and offsets for its leaf and
caller, no invented function/source information, and ordinary debug-info
resolution for the application's `main`. It also waits for the library's
debug-info load to settle as unavailable. The ARM64 unwind variants
check `_unwind_leaf` name-only labels and exact offsets at their instruction
boundaries. The `macho_symbol_fallback` torture case checks executable-section
boundaries, deterministic aliases, malformed names/tables, UUID/CPU matching
and universal-image slice selection.

## External language fixtures

```sh
python3 src/raddbg/tests/run_smoke.py --debugger build/raddbg \
  --case languages --examples /path/to/raddebugger_examples
```

`language-c`, `language-cpp`, `language-rust`, `language-odin` and `language-zig`
also run individually. The examples checkout must contain
`examples/language-basics/build.py`; there is no implicit clone, fetch or sibling
directory lookup. The runner records both repository revisions/statuses and
the builder/source hashes. It retains compiler commands, versions, debug-info
hashes, raw IPC and RAD logs in each fresh output directory. Python 3.9 relative
output roots and macOS `/var` aliases resolve to absolute paths before building.

Each case checks a caller source breakpoint, an enabled function breakpoint,
progress past its prologue, Over/Into/Out, source paths/lines, leaf and caller
scalar values through `debug_eval`, a later user breakpoint, and normal exit.
A second launch checks Into with the function breakpoint still enabled. The
prologue allowance is bounded and requires PC progress; it does not skip
missing source lines or disable user breakpoints. Failures do not retry a case
until it passes. Missing compilers remain uncovered.

These are unoptimized scalar cases, not a claim of complete language support.
They initially ran on Kiwi ARM64 only; other hosts need their own validation.
No new IPC commands or RDI structure changes are required.

## Frame-explicit scalar evaluation

`debug_eval` is separate from legacy `eval`. It takes one flat, comma-separated
Metadesk request and returns one JSON object. The IPC transport still requires
serialized clients. `Client.evaluate(stop, frame, expression)` constructs the
request from a `debug_snapshot` result; frame is a logical frame ID in that
snapshot. The context is valid only for the same RAD instance.

Required fields are `run_gen`, `stop_count`, `mem_gen`, `reg_gen`,
`process_handle`, `thread_id`, `frame`, `frame_pc`, `unwind_index`,
`inline_depth` and `expr`. The first four and process/thread identity come
from the snapshot; the frame row supplies the remaining identity fields.
`expr` is a quoted RAD expression. Missing, unknown or repeated fields are
invalid. The command does not fall back to the selected GUI thread/frame.
It compares both generations and frame identity, so a stale request cannot
silently refer to a different logical frame after debug-info loading.

The v1 response has `status`, `complete`, `reason`, `type`, `value` and an
echoed `context`, with these status meanings:

| Status | Meaning |
| --- | --- |
| `ready` | A scalar value was evaluated without a detected generation change or stale required read. |
| `pending` | Required stack, debug info or memory has not settled. No value; `complete` is false. |
| `unavailable` | Required registers/memory/debug info are unavailable, or stack reconstruction failed. |
| `stale` | The supplied generations/frame identity no longer match, or live registers disagree with the reconstructed stack. Take a new snapshot. |
| `running` | Targets are running; this command does not evaluate against a moving target. |
| `invalid_request` | Malformed/missing fields, invalid process/thread/frame, empty expression or oversized request. |
| `error` | Parse, name-resolution or interpretation error, including division by zero. |
| `unsupported` | Non-scalar result, inline scope, TLS access, unsupported space or exhausted read budget. |
| `too_large` | The response would exceed the transport allowance. This minimal response omits the other descriptive fields. |

Every status except `pending` has `complete: true`; completion is not success.
Only `ready` contains a value. That value has `bytes` (hex), `byte_size` and
`byte_order: "little"`, matching the supported x86/x64/ARM64 targets. `type`
is a display string, not a stable type ID. The command supplies scalar bytes,
not GUI-rendered values or a full type/children interface.

Each query owns its evaluation cache and restores the previous evaluator
context. It uses no GUI macros/display rules and provides no write or target-call
hook. RAD's `=` defines an evaluator macro, not a target assignment; such a
definition exists only within that query. TLS is unsupported in this increment,
including platform TLS resolution that would execute code in the target.
Inline frames and concrete frames whose PC is inside an inline scope are
unsupported until scope selection is implemented. Aggregate/member expansion
is deferred, though scalar member expressions use the existing evaluator.

Cache waits share a 10 ms deadline; this is not a wall-clock deadline for all
parsing/evaluation work. Requests are limited to 8 KiB, expressions to 4 KiB,
target/register reads to 64 KiB per query and replies to 60 KiB. Register reads
honor reconstructed byte availability, and process reads request fresh cache
data. Required debug-info changes during evaluation return pending. The engine
can leave some unreadable memory pending indefinitely; the caller must bound
its own wait. `Client.evaluated` retries only `pending`, for at most ten seconds,
and never turns a completed error/unavailable result into success.

`c-eval` checks leaf/caller values without GUI frame selection, genuine zero,
unreadable memory, invalid context, old-stop rejection, and query-local macro
isolation. `caller-registers` also checks explicit caller x0 and partial q8
availability on ARM64. `c-inline` checks the unsupported-inline contract,
not inline evaluation support. Deliberately forced cold-loading schedules,
nonselected threads, target-memory mutation between queries, C++ members and
oversized replies still need dedicated coverage.

`c-assert` and `c-assert-optimized` use a `__builtin_trap()` assertion at
`-O0` and `-O2`. They require the assert's source line, the faulting function
and its callers, the local value and a caller's local. On ARM64 they also
require the stopped PC to point at Clang's `BRK #1`, since an instruction
after the trap can still share its source line. Only the unoptimized case
checks the parameter value: the optimized fixture exposes a separate missing
parameter-location result, despite LLDB and the raw x0 register showing 20.
The Windows/MSVC runner reports these two cases as uncovered. Continuing
after this noreturn builtin is not valid target behavior; use `line_step.S`
and `line_step_ipc.py --case over` to check resumption after a returning trap.

Fresh `c-assert` runs also exercise cold debug-info loading: on Linux, the
locals cache previously retained an empty map built while RDI was pending.
The stack could then report ready while `eval input` still failed. Repeating
the case with a new fixture each time caught this race; reusing an already
converted binary did not. The test does not force that scheduling window,
and it does not cover the corresponding C++ member-map path. Keep the first
evaluation failure as a failure, without retrying it into a passing result.

DWARF conversion resolves inline identity through abstract origins and
specifications. It splits the CU's innermost-body lines into the existing
RDI per-frame tables, using inline call-file/line attributes for callers.
The original line sequences still supply source-breakpoint lookup. Lexical
blocks do not terminate the inline chain in stack construction, source lookup,
snapshot output or UI frame naming. No RDI format change is involved.

Incremental Windows linking, broader optimized/inlined behavior, other missing
debug-info cases, and other GUI workflows need separate coverage. `eval` still uses the
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
resolved debug information; a symbol-table label does not fill these fields.

Each frame also has `symbol: {name, source, address, offset}`. Its source is
`debug_info`, `symbol_table`, or `none`; addresses and offsets are hexadecimal
strings. Debug-info names take precedence. A symbol-table result names the
nearest preceding code symbol in the same executable section, not a proven
containing function. It must not supply source, scope, or stepping ranges.
Missing names have zero address/offset. Inline names come from RDI; their
anchor is the observed PC with zero offset, since an inline site need not
have one contiguous range. The GUI marks fallback labels with `[symbol]`.

`debug_info_status` is `none` (no referenced load), `pending` (queued/loading),
`ready` (loaded RDI), or `unavailable` (completed without RDI). Unavailable
does not distinguish missing inputs, unsupported formats and conversion
failure. `symbols_pending` now means at least one returned frame has a
pending debug-info load; false does not promise names for every frame.
If loading finishes between queries but this snapshot still has no RDI,
that frame stays pending until another observation can expose it.

The fallback provider currently reads 64-bit Mach-O symbol tables, cached
with each loaded module. It first tries target memory, then a local image
with matching UUID, CPU type and subtype. It supports thin and big-endian
FAT32/FAT64 containers. Reads are bounded to 16 MiB of symbol entries and
64 MiB of strings; the on-disk fallback also caps the whole file at 64 MiB.
Names must terminate within 4096 bytes. Same-address aliases use bytewise
name order. There is no ELF/PE export provider, export-trie parser, shared-cache
local-symbol extraction or symbol download in this change. A library stripped
of all usable names can still display an address. RDI layout is unchanged.
