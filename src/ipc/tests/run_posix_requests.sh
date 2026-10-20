#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../../.."
task_flags=(-Wno-unused-function -Wno-unused-variable -Wno-initializer-overrides -Wno-unused-value)
task_libs=(-lpthread -lm)
case "$(uname -s)" in
  Darwin) task_libs+=(-lproc) ;;
  Linux)
    task_libs+=(-ldl -lrt)
    if [[ "$(uname -m)" == x86_64 ]]; then task_flags+=(-msse4.1); fi
    ;;
  *) echo 'POSIX IPC tests require macOS or Linux.' >&2; exit 1 ;;
esac
mkdir -p build
${CC:-clang} -g -O0 -I src -D_GNU_SOURCE "${task_flags[@]}" \
  ${IPC_TEST_CFLAGS:-} src/ipc/tests/requests.c "${task_libs[@]}" -o build/ipc_requests
python3 -c 'import subprocess,sys; subprocess.run(sys.argv[1:],check=True,timeout=30)' ./build/ipc_requests "$@"
