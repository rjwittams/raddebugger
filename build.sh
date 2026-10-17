#!/usr/bin/env bash
set -eu
cd "$(dirname "$0")"

if (( BASH_VERSINFO[0] < 4 || (BASH_VERSINFO[0] == 4 && BASH_VERSINFO[1] < 2) )); then
  echo "[ERROR] build.sh requires Bash 4.2 or newer."
  exit 1
fi

# --- Unpack Arguments --------------------------------------------------------
auto_compile_flags=""
for arg in "$@"; do declare $arg='1'; done
if [[ "$#" == "0" ]]; then raddbg='1'; fi
if [[ "$#" == "1" && "${release:-0}" == "1" ]]; then raddbg='1'; fi
if [[ "${asan:-0}" == "1" ]]; then
  echo "[asan enabled]"
  auto_compile_flags="$auto_compile_flags -fsanitize=address"
fi

# --- Mix Git Commit ID -------------------------------------------------------
if git rev-parse --is-inside-work-tree >/dev/null 2>&1; then
  auto_compile_flags="$auto_compile_flags -DBUILD_GIT_HASH=\"$(git describe --always --dirty)\" -DBUILD_GIT_HASH_FULL=\"$(git rev-parse HEAD)\""
fi

# --- Compile/Link Line Definitions -------------------------------------------
host_os=$(uname -s)
host_arch=$(uname -m)
target_arch=${RADDBG_TARGET_ARCH:-$host_arch}
case "$target_arch" in
  x86_64|amd64) target_arch=x86_64 ;;
  arm64|aarch64) target_arch=arm64 ;;
  *) echo "[ERROR] unsupported target architecture: $target_arch" >&2; exit 1 ;;
esac
cc_host_flags=""
if [[ "$host_os" == "Darwin" ]]; then
  cc_host_flags="-arch $target_arch"
fi
if [[ "$target_arch" == "x86_64" ]]; then
  cc_host_flags="$cc_host_flags -mcx16 -msse2"
fi
cc_cflags_gcc=""
cc_cflags_clang=" -fdiagnostics-absolute-paths -Wno-for-loop-analysis  -Wno-incompatible-pointer-types-discards-qualifiers -Wno-initializer-overrides -Wno-compare-distinct-pointer-types -Wno-single-bit-bitfield-constant-conversion -Wno-deprecated-declarations -Wno-writable-strings -Wno-unknown-warning-option -Wno-deprecated-register -Wno-unused-local-typedef"
cc_common=${auto_compile_flags}" $cc_host_flags -I../src/ -I../local/ -D_GNU_SOURCE -g -Wall -Wno-missing-braces -Wno-unused-function -Wno-unused-variable -Wno-unused-but-set-variable -Wno-unused-value -D_USE_MATH_DEFINES -Dstrdup=_strdup -Dgnu_printf=printf"
cc_debug="-g -O0 -DBUILD_DEBUG=1 ${cc_common}"
cc_release="-g -O2 -DBUILD_DEBUG=0 ${cc_common}"
cc_link="-lpthread -lm -lrt -ldl"
if [[ "$host_os" == "Darwin" ]]; then
  cc_link="-lpthread -lm"
fi

# --- Per-Build Settings ------------------------------------------------------
cc_link_dll="-fPIC"

# --- External Libraries ------------------------------------------------------
# sudo apt install -y pkg-config libfreetype6-dev libx11-dev libxext-dev libxfixes-dev libxrandr-dev libgl-dev libegl-dev
if [[ "$host_os" == "Darwin" ]]; then
  cc_font_provider=""
  cc_os_gfx="-framework Cocoa"
  cc_render=""
elif [[ -x "$(command -v pkg-config)" ]]; then
  cc_font_provider="$(pkg-config --cflags --libs freetype2)"  
  cc_os_gfx="$(pkg-config --cflags --libs x11 xext xfixes xrandr)"
  cc_render="$(pkg-config --cflags --libs gl egl)"
else
  cc_font_provider="-I/usr/include/freetype2 -lfreetype"
  cc_os_gfx="-lX11 -lXext -lXfixes -lXrandr"
  cc_render="-lGL -lEGL"
fi

# --- Icons -------------------------------------------------------------------
cc_icon="-DLNX_WM_ICON=1"

# --- Choose Compile/Link Lines -----------------------------------------------
if   [[ "${gcc:-0}"   == "1" ]]; then compiler="${CC:-gcc}   $cc_cflags_gcc";   ar="${AR:-ar}";      echo "[gcc compile]";
elif [[ "${clang:-1}" == "1" ]]; then compiler="${CC:-clang} $cc_cflags_clang"; ar="${AR:-llvm-ar}"; echo "[clang compile]";
fi
if [[ "$host_os" == "Darwin" ]]; then ar="${AR:-ar}"; fi
if   [[ "${release:-0}" == "1" ]]; then echo "[release mode]"; compile="$compiler $cc_release";
elif [[ "${debug:-1}"   == "1" ]]; then echo "[debug mode]";   compile="$compiler $cc_debug";
fi

oodle_flags=()
blake3_libraries=()
if [[ "$target_arch" == "x86_64" ]]; then
  blake3_libraries=("blake3-$host_os-$target_arch/blake3.a")
fi
compile() {
  if [[ "$host_os" == "Darwin" ]]; then
    local task_source=$1
    shift
    $compile "${oodle_flags[@]}" -x objective-c "$task_source" -x none "$@" "${blake3_libraries[@]}"
  else
    $compile "${oodle_flags[@]}" -Wl,--start-group "$@" "${blake3_libraries[@]}" -latomic -Wl,--end-group
  fi
}

# --- Prep Directories --------------------------------------------------------
mkdir -p build local

# --- Build & Run Metaprogram -------------------------------------------------
if [[ "${meta:-0}" == "1" ]]
then
  echo "[building metagen]"
  cd build
  $compiler $cc_debug ../src/metagen/metagen_main.c $cc_link -o metagen
  ./metagen
  cd ..
fi

# --- Assemble BLAKE3 ---------------------------------------------------------
if [[ "$target_arch" == "x86_64" ]]
then
  blake3_dir="build/blake3-$host_os-$target_arch"
  mkdir -p "$blake3_dir"
  blake3_objects=()
  for variant in sse2 sse41 avx2 avx512; do
    source="src/third_party/blake3/blake3_${variant}_x86-64_unix.S"
    object="$blake3_dir/blake3_${variant}_x86-64_unix.o"
    if [[ ! -f "$object" || "$source" -nt "$object" ]]; then
      echo "[assembling blake3 $variant for $target_arch]"
      $compiler $cc_host_flags -c -g -o "$object" "$source"
    fi
    blake3_objects+=("$object")
  done
  $ar rs "$blake3_dir/blake3.a" "${blake3_objects[@]}"
fi

# --- Set up Oodle SDK -------------------------------------------------------
if [[ "${oodle:-0}" == "1" ]]; then
  oodle_sdk_path="${OODLE_SDK_DIR:-}"
  if [[ -z "$oodle_sdk_path" ]]; then
    for sdk in "$PWD"/local/oodle2*; do
      if [[ -f "$sdk/linux/include/oodle2.h" ]]; then
        if [[ -n "$oodle_sdk_path" ]]; then
          echo "Multiple Oodle SDKs found: $oodle_sdk_path and $sdk/linux" >&2
          echo "Set OODLE_SDK_DIR to select the SDK root." >&2
          exit 1
        fi
        oodle_sdk_path="$sdk/linux"
      fi
    done
  fi
  if [[ -z "$oodle_sdk_path" ]]; then
    echo "Oodle SDK not found. Put an oodle2* SDK with a linux subdirectory in $PWD/local or set OODLE_SDK_DIR." >&2
    exit 1
  fi
  if [[ -f "$oodle_sdk_path/linux/include/oodle2.h" ]]; then
    oodle_sdk_path="$oodle_sdk_path/linux"
  fi
  if [[ ! -f "$oodle_sdk_path/include/oodle2.h" ]]; then
    echo "Oodle SDK directory '$oodle_sdk_path' must contain include/oodle2.h" >&2
    exit 1
  fi
  oodle_sdk_path="$(cd "$oodle_sdk_path" && pwd)"
  echo "[Oodle SDK: $oodle_sdk_path]"
  # Keep the include path as one argument, including when it contains spaces.
  oodle_flags=(-DOODLE_SDK=1 "-I$oodle_sdk_path/include")
  for library in "$oodle_sdk_path"/lib/liboo2corelinux64.so "$oodle_sdk_path"/lib/liboo2corelinux64.so.*; do
    if [[ -f "$library" ]]; then
      cp -- "$library" "$PWD/build/"
    fi
  done
fi

# --- Build Everything (@build_targets) ---------------------------------------
cd build
if [[ "${raddbg:-0}"               == "1" ]]; then didbuild=1 && compile ../src/raddbg/raddbg_main.c $cc_icon $cc_link $cc_os_gfx $cc_render $cc_font_provider -o raddbg; fi
if [[ "${bundle:-0}"               == "1" ]]; then didbuild=1; if [[ "$host_os" != "Darwin" ]]; then echo "[ERROR] bundle target is only supported on Darwin."; exit 1; fi; compile ../src/raddbg/raddbg_main.c $cc_link $cc_os_gfx $cc_render $cc_font_provider -o raddbg; rm -rf "RAD Debugger.app"; mkdir -p "RAD Debugger.app/Contents/MacOS" "RAD Debugger.app/Contents/Resources"; cp ../src/mac/raddbg_Info.plist "RAD Debugger.app/Contents/Info.plist"; cp ../src/mac/raddbg.icns "RAD Debugger.app/Contents/Resources/raddbg.icns"; cp raddbg "RAD Debugger.app/Contents/MacOS/raddbg"; chmod +x "RAD Debugger.app/Contents/MacOS/raddbg"; fi
if [[ "${raddbg_non_graphical:-0}" == "1" ]]; then didbuild=1 && compile ../src/raddbg/raddbg_main.c -DWM_STUB=1 -DR_BACKEND=R_BACKEND_STUB $cc_link $cc_os_gfx $cc_render $cc_font_provider -o raddbg_non_graphical; fi
if [[ "${radbin:-0}"               == "1" ]]; then didbuild=1 && compile ../src/radbin/radbin_main.c   $cc_link -o radbin; fi
if [[ "${radlink:-0}"              == "1" ]]; then didbuild=1 && compile ../src/linker/lnk.c           $cc_link -o radlink; fi
if [[ "${torture:-0}"              == "1" ]]; then didbuild=1 && compile ../src/torture/torture_main.c $cc_link $cc_os_gfx $cc_render $cc_font_provider -o torture; fi
cd ..

# --- Warn On No Builds -------------------------------------------------------
if [[ "${didbuild:-0}" == "0" ]]
then
  echo "[WARNING] no valid build target specified; must use build target names as arguments to this script, like \`./build.sh raddbg\` or \`./build.sh radlink\`."
  exit 1
fi

# --- Warn On Debug Builds (if debug not explicitly specified) ----------------
if [[ ! -v debug && ! -v release ]] then
  echo "[INFO] Debug build complete. For a faster build, call this script with the \`release\` argument (this will take significantly longer than a debug build)."
fi
