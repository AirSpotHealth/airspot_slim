# AirSpot Slim Firmware Implementation Plan

## Executive Summary

This document outlines the migration plan from the existing nRF52832-based firmware (using nRF5 SDK v17.1.0) to a new nRF54L05-based firmware (using nRF Connect SDK with Zephyr RTOS) for the AirSpot Slim device.

**Target Platform:**

- **MCU**: nRF54L05 (ARM Cortex-M33, dual-core)
- **SDK**: nRF Connect SDK (Zephyr RTOS)
- **Development Board**: nRF54L15DK (connected via JTAG 0.5mm)
- **Project Location**: `airspot_slim/` folder

**Key Hardware Changes:**

- ✅ **Qi Wireless Charging** (replaces USB charging)
- ❌ **No Display** (ST7301 LCD removed)
- ✅ **Circuit redesign** (new board layout)
- ✅ **Same CO2 Sensor**: Sensirion STCC4/STC41
- ✅ **Same Flash**: AT25XE161D SPI flash
- ✅ **Same BLE**: Bluetooth Low Energy

---

## Current Status: Phase 11 - Validation & Release

**Last Updated**: 2026-01-24

### Implementation Progress

| Phase    | Description           | Status         | Completion |
| -------- | --------------------- | -------------- | ---------- |
| Phase 1  | Foundation            | ✅ Complete    | 100%       |
| Phase 2  | Hardware Drivers      | ✅ Complete    | 100%       |
| Phase 3  | Sensor & Storage      | ✅ Complete    | 100%       |
| Phase 4  | BLE Protocol          | ✅ Complete    | 100%       |
| Phase 5  | Application Logic     | ✅ Complete    | 100%       |
| Phase 6  | Integration & Testing | ✅ Complete    | 100%       |
| Phase 7  | Battery & Power       | ✅ Complete    | 100%       |
| Phase 8  | BLE NUS + Protocol    | ✅ Complete    | 100%       |
| Phase 9  | MCUboot DFU           | ✅ Complete    | 100%       |
| Phase 10 | Documentation         | ✅ Complete    | 100%       |
| Phase 11 | Validation & Release  | 🔄 In Progress | 10%        |

---

## 1. Feature Analysis - Existing Firmware

### 1.1 Core Features to Migrate

#### ✅ **Migrated Successfully**

1. **CO2 Monitoring** ✅
   - Real-time CO2, temperature, humidity measurement
   - STCC4/STC41 sensor driver (`src/drivers/stcc4.c`)
   - Ambient compensation from SHT40
   - Sensor error handling and recovery

2. **BLE Communication** ✅
   - Nordic UART Service (NUS) - `src/services/airspot_nus.c`
   - Custom protocol implementation (`src/services/protocol.c`)
   - All protocol commands implemented
   - BLE advertising and connection management

3. **Data Logging** ✅
   - Historical CO2 storage to SPI flash (`src/services/data_log.c`)
   - Half-page based storage system
   - History retrieval via BLE protocol
   - Erase and management commands

4. **Power Management** ✅
   - System OFF shipping mode (`src/services/power_manager.c`)
   - Tap + tap-hold-3s power gesture
   - Ultra-low power sleep (~1-3 µA)
   - Button wake-up

5. **Battery Management** ✅
   - Battery voltage monitoring via ADC (`src/drivers/battery.c`)
   - Battery level calculation (0-100%)
   - Qi charging detection (`src/drivers/qi_charger.c`)
   - Low battery indication

6. **User Interface** ✅
   - Button handling (`src/drivers/button_driver.c`)
   - LED indicators (`src/services/led_indicator.c`)
   - CO2 level color feedback (Green/Amber/Red)
   - Charging and pairing indicators

7. **User Settings** ✅
   - NVS-backed persistence (`src/services/user_settings.c`)
   - All configurable parameters
   - Flight mode support

8. **Factory Testing** ✅
   - Automated hardware tests (`src/services/factory_test.c`)
   - LED pass/fail indication
   - Report generation for BLE

9. **OTA Updates** ✅
   - MCUboot bootloader (41KB)
   - MCUmgr over BLE (SMP protocol)
   - RSA-2048 signed images
   - Works with nRF Device Manager app

#### ❌ **Removed (Display-Related)**

1. **LCD Display** - Hardware removed
2. **Display-Related Protocol Commands** - Not applicable
3. **USB Charging** - Replaced with Qi

---

## 2. Implemented Components

### 2.1 Drivers (`src/drivers/`)

| Driver     | File              | Status     | Description                 |
| ---------- | ----------------- | ---------- | --------------------------- |
| I2C Bus    | `i2c_bus.c`       | ✅ Working | 400kHz bus management, scan |
| SHT40      | `sht40.c`         | ✅ Working | Temperature & Humidity      |
| LPS22HH    | `lps22hh.c`       | ✅ Working | Pressure sensor             |
| STCC4      | `stcc4.c`         | ✅ Working | CO2 sensor                  |
| Battery    | `battery.c`       | ✅ Working | ADC voltage reading         |
| Qi Charger | `qi_charger.c`    | ✅ Working | Charge detection            |
| SPI Flash  | `spi_flash.c`     | ✅ Working | AT25XE161D driver           |
| Button     | `button_driver.c` | ✅ Working | Polling with events         |
| LED        | `led_driver.c`    | ✅ Working | RGB LED control             |

### 2.2 Services (`src/services/`)

| Service        | File                 | Status     | Description            |
| -------------- | -------------------- | ---------- | ---------------------- |
| BLE Service    | `ble_service.c`      | ✅ Working | Core BLE management    |
| NUS            | `airspot_nus.c`      | ✅ Working | Nordic UART Service    |
| Protocol       | `protocol.c`         | ✅ Working | App command handler    |
| Sensor Manager | `sensor_manager.c`   | ✅ Working | Sensor coordination    |
| Data Log       | `data_log.c`         | ✅ Working | History storage        |
| User Settings  | `user_settings.c`    | ✅ Working | NVS persistence        |
| Power Manager  | `power_manager.c`    | ✅ Working | System OFF, gestures   |
| LED Indicator  | `led_indicator.c`    | ✅ Working | Pattern engine         |
| Factory Test   | `factory_test.c`     | ✅ Working | Hardware tests         |
| BLE Mode       | `ble_mode_service.c` | ✅ Working | Peripheral/Mesh switch |
| Mesh Sensor    | `mesh_sensor.c`      | ✅ Ready   | Bluetooth Mesh sensor  |

---

## 3. Hardware Configuration

### 3.1 Pin Assignments (nRF54L05)

| Function     | Pin   | Config      |
| ------------ | ----- | ----------- |
| UART TX      | P1.04 | 115200 baud |
| UART RX      | P1.05 | 115200 baud |
| I2C SDA      | P0.04 | 400kHz      |
| I2C SCL      | P0.03 | 400kHz      |
| SPI SCK      | P1.09 | Flash       |
| SPI MOSI     | P1.10 | Flash       |
| SPI MISO     | P0.02 | Flash       |
| SPI CS       | P0.01 | Flash       |
| LED Red      | P2.00 | GPIO        |
| LED Green    | P2.01 | GPIO        |
| LED Blue     | P2.02 | GPIO        |
| Button       | P1.08 | Active low  |
| Battery ADC  | P1.05 | AIN1        |
| Qi Detection | P0.00 | GPIO        |

### 3.2 I2C Devices

| Device  | Address | Function      |
| ------- | ------- | ------------- |
| SHT40   | 0x44    | Temp/Humidity |
| LPS22HH | 0x5D    | Pressure      |
| STCC4   | 0x64    | CO2           |

---

## 4. Implementation Phases (Completed)

### Phase 1: Foundation ✅

**Status**: Complete

**Completed Tasks**:

- [x] Set up nRF Connect SDK development environment
- [x] Create Zephyr application skeleton
- [x] Configure device tree for nRF54L05 custom board
- [x] Set up CMake build system
- [x] Configure JTAG debugging via nRF54L15DK
- [x] Get basic BLE advertising working
- [x] Verify UART logging works

### Phase 2: Hardware Drivers ✅

**Status**: Complete

**Completed Tasks**:

- [x] Port GPIO configuration (device tree)
- [x] Port I2C driver for sensors
- [x] Port SPI driver for flash memory
- [x] Port ADC driver for battery monitoring
- [x] Port button driver (polling mode)
- [x] Implement Qi charging detection
- [x] Port LED driver

### Phase 3: Sensor & Storage ✅

**Status**: Complete

**Completed Tasks**:

- [x] Port STCC4/STC41 sensor driver
- [x] Port SHT40 driver
- [x] Port LPS22HH driver
- [x] Port SPI flash driver (AT25XE161D)
- [x] Port history storage system
- [x] Port configuration storage (NVS)

### Phase 4: BLE Protocol ✅

**Status**: Complete

**Completed Tasks**:

- [x] Port Nordic UART Service (NUS)
- [x] Port protocol frame parsing
- [x] Port all protocol commands
- [x] Real-time CO2 notifications
- [x] History data retrieval
- [x] Device state reporting

### Phase 5: Application Logic ✅

**Status**: Complete

**Completed Tasks**:

- [x] Implement sensor manager thread
- [x] Port user settings
- [x] Implement power management
- [x] Port factory testing
- [x] Remove all display-related code

### Phase 6: Integration & Testing ✅

**Status**: Complete

**Completed Tasks**:

- [x] End-to-end testing
- [x] BLE range and reliability testing
- [x] Factory test validation
- [x] Bug fixes and refinements

### Phase 7: Battery & Power ✅

**Status**: Complete

**Completed Tasks**:

- [x] Battery ADC calibration (~98% accuracy)
- [x] Qi charger detection
- [x] Power gesture implementation
- [x] System OFF shipping mode

### Phase 8: BLE NUS + App Protocol ✅

**Status**: Complete

**Completed Tasks**:

- [x] NUS TX/RX working
- [x] All protocol commands implemented
- [x] Sensor manager integration
- [x] Data logging to flash
- [x] User settings persistence
- [x] BLE mode switching

### Phase 9: MCUboot DFU ✅

**Status**: Complete

**Completed Tasks**:

- [x] MCUboot integration (41KB bootloader)
- [x] MCUmgr over BLE (SMP protocol)
- [x] Signed image verification (RSA-2048)
- [x] OTA updates via nRF Device Manager

### Phase 10: Documentation ✅

**Status**: Complete

**Tasks**:

- [x] Update all README files
- [x] Create service documentation
- [x] Add inline code documentation
- [x] Document BLE protocol commands
- [x] Create developer guide

### Phase 11: Validation & Release 🔄

**Status**: In Progress

**Tasks**:

- [ ] Verify Qi charging with hardware
- [ ] Power consumption profiling
- [ ] Sensor accuracy validation
- [ ] BLE protocol regression
- [ ] MCUboot DFU validation
- [ ] Factory test flow verification
- [ ] Production firmware release checklist

---

## 5. Next Steps

### Immediate

1. Complete documentation updates
2. Verify Qi charging with actual hardware
3. Power consumption optimization
4. Production firmware release

### Future Enhancements

1. Bluetooth Mesh integration
2. Advanced alarm features
3. Multi-device synchronization
4. Cloud connectivity (via app)

---

## 6. Project Structure

```
airspot_slim/
├── application/
│   ├── CMakeLists.txt           # Build configuration
│   ├── Kconfig                   # Kconfig options
│   ├── prj.conf                  # Zephyr configuration
│   ├── build.sh                  # Build script
│   ├── pm_static.yml             # Partition manager
│   │
│   ├── boards/                   # Board-specific files
│   │   └── arm/airspot_slim_nrf54l05_cpuapp/
│   │
│   ├── include/                  # Public headers
│   │   ├── airspot/             # Device definitions
│   │   ├── drivers/             # Driver interfaces
│   │   └── services/            # Service interfaces
│   │
│   └── src/                      # Source files
│       ├── main.c               # Application entry
│       ├── drivers/             # Driver implementations
│       └── services/            # Service implementations
│
├── docs/                         # Documentation
│   ├── board/                   # Hardware docs
│   ├── firmware/                # Firmware docs
│   └── datasheets/              # Component datasheets
│
└── external/                     # Zephyr, nRF SDK modules
```

---

## 7. Build Instructions

### Quick Build

```bash
cd application
source ../scripts/airspot
./build.sh
```

### Build and Flash

```bash
./build.sh --flash
```

### Clean Build

```bash
./build.sh --clean --flash
```

### OTA Package

After building, the DFU package is at:

```
build/dfu_application.zip
```

---

## 8. Testing Checklist

### Hardware Tests

- [x] I2C bus scan (3 devices found)
- [x] SHT40 temperature/humidity
- [x] LPS22HH pressure
- [x] STCC4 CO2 readings
- [x] Battery ADC accuracy
- [x] SPI flash read/write/erase
- [x] LED RGB control
- [x] Button press detection
- [x] Qi charger detection

### BLE Tests

- [x] Advertising visible
- [x] Connection stable
- [x] NUS TX/RX working
- [x] Protocol commands
- [x] Real-time CO2 notifications
- [x] History data retrieval
- [x] MCUmgr DFU

### System Tests

- [x] Power-on gesture
- [x] Power-off gesture
- [x] System OFF mode
- [x] Wake from System OFF
- [x] Factory test sequence
- [x] LED indicator patterns

---

## 9. References

### Documentation

- [QUICK_START.md](./QUICK_START.md) - Getting started guide
- [BUILD_INSTRUCTIONS.md](./BUILD_INSTRUCTIONS.md) - Build details
- [SENSORS.md](./SENSORS.md) - Sensor driver documentation
- [CODE_ORGANIZATION.md](./CODE_ORGANIZATION.md) - Architecture guide
- [../board/PIN_ASSIGNMENTS.md](../board/PIN_ASSIGNMENTS.md) - Pin mapping

### External

- [nRF Connect SDK Documentation](https://developer.nordicsemi.com/)
- [Zephyr RTOS Documentation](https://docs.zephyrproject.org/)
- [nRF54L05 Product Page](https://www.nordicsemi.com/Products/nRF54L05)

---

**Document Version**: 2.0  
**Last Updated**: 2026-01-24  
**Author**: Firmware Development Team  
**Status**: Active - Phase 10 (Documentation)
