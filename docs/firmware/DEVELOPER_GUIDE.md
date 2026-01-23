# Developer Guide

Practical guide for building, flashing, debugging, and extending the AirSpot Slim firmware.

## Scope

This guide is for developers working on the nRF54L05-based AirSpot Slim firmware using nRF Connect SDK and Zephyr RTOS.

## Prerequisites

- nRF Connect SDK v2.x installed
- West tool available
- Segger J-Link software installed
- VS Code with nRF Connect for VS Code, C/C++ extensions

See [docs/firmware/BUILD_INSTRUCTIONS.md](BUILD_INSTRUCTIONS.md) for detailed setup and environment notes.

## Repository Layout

Key areas:

- Application sources: [application](../../application)
- Core firmware docs: [docs/firmware](.)
- Board and hardware docs: [docs/board](../board)
- Datasheets: [docs/datasheets](../datasheets)

## Build and Flash (VS Code Tasks)

From the workspace root, you can use the predefined tasks:

- AirSpot: Build
- AirSpot: Flash
- AirSpot: Build + Flash
- AirSpot: Build + Flash + Logs
- AirSpot: Logs

## Build and Flash (CLI)

Recommended flow:

1. Source the environment
2. Build
3. Flash

See [docs/firmware/BUILD_INSTRUCTIONS.md](BUILD_INSTRUCTIONS.md) for the exact commands and options.

## Logging and Debugging

- UART logging at 115200 baud (TX/RX on P1.04/P1.05)
- Use RTT Viewer if preferred
- Enable module logs in [application/prj.conf](../../application/prj.conf)

## Coding Conventions

- Follow existing file layout in [docs/firmware/CODE_ORGANIZATION.md](CODE_ORGANIZATION.md)
- Keep drivers in [application/src/drivers](../../application/src/drivers) with public headers in [application/include/drivers](../../application/include/drivers)
- Keep services in [application/src/services](../../application/src/services) with public headers in [application/include/services](../../application/include/services)
- Avoid blocking calls on the main thread

## Adding a New Driver

1. Define public API in [application/include/drivers](../../application/include/drivers)
2. Implement the driver in [application/src/drivers](../../application/src/drivers)
3. Add device tree bindings or overlays in [application/boards](../../application/boards)
4. Update build system if needed (CMake/Kconfig)

## Adding a New Service

1. Define public API in [application/include/services](../../application/include/services)
2. Implement service logic in [application/src/services](../../application/src/services)
3. Wire initialization from [application/src/main.c](../../application/src/main.c)
4. Add documentation entry in [docs/firmware/SERVICES.md](SERVICES.md)

## BLE Protocol Updates

- Protocol details are maintained in [docs/firmware/BLE_PROTOCOL.md](BLE_PROTOCOL.md)
- Command handling lives in [application/src/services/protocol.c](../../application/src/services/protocol.c)
- NUS transport is implemented in [application/src/services/airspot_nus.c](../../application/src/services/airspot_nus.c)

## Power and Performance

- Power states handled in [application/src/services/power_manager.c](../../application/src/services/power_manager.c)
- Sensor cadence and scheduling in [application/src/services/sensor_manager.c](../../application/src/services/sensor_manager.c)
- Use production board and real battery for power profiling

## Release Checklist

- Build and flash a clean image
- Verify BLE advertising and connection
- Validate sensor readings and logging
- Run factory test sequence
- Verify MCUboot DFU over BLE
- Update documentation

## Related Docs

- [docs/firmware/QUICK_START.md](QUICK_START.md)
- [docs/firmware/SENSORS.md](SENSORS.md)
- [docs/firmware/SERVICES.md](SERVICES.md)
- [docs/firmware/BLE_PROTOCOL.md](BLE_PROTOCOL.md)
