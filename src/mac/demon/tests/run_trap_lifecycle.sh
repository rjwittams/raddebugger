#!/bin/bash
set -euo pipefail

cd "$(dirname "$0")/../../../.."
if [[ "$(uname -s)" != Darwin ]]; then
  echo "This test requires a macOS host." >&2
  exit 2
fi

# Repetition helps catch signal handoff races without rebuilding every time.
repeats=${RAD_TRAP_TEST_REPEAT:-1}
if [[ ! "$repeats" =~ ^[1-9][0-9]*$ ]]; then
  echo "RAD_TRAP_TEST_REPEAT must be a positive integer." >&2
  exit 2
fi

# Keep each run isolated and leave its binaries available for inspection.
mkdir -p build
test_dir=$(mktemp -d "$PWD/build/trap-lifecycle.XXXXXX")
if [[ "$(uname -m)" == arm64 ]]; then
  clang -g -O0 src/mac/demon/tests/trap_target.c -o "$test_dir/target"
fi
clang -g -O0 src/mac/demon/tests/fault_target.c -o "$test_dir/fault-target"
clang -x objective-c -Isrc -Ilocal -g -O0 -D_GNU_SOURCE \
  -Wno-initializer-overrides -Wno-unused-value -Wno-deprecated-declarations \
  -Wno-incompatible-pointer-types-discards-qualifiers \
  src/mac/demon/tests/trap_lifecycle.c -framework Cocoa -framework Security \
  -o "$test_dir/test"
codesign --force --sign - --entitlements src/mac/raddbg_debug.entitlements "$test_dir/test"
modes=("$@")
if [[ ${#modes[@]} -eq 0 ]]; then
  modes=(fault-address)
  if [[ "$(uname -m)" == arm64 ]]; then
    modes+=(continue call detach-init detach-healthy detach-moved-pc detach-trap)
  fi
fi
for mode in "${modes[@]}"; do
  case "$mode" in
    fault-address) ;;
    continue|call|detach-init|detach-healthy|detach-moved-pc|detach-trap)
      if [[ "$(uname -m)" != arm64 ]]; then
        echo "Mode $mode requires an ARM64 macOS host." >&2
        exit 2
      fi
      ;;
    *) echo "Unknown trap lifecycle mode: $mode" >&2; exit 2 ;;
  esac
  target="$test_dir/target"
  if [[ "$mode" == fault-address ]]; then
    target="$test_dir/fault-target"
  fi
  for ((iteration = 0; iteration < repeats; iteration += 1)); do
    RAD_TRAP_TEST_TARGET="$target" RAD_TRAP_TEST_MODE="$mode" \
      RAD_TRAP_TEST_MARKER="$test_dir/$mode.marker" "$test_dir/test"
  done
done
