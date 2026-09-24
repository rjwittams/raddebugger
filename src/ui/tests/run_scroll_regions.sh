#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/../../.."
mkdir -p build
# Production UI and font code with stub window/render backends; no desktop input.
clang -x objective-c -Isrc -Ilocal -g -O0 -D_GNU_SOURCE \
  -Wno-initializer-overrides -Wno-unused-value -Wno-deprecated-declarations \
  -Wno-incompatible-pointer-types-discards-qualifiers \
  src/ui/tests/scroll_regions.c \
  -framework Cocoa -framework Security -framework CoreText -framework CoreGraphics \
  -o build/scroll_regions
build/scroll_regions
