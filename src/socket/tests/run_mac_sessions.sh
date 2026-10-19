#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../../.."
if [[ "$(uname -s)" != "Darwin" ]]; then
  echo 'Darwin provider tests require macOS.' >&2
  exit 1
fi
mkdir -p build
${CC:-clang} -g -O0 -I src -D_GNU_SOURCE -Wno-unused-function -Wno-unused-variable \
  ${SOCKET_TEST_CFLAGS:-} src/socket/tests/mac_sessions.c -lpthread -lm -lproc -o build/mac_socket_sessions
python3 -c 'import subprocess,sys; subprocess.run(sys.argv[1:],check=True,timeout=30)' ./build/mac_socket_sessions "$@"
