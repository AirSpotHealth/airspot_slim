#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
args=()
flash=false
for arg in "$@"; do
    case "$arg" in
        -f|--flash) flash=true ;;
        -c) args+=(--clean) ;;
        -d) args+=(--debug) ;;
        *) args+=("$arg") ;;
    esac
done
bash "$root/scripts/airspot" build "${args[@]}"
if $flash; then bash "$root/scripts/airspot" flash; fi
