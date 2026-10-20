#!/bin/bash
set -euo pipefail

# Uses CoreText and the real font cache/draw runs, without a window or GPU.
cd "$(dirname "$0")/../../../.."
mkdir -p build
clang -x objective-c -Isrc -Ilocal -g -O0 -D_GNU_SOURCE \
  -Wno-initializer-overrides -Wno-unused-value -Wno-writable-strings \
  -Wno-deprecated-declarations -Wno-incompatible-pointer-types-discards-qualifiers \
  src/mac/font_provider/tests/text_positions.c \
  -framework Cocoa -framework Security -framework CoreText \
  -framework CoreGraphics -framework CoreFoundation -lpthread -lm \
  -o build/mac_text_positions
build/mac_text_positions
