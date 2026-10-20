#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/../../../.."
mkdir -p build
# UI interaction tests use production code; unused rendering paths are discarded.
clang -x objective-c -Isrc -Ilocal -g -O1 -D_GNU_SOURCE \
  -Wno-initializer-overrides -Wno-unused-value -Wno-deprecated-declarations \
  -Wno-incompatible-pointer-types-discards-qualifiers -Wno-undefined-internal \
  src/mac/window_manager/tests/scroll_events.c \
  -framework Cocoa -framework Security -framework QuartzCore \
  -o build/mac_scroll_events
build/mac_scroll_events
