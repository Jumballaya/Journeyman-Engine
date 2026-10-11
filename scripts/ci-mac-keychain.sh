#!/usr/bin/env bash
# CI on a Mac: puts the Developer ID certificate in a temporary keychain and the
# notary key in a file, then tells later steps (via $GITHUB_ENV) what sign-mac.sh needs.
# Reads secrets from the environment: MACOS_CERT_P12 (base64), MACOS_CERT_PASSWORD,
# MACOS_INSTALLER_P12 (base64), MACOS_INSTALLER_PASSWORD (the .pkg's identity),
# APPLE_API_KEY_P8 (base64), APPLE_API_KEY_ID, APPLE_API_ISSUER_ID.
# Without them (a fork, a PR) it does nothing, and builds are ad-hoc signed.
set -euo pipefail

if [[ -z "${MACOS_CERT_P12:-}" ]]; then
  echo "No signing secrets: release files will be ad-hoc signed"
  exit 0
fi
secrets="${RUNNER_TEMP:?}/signing"
mkdir -p "$secrets"
keychain="$secrets/signing.keychain-db"
password="$(uuidgen)"
security create-keychain -p "$password" "$keychain"
security set-keychain-settings -lut 21600 "$keychain"
security unlock-keychain -p "$password" "$keychain"
base64 --decode <<<"$MACOS_CERT_P12" >"$secrets/cert.p12"
security import "$secrets/cert.p12" -k "$keychain" -P "$MACOS_CERT_PASSWORD" -T /usr/bin/codesign
rm "$secrets/cert.p12"
if [[ -n "${MACOS_INSTALLER_P12:-}" ]]; then
  base64 --decode <<<"$MACOS_INSTALLER_P12" >"$secrets/installer.p12"
  security import "$secrets/installer.p12" -k "$keychain" -P "${MACOS_INSTALLER_PASSWORD:?}" -T /usr/bin/productbuild -T /usr/bin/productsign
  rm "$secrets/installer.p12"
fi
# The certificate's issuer, so codesign can build the chain to Apple's root.
curl -fsSL https://www.apple.com/certificateauthority/DeveloperIDG2CA.cer -o "$secrets/g2.cer"
security import "$secrets/g2.cer" -k "$keychain"
security set-key-partition-list -S apple-tool:,apple: -s -k "$password" "$keychain" >/dev/null
read -ra others <<<"$(security list-keychains -d user | tr -d '"')"
security list-keychains -d user -s "$keychain" "${others[@]}"

identity="$(security find-identity -v -p codesigning "$keychain" | sed -n 's/.*"\(Developer ID Application: .*\)"$/\1/p' | head -1)"
[[ -n "$identity" ]] || { echo "MACOS_CERT_P12 holds no Developer ID Application identity" >&2; exit 1; }
echo "Signing as $identity"
echo "JM_SIGN_IDENTITY=$identity" >>"$GITHUB_ENV"
installer="$(security find-identity -v "$keychain" | sed -n 's/.*"\(Developer ID Installer: .*\)"$/\1/p' | head -1)"
[[ -n "$installer" ]] && echo "JM_INSTALLER_IDENTITY=$installer" >>"$GITHUB_ENV"

if [[ -n "${APPLE_API_KEY_P8:-}" ]]; then
  base64 --decode <<<"$APPLE_API_KEY_P8" >"$secrets/notary.p8"
  {
    echo "JM_NOTARY_KEY=$secrets/notary.p8"
    echo "JM_NOTARY_KEY_ID=${APPLE_API_KEY_ID:?}"
    echo "JM_NOTARY_ISSUER=${APPLE_API_ISSUER_ID:?}"
  } >>"$GITHUB_ENV"
fi
