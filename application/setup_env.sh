#!/bin/bash
# Environment setup script for AirSpot Slim
# Source this file to set up Zephyr environment

# Use project's local Zephyr SDK from external folder
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
export ZEPHYR_BASE="$SCRIPT_DIR/../external/zephyr"

# Check and install Python dependencies if needed
check_python_deps() {
    local missing_deps=()
    
    python3 -c "import pykwalify" 2>/dev/null || missing_deps+=("pykwalify")
    python3 -c "import yaml" 2>/dev/null || missing_deps+=("pyyaml")
    
    if [ ${#missing_deps[@]} -gt 0 ]; then
        echo "⚠️  Missing Python dependencies: ${missing_deps[*]}"
        echo "   Installing..."
        python3 -m pip install "${missing_deps[@]}" --break-system-packages --quiet
        if [ $? -eq 0 ]; then
            echo "✅ Python dependencies installed"
        else
            echo "❌ Failed to install Python dependencies"
            echo "   Please install manually: pip install ${missing_deps[*]}"
        fi
    fi
}

# Check Python dependencies
check_python_deps

if [ -f "$ZEPHYR_BASE/zephyr-env.sh" ]; then
    source "$ZEPHYR_BASE/zephyr-env.sh"
    # Override toolchain settings after sourcing zephyr-env.sh
    export ZEPHYR_TOOLCHAIN_VARIANT=zephyr
    export ZEPHYR_SDK_INSTALL_DIR=/opt/nordic/ncs/toolchains/322ac893fe/opt/zephyr-sdk
    # Add toolchain binaries to PATH (compiler, dtc, etc.)
    export PATH="/opt/nordic/ncs/toolchains/322ac893fe/opt/zephyr-sdk/arm-zephyr-eabi/bin:/opt/nordic/ncs/toolchains/322ac893fe/bin:/opt/nordic/ncs/toolchains/322ac893fe/Cellar/dtc/1.6.1/bin:$PATH"
    echo "✅ Zephyr environment configured"
    echo "   ZEPHYR_BASE: $ZEPHYR_BASE"
    echo "   ZEPHYR_TOOLCHAIN_VARIANT: $ZEPHYR_TOOLCHAIN_VARIANT"
    echo "   West version: $(west --version 2>/dev/null || echo 'not found')"
else
    echo "❌ Error: zephyr-env.sh not found at $ZEPHYR_BASE"
    return 1 2>/dev/null || exit 1
fi

