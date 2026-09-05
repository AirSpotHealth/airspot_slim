#!/usr/bin/env bash
# Recovery uses the same checked, settings-preserving production path.
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
exec bash "$root/scripts/airspot" flash "$@"
