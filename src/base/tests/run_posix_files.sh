#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../../.."
task_source_root=${BASE_FILE_TEST_SOURCE_ROOT:-.}
task_flags=()
task_libs=(-lpthread -lm)
case "$(uname -s)" in
  Darwin) task_libs+=(-lproc) ;;
  Linux)
    task_libs+=(-ldl -lrt)
    if [[ "$(uname -m)" == x86_64 ]]; then task_flags+=(-msse4.1); fi
    ;;
  *) echo 'File tests require macOS or Linux.' >&2; exit 1 ;;
esac
mkdir -p build
${CC:-clang} -g -O0 -I "$task_source_root/src" -D_GNU_SOURCE \
  -Wno-unused-function -Wno-unused-variable -Wno-initializer-overrides \
  -Wno-unused-value "${task_flags[@]}" ${BASE_FILE_TEST_CFLAGS:-} \
  src/base/tests/files.c "${task_libs[@]}" -o build/base_files_tests
python3 src/base/tests/run_files.py build/base_files_tests "$@"
