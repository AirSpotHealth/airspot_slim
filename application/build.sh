#!/bin/bash
# Simple build script for AirSpot Slim

set -e

# Rev3A uses the same checked build/flash path as the interactive CLI.
if [[ "${AIRSPOT_HARDWARE:-rev3a}" = rev3a ]]; then
    rev3a_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
    rev3a_args=()
    rev3a_flash=false
    for arg in "$@"; do
        case "$arg" in
            -f|--flash) rev3a_flash=true ;;
            -c|--clean) rev3a_args+=(--clean) ;;
            -d|--debug) rev3a_args+=(--debug) ;;
            *) rev3a_args+=("$arg") ;;
        esac
    done
    bash "$rev3a_root/scripts/airspot-rev3a" build "${rev3a_args[@]}"
    if [[ "$rev3a_flash" = true ]]; then
        bash "$rev3a_root/scripts/airspot-rev3a" flash "${rev3a_args[@]}"
    fi
    exit 0
fi

BOARD="nrf54l15dk/nrf54l05/cpuapp"

echo "=========================================="
echo "AirSpot Slim - Build Script"
echo "=========================================="
echo ""

# Check if cmake is available
if ! command -v cmake &> /dev/null; then
    echo "ERROR: 'cmake' command not found!"
    echo "Please install CMake or use VS Code extension"
    exit 1
fi

# Check if ZEPHYR_BASE is set
if [ -z "$ZEPHYR_BASE" ]; then
    echo "WARNING: ZEPHYR_BASE not set"
    echo "Trying to find Zephyr base..."
    # Try common locations
    if [ -d "/opt/nordic/ncs/v3.2.0/zephyr" ]; then
        export ZEPHYR_BASE="/opt/nordic/ncs/v3.2.0/zephyr"
        # Source zephyr-env.sh to set up environment properly
        if [ -f "$ZEPHYR_BASE/zephyr-env.sh" ]; then
            source "$ZEPHYR_BASE/zephyr-env.sh"
        fi
        echo "Found ZEPHYR_BASE: $ZEPHYR_BASE"
    elif [ -d "$HOME/ncs/zephyr" ]; then
        export ZEPHYR_BASE="$HOME/ncs/zephyr"
        if [ -f "$ZEPHYR_BASE/zephyr-env.sh" ]; then
            source "$ZEPHYR_BASE/zephyr-env.sh"
        fi
        echo "Found ZEPHYR_BASE: $ZEPHYR_BASE"
    else
        echo "ERROR: Cannot find Zephyr base"
        echo "Please set ZEPHYR_BASE or use VS Code extension"
        exit 1
    fi
fi

echo "Board: $BOARD"
echo "ZEPHYR_BASE: $ZEPHYR_BASE"
echo ""

# Parse command line arguments
CLEAN=false
FLASH=false
DEBUG=false
EXTRA_CONF=""

while [[ $# -gt 0 ]]; do
    case $1 in
        -c|--clean)
            CLEAN=true
            shift
            ;;
        -f|--flash)
            FLASH=true
            shift
            ;;
        -d|--debug)
            DEBUG=true
            EXTRA_CONF="-DEXTRA_CONF_FILE=prj_debug.conf"
            shift
            ;;
        *)
            echo "Unknown option: $1"
            echo "Usage: $0 [-c|--clean] [-f|--flash] [-d|--debug]"
            exit 1
            ;;
    esac
done

if [ "$DEBUG" = true ]; then
    echo "Debug build: UART logging enabled (prj_debug.conf)"
fi

# Clean build if requested
if [ "$CLEAN" = true ]; then
    echo "Cleaning build directory..."
    rm -rf build
    echo ""
fi

# Create build directory if it doesn't exist
mkdir -p build

# Build using CMake (Zephyr build system)
echo "Building with CMake (Zephyr build system)..."
cmake -B build -GNinja -DBOARD=$BOARD $EXTRA_CONF -S .
cmake --build build

# Check if build succeeded
if [ $? -eq 0 ]; then
    echo ""
    echo "=========================================="
    echo "Build SUCCESS!"
    echo "=========================================="
    echo ""
    echo "Output files:"
    ls -lh build/zephyr/*.hex build/zephyr/*.elf 2>/dev/null | awk '{print "  " $9 " (" $5 ")"}'
    echo ""
    
    # Flash if requested
    if [ "$FLASH" = true ]; then
        echo "Flashing to device..."
        cmake --build build --target flash
        if [ $? -eq 0 ]; then
            echo ""
            echo "=========================================="
            echo "Flash SUCCESS!"
            echo "=========================================="
        else
            echo ""
            echo "=========================================="
            echo "Flash FAILED!"
            echo "=========================================="
            exit 1
        fi
    else
        echo "To flash, run: $0 --flash"
        echo "Or: west flash"
    fi
else
    echo ""
    echo "=========================================="
    echo "Build FAILED!"
    echo "=========================================="
    exit 1
fi
