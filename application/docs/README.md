# Documentation

This directory contains all documentation for the AirSpot Slim firmware project.

## Structure

### `board/` - Hardware Documentation
- **PIN_ASSIGNMENTS.md**: Complete pin mapping from schematic
- **HARDWARE_REFERENCE.md**: Hardware reference information
- **SCHEMATIC_ANALYSIS.md**: Schematic analysis and findings
- **SCHEMATIC_FINDINGS.md**: Key discoveries from schematic

### `firmware/` - Firmware Documentation
- **BUILD_INSTRUCTIONS.md**: Detailed build and flash instructions
- **CODE_ORGANIZATION.md**: Architecture and best practices guide
- **IMPLEMENTATION_PLAN.md**: Complete migration and implementation plan
- **QUICK_START.md**: Quick start guide
- **SENSORS.md**: Complete sensor driver documentation (SHT40, LPS22HH, STCC4)

### `datasheets/` - Datasheets and Schematics
- **AH-013-0001-SCH CO2 Monitor (Standard Build) PWA Schematics Rev1A.PDF**: Main schematic

## Quick Links

| Topic              | Document                                                           |
| ------------------ | ------------------------------------------------------------------ |
| Getting Started    | [firmware/QUICK_START.md](./firmware/QUICK_START.md)               |
| Build Instructions | [firmware/BUILD_INSTRUCTIONS.md](./firmware/BUILD_INSTRUCTIONS.md) |
| Sensor Drivers     | [firmware/SENSORS.md](./firmware/SENSORS.md)                       |
| Pin Assignments    | [board/PIN_ASSIGNMENTS.md](./board/PIN_ASSIGNMENTS.md)             |
| Architecture       | [firmware/CODE_ORGANIZATION.md](./firmware/CODE_ORGANIZATION.md)   |
| Schematic          | [datasheets/](./datasheets/)                                       |

## Working Features

| Feature        | Status    | Documentation                                    |
| -------------- | --------- | ------------------------------------------------ |
| LEDs           | ✅ Working | [PIN_ASSIGNMENTS.md](./board/PIN_ASSIGNMENTS.md) |
| I2C Bus        | ✅ Working | [SENSORS.md](./firmware/SENSORS.md)              |
| SPI Flash      | ✅ Working | [PIN_ASSIGNMENTS.md](./board/PIN_ASSIGNMENTS.md) |
| Button         | ✅ Working | Polling mode                                     |
| SHT40 Sensor   | ✅ Working | [SENSORS.md](./firmware/SENSORS.md)              |
| LPS22HH Sensor | ✅ Working | [SENSORS.md](./firmware/SENSORS.md)              |
| STCC4 Sensor   | ✅ Working | [SENSORS.md](./firmware/SENSORS.md)              |
| BLE            | ✅ Working | Basic peripheral                                 |
| DFU            | ✅ Ready   | Temporarily disabled                             |

---

**Last Updated**: 2026-01-19

