#!/bin/bash
set -euo pipefail

# Requires a macOS graphical login; no Accessibility permission is needed.
cd "$(dirname "$0")/../../../.."
mkdir -p build
clang -x objective-c -Isrc -Ilocal -g -O0 -D_GNU_SOURCE \
  -Wno-initializer-overrides -Wno-unused-value \
  -Wno-deprecated-declarations -Wno-incompatible-pointer-types-discards-qualifiers \
  src/mac/window_manager/tests/title_bar.c \
  -framework Cocoa -framework Security -framework QuartzCore \
  -o build/mac_title_bar
build/mac_title_bar
