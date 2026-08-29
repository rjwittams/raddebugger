#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"

usage()
{
  cat <<'EOF'
usage: ./notarize_macos_release.sh [path-to.dmg]

Submits the signed RAD Debugger disk image to Apple's notary service, waits
for acceptance, staples the resulting ticket, and validates the exact disk
image intended for distribution.

If no path is given, the script selects the single versioned RAD Debugger DMG
under build/release.

Environment:
  RADDBG_NOTARYTOOL_PROFILE  Keychain profile created by:
                              xcrun notarytool store-credentials PROFILE
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

if [[ "${1:-}" == "--help" || "${1:-}" == "-h" ]]; then
  usage
  exit 0
elif (( $# > 1 )); then
  usage >&2
  exit 1
fi

if (( BASH_VERSINFO[0] < 4 || (BASH_VERSINFO[0] == 4 && BASH_VERSINFO[1] < 2) )); then
  fail "notarize_macos_release.sh requires Bash 4.2 or newer"
fi

[[ "$(uname -s)" == "Darwin" ]] || fail "notarization requires Darwin"
for command_name in codesign git grep hdiutil spctl xcrun; do
  require_command "$command_name"
done

repo_root=$(git rev-parse --show-toplevel)
[[ "$repo_root" == "$PWD" ]] || fail "run this script from its RAD Debugger repository"

notary_profile=${RADDBG_NOTARYTOOL_PROFILE:-}
[[ -n "$notary_profile" ]] || fail "set RADDBG_NOTARYTOOL_PROFILE to a notarytool Keychain profile"

if [[ $# == 1 ]]; then
  dmg=$1
  if [[ "$dmg" != /* ]]; then
    dmg="$repo_root/$dmg"
  fi
else
  shopt -s nullglob
  release_dmgs=("$repo_root"/build/release/RAD-Debugger-*-macOS-universal.dmg)
  shopt -u nullglob
  (( ${#release_dmgs[@]} == 1 )) || fail "expected exactly one versioned release DMG; pass its path explicitly"
  dmg=${release_dmgs[0]}
fi

[[ -f "$dmg" ]] || fail "disk image not found: $dmg"
[[ "$dmg" == *.dmg ]] || fail "notarization input must be a disk image: $dmg"

hdiutil verify "$dmg"
codesign --verify --strict --verbose=2 "$dmg"
if codesign -dvv "$dmg" 2>&1 | grep -q '^Signature=adhoc$'; then
  fail "refusing to submit an ad-hoc-signed disk image"
fi

echo "[notarize] submitting: $dmg"
xcrun notarytool submit "$dmg" --keychain-profile "$notary_profile" --wait
xcrun stapler staple "$dmg"
xcrun stapler validate "$dmg"
hdiutil verify "$dmg"
codesign --verify --strict --verbose=2 "$dmg"
spctl --assess --type open --context context:primary-signature --verbose=2 "$dmg"

echo "[notarize] accepted and stapled: $dmg"
