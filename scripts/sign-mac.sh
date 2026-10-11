#!/usr/bin/env bash
# Signs macOS release files and notarizes them, with what the environment holds:
#   JM_SIGN_IDENTITY   a Developer ID Application identity in the keychain;
#                      unset: ad-hoc signed (runs, but Gatekeeper warns on download)
#   JM_NOTARY_KEY, JM_NOTARY_KEY_ID, JM_NOTARY_ISSUER
#                      an App Store Connect API key (.p8 file, its id, the issuer)
#
#   scripts/sign-mac.sh sign <file|.app>...      an .app: its executables, then the bundle
#   scripts/sign-mac.sh notarize <file|.app|.pkg>...  one submission for all; staples the .apps
#                      (a .pkg goes alone: notarytool takes it as it is)
set -euo pipefail

identity="${JM_SIGN_IDENTITY:-}"

sign_one() {
  if [[ -n "$identity" ]]; then
    codesign --force --options runtime --timestamp --sign "$identity" "$1"
  else
    codesign --force --sign - "$1"
  fi
}

sign() {
  for path in "$@"; do
    if [[ "$path" == *.app ]]; then
      for bin in "$path/Contents/MacOS"/*; do sign_one "$bin"; done
      sign_one "$path"
    else
      sign_one "$path"
    fi
  done
}

notarize() {
  if [[ -z "${JM_NOTARY_KEY:-}" || -z "$identity" ]]; then
    echo "Not notarized (needs JM_SIGN_IDENTITY and JM_NOTARY_KEY)"
    return
  fi
  local staging zip result id
  staging="$(mktemp -d)"
  zip="$staging/notarize.zip"
  if [[ $# == 1 && "$1" == *.pkg ]]; then
    zip="$1"
  else
    mkdir "$staging/files"
    for path in "$@"; do ditto "$path" "$staging/files/$(basename "$path")"; done
    ditto -c -k --keepParent "$staging/files" "$zip"
  fi
  result="$(xcrun notarytool submit "$zip" --key "$JM_NOTARY_KEY" --key-id "$JM_NOTARY_KEY_ID" \
    --issuer "$JM_NOTARY_ISSUER" --wait --output-format json)"
  echo "$result"
  if ! grep -q '"status" *: *"Accepted"' <<<"$result"; then
    id="$(sed -n 's/.*"id" *: *"\([^"]*\)".*/\1/p' <<<"$result" | head -1)"
    [[ -n "$id" ]] && xcrun notarytool log "$id" --key "$JM_NOTARY_KEY" --key-id "$JM_NOTARY_KEY_ID" --issuer "$JM_NOTARY_ISSUER"
    echo "Notarization failed" >&2
    exit 1
  fi
  for path in "$@"; do
    case "$path" in  # a bare binary can't hold a ticket: Gatekeeper looks it up online
      *.app) xcrun stapler staple "$path"; spctl --assess --type execute -vv "$path" ;;
      *.pkg) xcrun stapler staple "$path"; spctl --assess --type install -vv "$path" ;;
    esac
  done
  rm -rf "$staging"
}

command="$1"
shift
case "$command" in
  sign) sign "$@" ;;
  notarize) notarize "$@" ;;
  *) echo "usage: $0 sign|notarize <file|.app>..." >&2; exit 2 ;;
esac
