#!/bin/bash
set -eu
cd "$(dirname "$0")"

# --- Unpack Arguments --------------------------------------------------------
for arg in "$@"; do declare $arg='1'; done
if [ -z "${gcc+x}" ];     then clang=1; fi
if [ -z "${release+x}" ]; then debug=1; fi
if [ -n "${debug+x}" ];   then echo "[debug mode]"; fi
if [ -n "${release+x}" ]; then echo "[release mode]"; fi
if [ -n "${clang+x}" ];   then compiler="${CC:-clang}"; echo "[clang compile]"; fi
if [ -n "${gcc+x}" ];     then compiler="${CC:-gcc}"; echo "[gcc compile]"; fi

# --- Unpack Command Line Build Arguments -------------------------------------
auto_compile_flags=''

# --- Get Current Git Commit Id -----------------------------------------------
git_hash=$(git describe --always --dirty)
git_hash_full=$(git rev-parse HEAD)

# --- Compile/Link Line Definitions -------------------------------------------
host_os=$(uname -s)
clang_common="-I../src/ -I/usr/include/freetype2/ -I../local/ -D_GNU_SOURCE -g -DBUILD_GIT_HASH=\"$git_hash\" -DBUILD_GIT_HASH_FULL=\"$git_hash_full\" -Wno-unknown-warning-option -fdiagnostics-absolute-paths -Wall -Wno-missing-braces -Wno-unused-function -Wno-writable-strings -Wno-unused-value -Wno-unused-variable -Wno-unused-local-typedef -Wno-deprecated-register -Wno-deprecated-declarations -Wno-unused-but-set-variable -Wno-single-bit-bitfield-constant-conversion -Wno-compare-distinct-pointer-types -Wno-initializer-overrides -Wno-incompatible-pointer-types-discards-qualifiers -Wno-for-loop-analysis -Xclang -flto-visibility-public-std -D_USE_MATH_DEFINES -Dstrdup=_strdup -Dgnu_printf=printf"
if [ "$host_os" = "Darwin" ]; then
  clang_common="$clang_common -x objective-c"
fi
clang_debug="$compiler -g -O0 -DBUILD_DEBUG=1 ${clang_common} ${auto_compile_flags}"
clang_release="$compiler -g -O2 -DBUILD_DEBUG=0 ${clang_common} ${auto_compile_flags}"
clang_link="-lpthread -lm -lrt -ldl"
clang_out="-o"
gcc_common="-I../src/ -I../local/ -g -D_GNU_SOURCE -DBUILD_GIT_HASH=\"$git_hash\" -DBUILD_GIT_HASH_FULL=\"$git_hash_full\" -Wno-unknown-warning-option -Wall -Wno-missing-braces -Wno-unused-function -Wno-attributes -Wno-unused-value -Wno-unused-variable -Wno-unused-local-typedef -Wno-deprecated-declarations -Wno-unused-but-set-variable -Wno-compare-distinct-pointer-types -D_USE_MATH_DEFINES -Dstrdup=_strdup -Dgnu_printf=printf"
gcc_debug="$compiler -g -O0 -DBUILD_DEBUG=1 ${gcc_common} ${auto_compile_flags}"
gcc_release="$compiler -g -O2 -DBUILD_DEBUG=0 ${gcc_common} ${auto_compile_flags}"
gcc_link="-lpthread -lm -lrt -ldl"
gcc_out="-o"

if [ "$host_os" = "Darwin" ]; then
  clang_link="-lpthread -lm"
  gcc_link="-lpthread -lm"
fi

# --- Per-Build Settings ------------------------------------------------------
link_dll="-fPIC"
link_os_gfx="-lX11 -lXext"
link_render="-lGL -lEGL"
link_font_provider="-lfreetype"

if [ "$host_os" = "Darwin" ]; then
  link_os_gfx="-framework Cocoa"
  link_render="-framework Metal -framework QuartzCore"
  link_font_provider="-framework CoreText -framework CoreGraphics -framework CoreFoundation"
fi

# --- Choose Compile/Link Lines -----------------------------------------------
if [ -n "${gcc+x}" ];     then compile_debug="$gcc_debug"; fi
if [ -n "${gcc+x}" ];     then compile_release="$gcc_release"; fi
if [ -n "${gcc+x}" ];     then compile_link="$gcc_link"; fi
if [ -n "${gcc+x}" ];     then out="$gcc_out"; fi
if [ -n "${clang+x}" ];   then compile_debug="$clang_debug"; fi
if [ -n "${clang+x}" ];   then compile_release="$clang_release"; fi
if [ -n "${clang+x}" ];   then compile_link="$clang_link"; fi
if [ -n "${clang+x}" ];   then out="$clang_out"; fi
if [ -n "${debug+x}" ];   then compile="$compile_debug"; fi
if [ -n "${release+x}" ]; then compile="$compile_release"; fi

# --- Prep Directories --------------------------------------------------------
mkdir -p build
mkdir -p local

# --- Build & Run Metaprogram -------------------------------------------------
if [ -n "${meta+x}" ]
then
  echo "[doing metagen]"
  cd build
  $compile_debug ../src/metagen/metagen_main.c $compile_link $out metagen
  ./metagen
  cd ..
fi

# --- Build Everything (@build_targets) ---------------------------------------
cd build
if [ -n "${raddbg+x}" ];              then didbuild=1 && $compile ../src/raddbg/raddbg_main.c                                    $compile_link $link_os_gfx $link_render $link_font_provider $out raddbg; fi
if [ -n "${bundle+x}" ];              then didbuild=1; if [ "$host_os" != "Darwin" ]; then echo "[ERROR] bundle target is only supported on Darwin."; exit 1; fi; $compile ../src/raddbg/raddbg_main.c $compile_link $link_os_gfx $link_render $link_font_provider $out raddbg; rm -rf "RAD Debugger.app"; mkdir -p "RAD Debugger.app/Contents/MacOS" "RAD Debugger.app/Contents/Resources"; cp ../src/mac/raddbg_Info.plist "RAD Debugger.app/Contents/Info.plist"; cp ../src/mac/raddbg.icns "RAD Debugger.app/Contents/Resources/raddbg.icns"; cp raddbg "RAD Debugger.app/Contents/MacOS/raddbg"; chmod +x "RAD Debugger.app/Contents/MacOS/raddbg"; fi
if [ -n "${radbin+x}" ];              then didbuild=1 && $compile ../src/radbin/radbin_main.c                                    $compile_link $out radbin; fi
if [ -n "${radlink+x}" ];             then didbuild=1 && $compile ../src/linker/lnk.c                                            $compile_link $out radlink; fi
cd ..

# --- Warn On No Builds -------------------------------------------------------
if [ -z "${didbuild+x}" ]
then
  echo "[WARNING] no valid build target specified; must use build target names as arguments to this script, like \`./build.sh raddbg\` or \`./build.sh rdi_from_pdb\`."
  exit 1
fi
