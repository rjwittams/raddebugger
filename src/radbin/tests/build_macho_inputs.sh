#!/usr/bin/env bash
set -euo pipefail
task_wt=$1
task_output=$2
mkdir -p "$task_output"
task_output=$(cd "$task_output" && pwd)
xcrun clang --version >"$task_output/compiler.txt"
xcrun dsymutil --version >"$task_output/dsymutil.txt"
for task_arch in arm64 x86_64; do
  for task_version in 4 5; do
    task_name=$task_arch-dwarf$task_version
    xcrun clang -arch "$task_arch" -O0 -gdwarf-"$task_version" -c \
      "$task_wt/src/raddbg/tests/inline.c" -o "$task_output/$task_name.o"
    xcrun clang -arch "$task_arch" "$task_output/$task_name.o" -o "$task_output/$task_name"
    xcrun dsymutil "$task_output/$task_name" -o "$task_output/$task_name.dSYM"
    xcrun dwarfdump --uuid "$task_output/$task_name" >"$task_output/$task_name.uuid.txt"
  done
done
