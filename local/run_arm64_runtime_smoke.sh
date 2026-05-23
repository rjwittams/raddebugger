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
  label: "vector ready"
  address_location: "smoke_vector_ready"
  enabled: 1
}

breakpoint:
{
  label: "unwind leaf"
  address_location: "smoke_unwind_c"
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

thread_rip()
{
  awk 'match($0, / rip:0x[0-9a-fA-F]+ /) { print substr($0, RSTART+5, RLENGTH-6); exit }'
}

hex_to_dec()
{
  local hex="${1#0x}"
  hex="${hex#0X}"
  printf '%u' "$((16#$hex))"
}

symbol_vaddr()
{
  local symbol="$1"
  nm -m "$target" | awk -v symbol="_$symbol" '$NF == symbol { print $1; exit }'
}

text_vmaddr()
{
  otool -l "$target" | awk '/segname __TEXT/{seen=1} seen && /vmaddr/{print $2; exit}'
}

module_base_from_threads()
{
  awk 'match($0, / rip:0x[0-9a-fA-F]+ module:arm64_runtime_smoke\+0x[0-9a-fA-F]+/) {
         text = substr($0, RSTART, RLENGTH)
         sub(/^ rip:/, "", text)
         sub(/ module:arm64_runtime_smoke\+/, " ", text)
         print text
         exit
       }'
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
vector_stop="$(wait_for_stop)" || exit $?
printf '%s\n' "$vector_stop"
vector_rip="$(printf '%s\n' "$vector_stop" | thread_rip)"
module_base_parts="$(printf '%s\n' "$vector_stop" | module_base_from_threads)"
if [[ -z "$module_base_parts" ]]; then
  echo "error: failed to parse module base from stopped thread" >&2
  exit 1
fi
module_rip="${module_base_parts%% *}"
module_off="${module_base_parts##* }"
module_base="$(printf '0x%x' "$(( $(hex_to_dec "$module_rip") - $(hex_to_dec "$module_off") ))")"
vector_stack="$(ipc dump_call_stack)"
printf '%s\n' "$vector_stack"
expect_contains "$vector_stack" "smoke_vector_stop" "vector stop frame" || exit 1

regs="$(ipc dump_registers q0 q8 q31)"
printf '%s\n' "$regs"
expect_contains "$regs" "q0:11111111111111111111111111111111" "q0 vector value" || exit 1
expect_contains "$regs" "q8:88888888888888888888888888888888" "q8 vector value" || exit 1
expect_contains "$regs" "q31:31313131313131313131313131313131" "q31 vector value" || exit 1

watch_symbol="$(symbol_vaddr watch_value)"
text_base="$(text_vmaddr)"
if [[ -z "$watch_symbol" || -z "$text_base" || -z "$module_base" ]]; then
  echo "error: failed to compute runtime watch_value address" >&2
  exit 1
fi
watch_addr="$(printf '0x%x' "$(( $(hex_to_dec "$module_base") + $(hex_to_dec "$watch_symbol") - $(hex_to_dec "$text_base") ))")"
watch_add="$(ipc add_watchpoint "$watch_addr" 8 0 1)"
printf '%s\n' "$watch_add"
expect_contains "$watch_add" "$" "watchpoint add result" || exit 1

ipc run >/dev/null || true
watch_stop="$(wait_for_stop)" || exit $?
printf '%s\n' "$watch_stop"
watch_rip="$(printf '%s\n' "$watch_stop" | thread_rip)"
expect_contains "$watch_stop" "last_stop:{cause:4" "watchpoint stop context" || exit 1
if [[ "$watch_rip" == "$vector_rip" ]]; then
  echo "error: watchpoint phase did not advance past vector stop" >&2
  exit 1
fi

ipc run >/dev/null || true
unwind_stop="$(wait_for_stop)" || exit $?
printf '%s\n' "$unwind_stop"
unwind_rip="$(printf '%s\n' "$unwind_stop" | thread_rip)"
if [[ "$unwind_rip" == "$vector_rip" || "$unwind_rip" == "$watch_rip" ]]; then
  echo "error: unwind phase did not reach a distinct stop" >&2
  exit 1
fi
stack="$(ipc dump_call_stack)"
printf '%s\n' "$stack"
expect_contains "$stack" "frames:" "call stack frame summary" || exit 1
expect_contains "$stack" "smoke_unwind_c" "unwind leaf frame" || exit 1
expect_contains "$stack" "smoke_unwind_b" "unwind caller frame" || exit 1
expect_contains "$stack" "smoke_unwind_a" "unwind caller frame" || exit 1

echo "ok: ARM64 runtime smoke completed"
