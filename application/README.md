# AirSpot Slim Firmware

Firmware for the AirSpot Slim device based on nRF54L05 and STCC4 Slim hardware.

## Overview

This firmware is a migration from the nRF52832-based firmware to the new nRF54L05 platform using nRF Connect SDK with Zephyr RTOS.

**Key Differences from Original Firmware:**

- ✅ **MCU**: nRF54L05 (ARM Cortex-M33, dual-core)
- ✅ **SDK**: nRF Connect SDK (Zephyr RTOS)
- ✅ **Qi Charging**: Wireless charging support
- ❌ **No Display**: LCD removed from hardware
- ✅ **Same Features**: All other features maintained

## Hardware

- **MCU**: Nordic nRF54L05
- **CO2 Sensor**: Sensirion SCD4x series
- **Flash Memory**: ZB25D16 SPI flash
- **Charging**: Qi wireless charging
- **Debug**: JTAG via nRF54L15DK (0.5mm connector)

## Software Requirements

### Required Tools

1. **nRF Connect SDK** v2.x or later
   - Download from: [Nordic Developer Zone](https://www.nordicsemi.com/Software-and-tools/Software/nRF-Connect-SDK)
   - Follow installation guide

2. **VS Code** with Extensions
   - nRF Connect for VS Code extension
   - C/C++ extension
   - CMake extension

3. **West Tool**
   - Included with nRF Connect SDK
   - Used for building and managing the project

4. **J-Link Software**
   - Download from: [Segger J-Link](https://www.segger.com/downloads/jlink/)
   - Required for programming and debugging

### Optional Tools

- **nRF Connect for Desktop** - For BLE debugging
- **J-Link RTT Viewer** - For real-time logging

## Project Structure

```
application/
├── README.md                   # This file
├── CMakeLists.txt              # Build configuration
├── Kconfig                     # Kconfig options
├── prj.conf                    # Zephyr configuration
├── build.sh                    # Build script
├── setup_env.sh                # Environment setup script
│
├── boards/                     # Board-specific files
│   └── nrf54l15dk_nrf54l05_cpuapp.overlay
│
├── docs/                       # All documentation
│   ├── README.md              # Documentation index
│   ├── board/                 # Hardware documentation
│   │   ├── PIN_ASSIGNMENTS.md
│   │   ├── PIN_CONFIGURATION.md
│   │   ├── SCHEMATIC_ANALYSIS.md
│   │   └── SCHEMATIC_FINDINGS.md
│   ├── firmware/              # Firmware documentation
│   │   ├── BUILD_INSTRUCTIONS.md
│   │   ├── CODE_ORGANIZATION.md
│   │   ├── IMPLEMENTATION_PLAN.md
│   │   └── QUICK_START.md
│   └── datasheets/            # Schematics and datasheets
│
├── include/                    # Public headers
│   ├── airspot/               # AirSpot-specific
│   │   ├── board.h
│   │   └── version.h
│   ├── drivers/               # Driver interfaces
│   │   ├── button_driver.h
│   │   └── led_driver.h
│   └── services/              # Service interfaces
│       └── ble_service.h
│
└── src/                        # Source files
    ├── main.c                  # Application entry
    ├── drivers/                # Driver implementations
    │   ├── button_driver.c
    │   └── led_driver.c
    └── services/               # Service implementations
        └── ble_service.c
```

### Code Organization

**`include/` - Public Headers**

- `airspot/`: AirSpot-specific definitions (version, board)
- `drivers/`: Driver interfaces (LED, button, etc.)
- `services/`: Service interfaces (BLE, power management, etc.)

**`src/` - Source Files**

- `main.c`: Application entry point
- `drivers/`: Driver implementations
- `services/`: Service implementations

**`boards/` - Board Configuration**

- Device tree overlays for hardware configuration

**`docs/` - Documentation**

- `board/`: Hardware documentation (pin assignments, schematics)
- `firmware/`: Firmware documentation (build, architecture, implementation)
- `datasheets/`: Schematics and component datasheets

See [docs/firmware/CODE_ORGANIZATION.md](./docs/firmware/CODE_ORGANIZATION.md) for detailed architecture and best practices.

## Getting Started

### 1. Install nRF Connect SDK

Follow the official installation guide:
https://developer.nordicsemi.com/nRF_Connect_SDK/doc/latest/nrf/getting_started.html

**Recommended**: Use VS Code with "nRF Connect for VS Code" extension for easiest setup.

### 2. Build the Project

#### Using Build Script (Recommended)

```bash
cd application

# Source environment (sets ZEPHYR_BASE)
source setup_env.sh

# Build
./build.sh

# Build and flash
./build.sh --flash

# Clean build
./build.sh --clean --flash
```

#### Using VS Code Extension

1. Open VS Code in the `airspot_slim/` directory (workspace root)
2. Use the nRF Connect extension to build and flash
3. The extension is configured automatically

#### Using West (from workspace root)

```bash
cd airspot_slim
west build -b nrf54l15dk/nrf54l05/cpuapp application
west flash
```

### 3. Testing

- **LEDs**: Should blink on startup
- **Button**: Press button, check serial logs
- **BLE**: Device advertises as "AirSpot Slim"
- **Serial Logs**: Connect UART at 115200 baud

See [docs/firmware/BUILD_INSTRUCTIONS.md](./docs/firmware/BUILD_INSTRUCTIONS.md) for detailed instructions.

## Features (Current Status)

### ✅ Implemented & Working

| Feature          | Status      | Notes                                  |
| ---------------- | ----------- | -------------------------------------- |
| **BLE**          | ✅ Working  | Basic BLE advertising and connection   |
| **DFU**          | ✅ Ready    | MCUboot support (temporarily disabled) |
| **Button**       | ✅ Working  | Polling mode (press/long-press)        |
| **LEDs**         | ✅ Working  | RGB LED on GPIO2                       |
| **UART Logging** | ✅ Working  | 115200 baud                            |
| **I2C Bus**      | ✅ Working  | 400kHz on P0.03/P0.04                  |
| **SPI Flash**    | ✅ Working  | AT25XE161D, JEDEC verified             |
| **SHT40**        | ✅ Working  | Temperature & Humidity                 |
| **LPS22HH**      | ✅ Working  | Pressure (requires BOOT)               |
| **STCC4**        | ✅ Working  | CO2 sensor                             |
| **Battery ADC**  | ✅ Verified | ~98% accuracy (P1.05/AIN1)             |
| **Qi Charging**  | 🔶 Untested | Driver ready, needs HW test (P0.00)    |

### ✅ Phase 8: BLE NUS + App Protocol

| Feature              | Status     | Notes                               |
| -------------------- | ---------- | ----------------------------------- |
| **NUS Service**      | ✅ Working | Nordic UART Service for app comms   |
| **Protocol Handler** | ✅ Working | All commands implemented            |
| **Sensor Manager**   | ✅ Working | Periodic measurements with callback |
| **Data Logging**     | ✅ Working | CO2 history to SPI flash            |
| **User Settings**    | ✅ Working | NVS-backed persistence              |
| **BLE Mode Service** | ✅ Working | Peripheral/Mesh switching           |

### 🚧 Planned (Next Steps)

- Power management optimization
- Factory testing mode
- MCUboot DFU re-enable

### ❌ Removed from Original

- LCD display (hardware removed)
- Display-related protocol commands
- USB charging (replaced with Qi)

## Development Applications

| Application         | Purpose                 | Location           |
| ------------------- | ----------------------- | ------------------ |
| **incremental_app** | Main development app    | `incremental_app/` |
| **sensor_test**     | Isolated sensor testing | `sensor_test/`     |
| **flash_test**      | SPI flash testing       | `flash_test/`      |
| **button_test**     | Button testing          | `button_test/`     |

## Development Status

**Current Phase**: Phase 8 - BLE NUS + App Protocol (Complete)

See [docs/firmware/IMPLEMENTATION_PLAN.md](./docs/firmware/IMPLEMENTATION_PLAN.md) for detailed migration plan.
See [docs/firmware/SENSORS.md](./docs/firmware/SENSORS.md) for sensor driver documentation.

## Documentation

All documentation is organized in the `docs/` directory:

| Topic                   | Document                                                                       |
| ----------------------- | ------------------------------------------------------------------------------ |
| **Getting Started**     | [docs/firmware/QUICK_START.md](./docs/firmware/QUICK_START.md)                 |
| **Build Instructions**  | [docs/firmware/BUILD_INSTRUCTIONS.md](./docs/firmware/BUILD_INSTRUCTIONS.md)   |
| **Sensor Drivers**      | [docs/firmware/SENSORS.md](./docs/firmware/SENSORS.md)                         |
| **Architecture**        | [docs/firmware/CODE_ORGANIZATION.md](./docs/firmware/CODE_ORGANIZATION.md)     |
| **Implementation Plan** | [docs/firmware/IMPLEMENTATION_PLAN.md](./docs/firmware/IMPLEMENTATION_PLAN.md) |
| **Pin Assignments**     | [docs/board/PIN_ASSIGNMENTS.md](./docs/board/PIN_ASSIGNMENTS.md)               |
| **Schematic**           | [docs/datasheets/](./docs/datasheets/)                                         |

See [docs/README.md](./docs/README.md) for complete documentation index.

## References

- [nRF Connect SDK Documentation](https://developer.nordicsemi.com/nRF_Connect_SDK/doc/latest/)
- [Zephyr RTOS Documentation](https://docs.zephyrproject.org/)
- [nRF54L05 Product Page](https://www.nordicsemi.com/Products/nRF54L05)
- [Original Firmware](../nRF5_SDK_17.1.0_ddde560/examples/ble_peripheral/ble_app_uart_minico2/)

## License

[Specify license]

---

**Version**: 0.8.0 (Phase 8 - BLE NUS + App Protocol)  
**Last Updated**: 2026-01-20
