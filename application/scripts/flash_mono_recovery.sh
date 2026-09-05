#!/usr/bin/env bash
# Recover Slim with a monolithic image at 0x0 (no MCUboot).
# IMPORTANT: Zephyr mono .hex only programs 64 KB — use .bin or AH01 full hex after chiperase.
set -euo pipefail

if [[ "${AIRSPOT_HARDWARE:-rev3a}" = rev3a ]]; then
  rev3a_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
  exec bash "$rev3a_root/scripts/airspot-rev3a" flash "$@"
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
APP_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
WS_DIR="$(cd "$APP_DIR/.." && pwd)"

IMAGE="${1:-}"

pick_default_image() {
  local vhex vbin ah01
  vhex="$APP_DIR/build/zephyr/airspot_slim_v0.11.0.hex"
  vbin="$APP_DIR/build/zephyr/airspot_slim_v0.11.0.bin"
  ah01="$WS_DIR/AH01 Working UART 15012026.hex"

  if [[ -f "$vbin" ]]; then
    echo "$vbin"
    return
  fi
  if [[ -f "$vhex" ]]; then
    echo "$vhex"
    return
  fi
  if [[ -f "$ah01" ]]; then
    echo "$ah01"
    return
  fi
  echo "Pass .bin or full mono .hex, or run: airspot build --clean && airspot flash" >&2
  exit 1
}

if [[ -z "$IMAGE" ]]; then
  IMAGE="$(pick_default_image)"
fi

if [[ ! -f "$IMAGE" ]]; then
  echo "Not found: $IMAGE" >&2
  exit 1
fi

# If user passed mono .hex but build .bin exists and hex is truncated, switch to .bin.
if [[ "$IMAGE" == *.hex ]]; then
  paired="${IMAGE%.hex}.bin"
  if [[ -f "$paired" ]]; then
    hex_end=$(python3 - "$IMAGE" <<'PY'
import sys
path = sys.argv[1]
ext = end = 0
for line in open(path):
    if not line.startswith(":"):
        continue
    c = int(line[1:3], 16)
    a = int(line[3:7], 16)
    t = int(line[7:9], 16)
    if t == 4:
        ext = int(line[9:13], 16) << 16
    elif t == 0:
        end = max(end, ext + a + c)
print(end)
PY
)
    bin_sz=$(wc -c < "$paired" | tr -d ' ')
    if [[ "$bin_sz" -gt 65536 && "$hex_end" -le 65536 ]]; then
      echo "Mono .hex truncated ($hex_end bytes mapped) — using .bin ($bin_sz bytes)" >&2
      IMAGE="$paired"
    fi
  fi
fi

if ! command -v nrfjprog >/dev/null 2>&1; then
  echo "nrfjprog not found." >&2
  exit 1
fi

echo "Recovering with monolithic image (no MCUboot): $IMAGE"
nrfjprog --recover -f NRF54L
if [[ "$IMAGE" == *.bin ]]; then
  nrfjprog --program "$IMAGE" --chiperase --verify -f NRF54L
else
  nrfjprog --program "$IMAGE" --chiperase --verify -f NRF54L
fi
nrfjprog --reset -f NRF54L
echo "Done. UART logs: P1.02 @ 115200"
