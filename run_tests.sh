#!/bin/bash
set -eu
cd "$(dirname "$0")"

target_values=(raddbg raddbg_non_graphical radlink radbin torture)
cc_values=(clang gcc)
mode_values=(debug release)
run_torture=1
torture_args=()

while [[ $# -gt 0 ]]; do
  case "$1" in
    --)
      shift
      torture_args=("$@")
      break
      ;;
    clang)
      cc_values=("$1")
      shift
      ;;
    gcc)
      cc_values=("$1")
      shift
      ;;
    debug|release)
      mode_values=("$1")
      shift
      ;;
    no_torture)
      run_torture=0
      shift
      ;;
    *)
      echo "usage: $0 [clang|gcc] [debug|release] [no_torture] [-- torture-args]"
      exit 1
      ;;
  esac
done

if [[ "${#torture_args[@]}" == "0" ]]; then
  torture_args=("*")
fi

if [[ "$run_torture" == "1" ]]; then
  test_data_dir="local/test_data"
  test_data_version_latest="local/test_data_version_latest.txt"
  test_data_version_current="local/test_data_version_current.txt"
  test_data_archive="local/test_data.zip"
  mkdir -p "$test_data_dir"
  curl -fsSL -o "$test_data_version_latest" https://data.raddbg.com/public/raddbg_test_data_version.txt
  if [[ ! -f "$test_data_version_current" ]] || ! cmp -s "$test_data_version_latest" "$test_data_version_current"; then
    echo "Downloading test data..."
    curl -fsSL -o "$test_data_archive" https://data.raddbg.com/public/raddbg_test_data.zip
    tar -xf "$test_data_archive" -C "$test_data_dir"
    rm -f "$test_data_archive"
    cp "$test_data_version_latest" "$test_data_version_current"
  fi
fi

for m in "${mode_values[@]}"; do
  for c in "${cc_values[@]}"; do
    # nuke artifacts from last run
    rm -rf build/torture_artifacts
    rm -f build/metagen
    for t in "${target_values[@]}"; do
      rm -f "build/$t"
    done

    # build targets
    ./build.sh meta "$c" "$m" "${target_values[@]}"

    # run torture from build folder
    if [[ "$run_torture" == "1" ]]; then
      torture_compiler_args=()
      has_clang_arg=0
      has_gcc_arg=0
      for arg in "${torture_args[@]}"; do
        case "$arg" in
          -clang:*|--clang:*) has_clang_arg=1 ;;
          -gcc:*|--gcc:*) has_gcc_arg=1 ;;
        esac
      done
      if [[ "$c" == clang && "$has_clang_arg" == "0" ]]; then
        torture_compiler_args=("-clang:$(command -v "${CC:-clang}")")
      elif [[ "$c" == gcc && "$has_gcc_arg" == "0" ]]; then
        torture_compiler_args=("-gcc:$(command -v "${CC:-gcc}")")
      fi
      (cd build && ./torture "${torture_compiler_args[@]}" "${torture_args[@]}")
    fi

  done
done
