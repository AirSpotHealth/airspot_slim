# Build Instructions - AirSpot Slim

This document provides detailed instructions for building, flashing, and creating OTA update packages for the AirSpot Slim firmware.

## Quick Start (Recommended)

The project includes a unified helper script `scripts/airspot` that handles all common tasks.

```bash
# Sourcing the script adds the 'airspot' command to your shell
source scripts/airspot

# Show available commands
airspot help

# Build the firmware
airspot build

# Build and Flash
airspot bf

# View Logs
airspot logs
```

---

## 1. Prerequisites

1. **nRF Connect SDK (v2.x or later)**
   - [Installation Guide](https://developer.nordicsemi.com/nRF_Connect_SDK/doc/latest/nrf/getting_started.html)
   - Ensure `west` tool is in your PATH

2. **Command Line Tools**
   - **nRF Command Line Tools** (for `nrfjprog`, `nrfutil`)
   - **J-Link Software** (for flashing/debugging)
   - **CMake** & **Ninja**

---

## 2. Building Firmware

### Using the `airspot` Script

```bash
# Standard build (no MCUboot, faster for development)
airspot build

# Clean and build
airspot build --clean
```

### Output Files

After a successful build, artifacts are available in:

- `build/zephyr/airspot_slim_vX.Y.Z.hex`
- `builds/airspot_slim_vX.Y.Z.hex` (Easy access copy)

### Using West Directly

If you prefer standard Zephyr tools:

```bash
cd application
west build -b nrf54l15dk/nrf54l05/cpuapp
```

---

## 3. Flashing Firmware

### Using the `airspot` Script

```bash
# Flash the latest build
airspot flash

# Build and then flash
airspot bf
```

### Using West Directly

```bash
cd application
west flash
```

### Using nrfjprog directly

```bash
nrfjprog --program build/zephyr/zephyr.hex --sectorerase --verify --reset -f NRF54L
```

---

## 4. OTA / DFU Updates

To perform Over-The-Air updates, you need to generate a DFU package containing the signed firmware image.

## 4. OTA / DFU Updates

To perform Over-The-Air updates, you need to generate a DFU package containing the signed firmware image.

### Generating DFU Package

Use the `--dfu` flag with the build command. This handles enabling MCUboot (via sysbuild) and packaging the artifact.

```bash
# Build with MCUboot and generate DFU zip
airspot build --dfu
```

### Artifact Organization

Releases are automatically organized by version in the `releases/` directory.

**Example Structure:**

```
releases/v1.0.0/
├── airspot_slim_v1.0.0_TIMESTAMP.hex          (Standard Build)
├── airspot_slim_v1.0.0_TIMESTAMP_dfu.zip      (DFU Package from --dfu build)
├── airspot_slim_v1.0.0_TIMESTAMP_signed.bin   (Signed Image)
└── airspot_slim_v1.0.0_TIMESTAMP_merged.hex   (Full Image with Bootloader)
```

### Performing the Update

1. **Mobile App**:
   - Install **nRF Connect Device Manager** ([iOS](https://apps.apple.com/app/nrf-connect-device-manager/id1519423539) / [Android](https://play.google.com/store/apps/details?id=no.nordicsemi.android.nrfconnectdevicemanager))
   - Connect to "AirSpot Slim"
   - Select "DFU" tab
   - Select the generated `.zip` file
   - Tap "Start"

2. **Desktop**:
   - Use **nRF Connect for Desktop** -> **Bluetooth Low Energy** app
   - Connect to device
   - Click "DFU" button
   - Select the `.zip` file

---

## 5. Debugging & Logging

### UART Logging

The device outputs logs on the UART port at 115200 baud.

```bash
# View UART logs
airspot logs

# Or specifically
airspot logs uart
```

### RTT Logging

If you have a J-Link debugger connected, RTT logging is faster and doesn't require UART pins.

```bash
airspot logs rtt
```

---

## 6. Directory Structure

```
airspot_slim/
├── application/          # Zephyr application source
│   ├── build.sh         # Legacy build script
│   └── ...
├── scripts/
│   └── airspot          # Main helper script
├── build/               # Temporary build artifacts
├── builds/              # Saved HEX files
├── ota/                 # Generated DFU packages
└── ...
```

---

## 7. Troubleshooting

### "Sysbuild failed" during OTA build

- Ensure `sysbuild.conf` enables MCUboot (`SB_CONFIG_BOOTLOADER_MCUBOOT=y`).
- Try cleaning the build directory: `airspot clean`.

### "No UART device found"

- Check USB connection.
- Verify user permissions for serial ports (`sudo usermod -a -G dialout $USER` on Linux).

### Flash failures

- Check J-Link connection.
- Ensure device is powered.
- Try `airspot erase` to completely wipe the chip (requires re-flashing bootloader via `airspot ota --build` + flash).

---

**Last Updated**: 2026-01-24
