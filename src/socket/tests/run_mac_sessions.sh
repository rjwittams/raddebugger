#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../../.."
if [[ "$(uname -s)" != "Darwin" ]]; then
  echo 'Darwin provider tests require macOS.' >&2
  exit 1
fi
exec bash src/socket/tests/run_posix_sessions.sh "$@"
