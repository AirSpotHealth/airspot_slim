#!/usr/bin/env bash
# Recover a Slim that won't boot or show logs after a bad OTA.
# Requires: nRF Command Line Tools (nrfjprog) and J-Link to the Slim debug port.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
APP_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
RELEASES_DIR="$(cd "$SCRIPT_DIR/../../releases" && pwd)"

pick_hex() {
  local build_merged="$APP_DIR/build/merged.hex"
  if [[ -f "$build_merged" ]]; then
    echo "$build_merged"
    return
  fi
  local candidate
  candidate="$(find "$RELEASES_DIR" -name '*_merged.hex' -type f 2>/dev/null | sort | tail -1)"
  if [[ -z "$candidate" ]]; then
    echo "No merged.hex in build/ or releases/" >&2
    exit 1
  fi
  echo "$candidate"
}

HEX="${1:-$(pick_hex)}"

if ! command -v nrfjprog >/dev/null 2>&1; then
  echo "nrfjprog not found. Install nRF Command Line Tools from Nordic." >&2
  exit 1
fi

if ! python3 - "$HEX" <<'PY'
import sys
path = sys.argv[1]
ext = 0
for line in open(path):
    line = line.strip()
    if not line.startswith(":"):
        continue
    count = int(line[1:3], 16)
    addr = int(line[3:7], 16)
    typ = int(line[7:9], 16)
    if typ == 4:
        ext = int(line[9:9 + 4], 16) << 16
    elif typ == 0 and ext + addr + count > 0xC000:
        sys.exit(0)
print(f"ERROR: {path} is not a merged image (no data at 0xC000+).", file=sys.stderr)
print("Do NOT use legacy airspot_slim_*.hex (64KB @ 0x0) after MCUboot — use *_merged.hex", file=sys.stderr)
sys.exit(1)
PY
then
  exit 1
fi

echo "Recovering AirSpot Slim with: $HEX"
echo "Full internal erase + MCUboot @ 0x0 + signed app @ 0xC000."
nrfjprog --recover -f NRF54L
nrfjprog --program "$HEX" --chiperase --verify -f NRF54L
nrfjprog --reset -f NRF54L
echo "Done."
echo "  Logs: UART P1.02 TX @ 115200 — airspot logs uart"
echo "  (USB-serial on Slim debug header, not the nRF DK USB port)"
echo "  Expect LED blinks on P2.09 / P2.08 / P2.06 at boot."
