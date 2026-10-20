# Binary Ninja ARM64 Disassembler

This directory vendors the ARM64 disassembler from Vector35's Binary Ninja API repository.

- Source: `https://github.com/Vector35/binaryninja-api/tree/v5.0.7648-stable/arch/arm64/disassembler`
- Tag: `v5.0.7648-stable`
- Commit: `7659bfc94c2c50fd7c3f695559b0cec62ffe6fa3`
- License: MIT, copied in `LICENSE.txt`

The vendored files are the runtime decode/format implementation only. Upstream test programs,
test corpora, Makefiles, `.gitignore`, `gofer.c`, `encodings_fmt.c`, and the older `sysregs.c/.h`
are intentionally omitted. `encodings_fmt.h` is retained because the formatter header includes it,
but RAD does not currently call the upstream encoding-name formatter.

Local changes:

- `pcode.h` has a conventional include guard so the source can be consumed by RAD's unity build.
- `bn_arm64_disasm.c/.h` provide a small RAD-facing bridge with prefixed symbols and basic
  control-flow classification, scalar register extraction, memory-base offsets, and stack-pointer
  change flags.
- `bn_arm64_disasm_prefix.h` mechanically prefixes upstream global function names while including
  the vendor source. This avoids compile-time collisions in RAD's unity build and keeps accidental
  unprefixed upstream symbols out of the final binary.

This is intended as ARM64 footing. If a smaller no-allocation ARM64 decoder becomes available or
practical to write, this vendor can be replaced behind the same bridge.
