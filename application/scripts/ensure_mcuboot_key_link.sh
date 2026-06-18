#!/usr/bin/env bash
# Sysbuild passes SB_CONFIG_BOOT_SIGNATURE_KEY_FILE=application/keys/... into
# MCUboot. imgtool resolves that under the MCUboot repo as:
#   external/bootloader/mcuboot/application/keys/
# The real key lives at application/keys/. This symlink makes both paths work.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
APP_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
WS_DIR="$(cd "$APP_DIR/.." && pwd)"

KEYS_SRC="$APP_DIR/keys"
LINK_PARENT="$WS_DIR/external/bootloader/mcuboot/application"
LINK_PATH="$LINK_PARENT/keys"

if [[ ! -f "$KEYS_SRC/airspot-signing-key-ecdsa.pem" ]]; then
  echo "Missing signing key: $KEYS_SRC/airspot-signing-key-ecdsa.pem" >&2
  echo "Generate or copy the ECDSA key before building with --sysbuild." >&2
  exit 1
fi

if [[ ! -d "$WS_DIR/external/bootloader/mcuboot" ]]; then
  echo "MCUboot not found. Run: west update" >&2
  exit 1
fi

mkdir -p "$LINK_PARENT"
if [[ -L "$LINK_PATH" ]] || [[ -e "$LINK_PATH" ]]; then
  rm -rf "$LINK_PATH"
fi
ln -sf "../../../../application/keys" "$LINK_PATH"
