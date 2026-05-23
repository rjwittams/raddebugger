#!/usr/bin/env bash
set -u

cd "$(dirname "$0")/.."

if [[ "$(uname -m)" != "arm64" ]]; then
  echo "skip: native ARM64 smoke requires an Apple Silicon host" >&2
  exit 77
fi

work_dir="${RADDBG_ARM64_SMOKE_DIR:-/tmp/raddebugger-arm64-smoke}"
mkdir -p "$work_dir"

target="$work_dir/arm64_runtime_smoke"
user_file="$work_dir/smoke.raddbg_user"
project_file="$work_dir/smoke.raddbg_project"
raddbg_log="$work_dir/raddbg.log"

clang -g -O0 -fno-omit-frame-pointer -arch arm64 local/arm64_runtime_smoke.c -o "$target" || exit 1
bash build.sh raddbg || exit 1

cat >"$project_file" <<EOF
// raddbg 0.9.27 project file

target:
{
  executable: "$target"
  working_directory: "$work_dir"
  enabled: 1
}

breakpoint:
{
  label: "watch_value write"
  address_location: "watch_value"
  address_range_size: 8
  break_on_write: 1
  enabled: 1
}
EOF

./build/raddbg --user:"$user_file" --project:"$project_file" >"$raddbg_log" 2>&1 &
raddbg_pid=$!
cleanup()
{
  kill "$raddbg_pid" >/dev/null 2>&1 || true
}
trap cleanup EXIT

ipc()
{
  ./build/raddbg --ipc --pid:"$raddbg_pid" "$@"
}

wait_for_ipc()
{
  local out
  for _ in {1..80}; do
    out="$(ipc dump_processes 2>/dev/null || true)"
    if [[ "$out" == processes:* ]]; then
      return 0
    fi
    sleep 0.125
  done
  echo "error: raddbg IPC did not become ready" >&2
  cat "$raddbg_log" >&2
  return 1
}

wait_for_stop()
{
  local out output
  for _ in {1..120}; do
    output="$(ipc dump_output 4096 2>/dev/null || true)"
    if [[ "$output" == *"taskport authorization failed"* ]]; then
      echo "$output" >&2
      echo "error: run security authorize -u -P system.privilege.taskport from a logged-in Terminal session, then rerun this smoke" >&2
      return 86
    fi
    out="$(ipc dump_threads 2>/dev/null || true)"
    if [[ "$out" == *"process#0"* && "$out" == running:0* && "$out" != *"rip:0x0"* ]]; then
      printf '%s\n' "$out"
      return 0
    fi
    sleep 0.25
  done
  echo "error: target did not stop in time" >&2
  ipc dump_processes >&2 || true
  ipc dump_threads >&2 || true
  ipc dump_output 4096 >&2 || true
  return 1
}

expect_contains()
{
  local haystack="$1"
  local needle="$2"
  local label="$3"
  if [[ "$haystack" != *"$needle"* ]]; then
    echo "error: missing $label ($needle)" >&2
    printf '%s\n' "$haystack" >&2
    return 1
  fi
}

wait_for_ipc || exit 1

ipc run >/dev/null || true
wait_for_stop || exit $?

regs="$(ipc dump_registers q0 q8 q31)"
printf '%s\n' "$regs"
expect_contains "$regs" "q0:11111111111111111111111111111111" "q0 vector value" || exit 1
expect_contains "$regs" "q8:88888888888888888888888888888888" "q8 vector value" || exit 1
expect_contains "$regs" "q31:31313131313131313131313131313131" "q31 vector value" || exit 1

ipc run >/dev/null || true
watch_stop="$(wait_for_stop)" || exit $?
printf '%s\n' "$watch_stop"
expect_contains "$watch_stop" "last_stop:{cause:" "watchpoint stop context" || exit 1

ipc run >/dev/null || true
unwind_stop="$(wait_for_stop)" || exit $?
printf '%s\n' "$unwind_stop"
stack="$(ipc dump_call_stack)"
printf '%s\n' "$stack"
expect_contains "$stack" "frames:" "call stack frame summary" || exit 1
expect_contains "$stack" "#0" "at least one concrete frame" || exit 1

echo "ok: ARM64 runtime smoke completed"
