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

# Stapling fetches the ticket from Apple: retried like the submission.
staple() {
  for attempt in 1 2 3; do
    xcrun stapler staple "$1" && return
    sleep 30
  done
  echo "Couldn't staple $1" >&2
  return 1
}

notarize() (  # a subshell: its EXIT trap removes the staging however it ends
  if [[ -z "${JM_NOTARY_KEY:-}" || -z "$identity" ]]; then
    echo "Not notarized (needs JM_SIGN_IDENTITY and JM_NOTARY_KEY)"
    return
  fi
  local staging zip result id
  staging="$(mktemp -d)"
  trap 'rm -rf "$staging"' EXIT
  zip="$staging/notarize.zip"
  if [[ $# == 1 && "$1" == *.pkg ]]; then
    zip="$1"
  else
    mkdir "$staging/files"
    for path in "$@"; do ditto "$path" "$staging/files/$(basename "$path")"; done
    ditto -c -k --keepParent "$staging/files" "$zip"
  fi
  local key=(--key "$JM_NOTARY_KEY" --key-id "$JM_NOTARY_KEY_ID" --issuer "$JM_NOTARY_ISSUER")
  for attempt in 1 2 3; do  # the upload itself can hit a network blip
    result="$(xcrun notarytool submit "$zip" "${key[@]}" --output-format json)" && break
    [[ $attempt == 3 ]] && { echo "Couldn't submit for notarization" >&2; exit 1; }
    sleep 30
  done
  echo "$result"
  id="$(sed -n 's/.*"id" *: *"\([^"]*\)".*/\1/p' <<<"$result" | head -1)"
  # Apple can take an hour; ask in short rounds so a dropped connection only costs one round.
  local status="In Progress"
  for _ in $(seq 1 240); do
    result="$(xcrun notarytool info "$id" "${key[@]}" --output-format json 2>/dev/null)" &&
      status="$(sed -n 's/.*"status" *: *"\([^"]*\)".*/\1/p' <<<"$result" | head -1)"
    [[ "$status" != "In Progress" ]] && break
    sleep 30
  done
  echo "Notarization $id: $status"
  if [[ "$status" != "Accepted" ]]; then
    xcrun notarytool log "$id" "${key[@]}" || true
    echo "Notarization failed" >&2
    exit 1
  fi
  for path in "$@"; do
    case "$path" in  # a bare binary can't hold a ticket: Gatekeeper looks it up online
      *.app | *.pkg) staple "$path" ;;
    esac
    case "$path" in
      *.app) spctl --assess --type execute -vv "$path" ;;
      *.pkg) spctl --assess --type install -vv "$path" ;;
    esac
  done
)

command="$1"
shift
case "$command" in
  sign) sign "$@" ;;
  notarize) notarize "$@" ;;
  *) echo "usage: $0 sign|notarize <file|.app>..." >&2; exit 2 ;;
esac
