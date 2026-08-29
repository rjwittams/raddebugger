#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"

usage()
{
  cat <<'EOF'
usage: ./build_macos_release.sh

Builds both arm64 and x86_64 slices on one Mac, combines them into a
Universal 2 RAD Debugger app, and packages it in a signed disk image.
An accompanying universal dSYM archive is retained for crash diagnosis.

Environment:
  RADDBG_RELEASE_CODESIGN_IDENTITY  Developer ID Application identity.
  RADDBG_RELEASE_ADHOC=1            Permit an ad-hoc test build instead.
  RADDBG_MACOS_DEPLOYMENT_TARGET    Minimum macOS version (default: 14.0).
  RADDBG_RELEASE_BUNDLE_IDENTIFIER  Bundle ID (default: org.changedirection.raddbg).
  RADDBG_RELEASE_ENTITLEMENTS       Release entitlement plist override.

The script builds but does not submit the resulting disk image for notarization.
Run ./notarize_macos_release.sh after inspecting the signed disk image.
EOF
}

fail()
{
  echo "[ERROR] $*" >&2
  exit 1
}

require_command()
{
  command -v "$1" >/dev/null 2>&1 || fail "required command not found: $1"
}

require_tracked_files_clean()
{
  if ! git diff --quiet || ! git diff --cached --quiet; then
    git status --short >&2
    fail "$1"
  fi
}

if [[ "${1:-}" == "--help" || "${1:-}" == "-h" ]]; then
  usage
  exit 0
elif [[ "$#" != "0" ]]; then
  usage >&2
  exit 1
fi

if (( BASH_VERSINFO[0] < 4 || (BASH_VERSINFO[0] == 4 && BASH_VERSINFO[1] < 2) )); then
  fail "build_macos_release.sh requires Bash 4.2 or newer"
fi

[[ "$(uname -s)" == "Darwin" ]] || fail "the macOS release build requires Darwin"

for command_name in clang codesign ditto dwarfdump git hdiutil lipo plutil; do
  require_command "$command_name"
done
[[ -x /usr/libexec/PlistBuddy ]] || fail "required command not found: /usr/libexec/PlistBuddy"

repo_root=$(git rev-parse --show-toplevel)
[[ "$repo_root" == "$PWD" ]] || fail "run this script from its RAD Debugger repository"

deployment_target=${RADDBG_MACOS_DEPLOYMENT_TARGET:-14.0}
[[ "$deployment_target" =~ ^[0-9]+\.[0-9]+(\.[0-9]+)?$ ]] || fail "invalid deployment target: $deployment_target"
bundle_identifier=${RADDBG_RELEASE_BUNDLE_IDENTIFIER:-org.changedirection.raddbg}
[[ "$bundle_identifier" =~ ^[A-Za-z0-9-]+(\.[A-Za-z0-9-]+)+$ ]] || fail "invalid bundle identifier: $bundle_identifier"

release_identity=${RADDBG_RELEASE_CODESIGN_IDENTITY:-}
allow_adhoc=${RADDBG_RELEASE_ADHOC:-0}
[[ "$allow_adhoc" == "0" || "$allow_adhoc" == "1" ]] || fail "RADDBG_RELEASE_ADHOC must be 0 or 1"
if [[ -z "$release_identity" ]]; then
  [[ "$allow_adhoc" == "1" ]] || fail "set RADDBG_RELEASE_CODESIGN_IDENTITY, or RADDBG_RELEASE_ADHOC=1 for a non-distributable test build"
  release_identity="-"
elif [[ "$release_identity" == "-" && "$allow_adhoc" != "1" ]]; then
  fail "RADDBG_RELEASE_CODESIGN_IDENTITY=- requires RADDBG_RELEASE_ADHOC=1"
fi

release_entitlements=${RADDBG_RELEASE_ENTITLEMENTS:-src/mac/raddbg_release.entitlements}
if [[ "$release_entitlements" != /* ]]; then
  release_entitlements="$repo_root/$release_entitlements"
fi
[[ -f "$release_entitlements" ]] || fail "release entitlements not found: $release_entitlements"
if /usr/libexec/PlistBuddy -c 'Print :com.apple.security.get-task-allow' "$release_entitlements" >/dev/null 2>&1; then
  fail "release entitlements must not contain com.apple.security.get-task-allow"
fi

require_tracked_files_clean "release builds require a clean tracked worktree"
release_commit=$(git rev-parse HEAD)
release_version=$(git describe --always)

case "$(uname -m)" in
  arm64|aarch64) host_arch=arm64 ;;
  x86_64|amd64) host_arch=x86_64 ;;
  *) fail "unsupported build host architecture: $(uname -m)" ;;
esac
if [[ "$host_arch" == "arm64" ]]; then
  other_arch=x86_64
else
  other_arch=arm64
fi

release_root="$repo_root/build/release"
stage_root="$release_root/stage"
app="$release_root/RAD Debugger.app"
app_executable="$app/Contents/MacOS/raddbg"
universal_dsym="$release_root/raddbg.dSYM"
dmg_root="$release_root/dmg-root"

[[ "$release_root" == "$repo_root/build/release" ]] || fail "refusing unexpected release output path: $release_root"
rm -rf "$release_root"
mkdir -p "$stage_root"

build_slice()
{
  local arch=$1
  local run_metagen=$2
  local target="${arch}-apple-macos${deployment_target}"
  local -a build_args=(raddbg release)
  if [[ "$run_metagen" != "1" ]]; then
    build_args+=(no_meta)
  fi

  echo "[release] building $arch for macOS $deployment_target"
  rm -f "$repo_root/build/raddbg"
  rm -rf "$repo_root/build/raddbg.dSYM"
  RADDBG_TARGET_ARCH="$arch" \
  RADDBG_CODESIGN_IDENTITY="-" \
  RADDBG_CODESIGN_ENTITLEMENTS="$release_entitlements" \
  CC="clang -target $target -Wunguarded-availability -Werror=unguarded-availability" \
    "$BASH" ./build.sh "${build_args[@]}"

  require_tracked_files_clean "metagen or compilation changed tracked files; refusing mismatched release slices"
  [[ "$(git rev-parse HEAD)" == "$release_commit" ]] || fail "HEAD changed during the release build"
  [[ -x "$repo_root/build/raddbg" ]] || fail "$arch executable was not produced"
  [[ -d "$repo_root/build/raddbg.dSYM" ]] || fail "$arch dSYM was not produced"
  lipo "$repo_root/build/raddbg" -verify_arch "$arch"
  lipo "$repo_root/build/raddbg.dSYM/Contents/Resources/DWARF/raddbg" -verify_arch "$arch"

  local arch_stage="$stage_root/$arch"
  mkdir -p "$arch_stage"
  install -m 755 "$repo_root/build/raddbg" "$arch_stage/raddbg"
  ditto "$repo_root/build/raddbg.dSYM" "$arch_stage/raddbg.dSYM"
}

# The first slice matches the host so its metagen executable can run natively.
build_slice "$host_arch" 1
build_slice "$other_arch" 0

mkdir -p "$app/Contents/MacOS" "$app/Contents/Resources"
cp src/mac/raddbg_Info.plist "$app/Contents/Info.plist"
cp src/mac/raddbg.icns "$app/Contents/Resources/raddbg.icns"
/usr/libexec/PlistBuddy -c "Set :CFBundleIdentifier $bundle_identifier" "$app/Contents/Info.plist"
/usr/libexec/PlistBuddy -c "Add :LSMinimumSystemVersion string $deployment_target" "$app/Contents/Info.plist"
lipo -create \
  "$stage_root/arm64/raddbg" \
  "$stage_root/x86_64/raddbg" \
  -output "$app_executable"
chmod +x "$app_executable"

# Preserve both relocation maps, then replace the thin DWARF image with a
# universal image containing UUIDs matching both executable slices.
ditto "$stage_root/arm64/raddbg.dSYM" "$universal_dsym"
ditto "$stage_root/x86_64/raddbg.dSYM/Contents/Resources/Relocations" \
      "$universal_dsym/Contents/Resources/Relocations"
lipo -create \
  "$stage_root/arm64/raddbg.dSYM/Contents/Resources/DWARF/raddbg" \
  "$stage_root/x86_64/raddbg.dSYM/Contents/Resources/DWARF/raddbg" \
  -output "$universal_dsym/Contents/Resources/DWARF/raddbg"

codesign_args=(--force --options runtime --sign "$release_identity" --entitlements "$release_entitlements")
if [[ "$release_identity" != "-" ]]; then
  codesign_args+=(--timestamp)
fi
codesign "${codesign_args[@]}" "$app"

lipo "$app_executable" -verify_arch arm64 x86_64
lipo "$universal_dsym/Contents/Resources/DWARF/raddbg" -verify_arch arm64 x86_64
codesign --verify --strict --verbose=2 "$app"

app_uuids=$(dwarfdump --uuid "$app_executable")
dsym_uuids=$(dwarfdump --uuid "$universal_dsym")
for arch in arm64 x86_64; do
  app_uuid=$(printf '%s\n' "$app_uuids" | awk -v arch="$arch" '$0 ~ "\\(" arch "\\)" {print $2}')
  dsym_uuid=$(printf '%s\n' "$dsym_uuids" | awk -v arch="$arch" '$0 ~ "\\(" arch "\\)" {print $2}')
  [[ -n "$app_uuid" && "$app_uuid" == "$dsym_uuid" ]] || fail "$arch executable and dSYM UUIDs do not match"
done

release_product_version=$(/usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' "$app/Contents/Info.plist")
[[ "$release_product_version" =~ ^[0-9]+(\.[0-9]+)*$ ]] || fail "invalid app product version: $release_product_version"
dmg="$release_root/RAD-Debugger-${release_product_version}-macOS-universal.dmg"
dsym_archive="$release_root/RAD-Debugger-macOS-universal-dSYM.zip"

mkdir -p "$dmg_root"
ditto "$app" "$dmg_root/RAD Debugger.app"
ln -s /Applications "$dmg_root/Applications"
hdiutil create \
  -volname "RAD Debugger" \
  -srcfolder "$dmg_root" \
  -format UDZO \
  -ov \
  "$dmg"

dmg_codesign_args=(--force --sign "$release_identity")
if [[ "$release_identity" != "-" ]]; then
  dmg_codesign_args+=(--timestamp)
fi
codesign "${dmg_codesign_args[@]}" "$dmg"
hdiutil verify "$dmg"
codesign --verify --strict --verbose=2 "$dmg"

ditto -c -k --keepParent "$universal_dsym" "$dsym_archive"

require_tracked_files_clean "release build changed tracked files"

echo "[release] commit: $release_commit ($release_version)"
echo "[release] bundle identifier: $bundle_identifier"
echo "[release] app: $app"
echo "[release] dSYM: $universal_dsym"
echo "[release] disk image: $dmg"
echo "[release] support symbols: $dsym_archive"
if [[ "$release_identity" == "-" ]]; then
  echo "[release] signature: ad hoc (not distributable or notarization-ready)"
else
  echo "[release] signature: $release_identity"
  echo "[release] notarization has not been submitted; run ./notarize_macos_release.sh"
fi
