#!/bin/bash
set -euo pipefail

cd "$(dirname "$0")/../../../.."
if [[ "$(uname -s)" != Darwin || "$(uname -m)" != arm64 ]]; then
  echo "This test requires an ARM64 macOS host." >&2
  exit 2
fi

# Keep each run isolated and leave its binaries available for inspection.
mkdir -p build
test_dir=$(mktemp -d "$PWD/build/trap-lifecycle.XXXXXX")
clang -g -O0 src/mac/demon/tests/trap_target.c -o "$test_dir/target"
clang -x objective-c -Isrc -Ilocal -g -O0 -D_GNU_SOURCE \
  -Wno-initializer-overrides -Wno-unused-value -Wno-deprecated-declarations \
  -Wno-incompatible-pointer-types-discards-qualifiers \
  src/mac/demon/tests/trap_lifecycle.c -framework Cocoa -framework Security \
  -o "$test_dir/test"
codesign --force --sign - --entitlements src/mac/raddbg_debug.entitlements "$test_dir/test"
for mode in continue call; do
  RAD_TRAP_TEST_TARGET="$test_dir/target" RAD_TRAP_TEST_MODE="$mode" "$test_dir/test"
done
