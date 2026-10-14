# Slim documentation

Start with [setup](GETTING_STARTED.md) for a new checkout, or
[test status](STATUS.md) to see what has passed on the current firmware.

## Build and connect

| Guide | Covers |
| :--- | :--- |
| [Getting started](GETTING_STARTED.md) | SDK setup, signing key, first build and J-Link |
| [Updates and recovery](OTA.md) | Wired installation, Bluetooth OTA, rollback and memory layout |
| [Production board](board/PRODUCTION_BOARD.md) | Rev3A wiring, GPIO assignments and hardware evidence |
| [BLE protocol](../application/docs/protocol_commands.md) | Commands, payloads and responses |

## Device behavior

| Guide | Covers |
| :--- | :--- |
| [Automatic sensor recovery](SLIM_AUTOMATIC_RECOVERY.md) | Retry timing, fault handling and recovery telemetry |
| [Factory reset](SLIM_FACTORY_RESET_POWER_OFF.md) | Erasure, power-off, button wake and device test results |
| [Settling after calibration erase](SLIM_SETTLING_GUIDANCE.md) | Persistent progress and fresh-air guidance |
| [History erase fix](SLIM_HISTORY_ERASE_FIX.md) | Flash metadata, saved cursors and restart behavior |
| [Battery recovery](BATTERY_RECOVERY.md) | Low-battery handling and recovery conditions |

## Investigations and reference material

- [Pressure sensor investigation](PRESSURE_RECOVERY_INVESTIGATION.md)
- [Sensor diagnostics](SLIM_SENSOR_DIAGNOSTICS.md)
- [Schematics and datasheets](datasheets/)
- [Earlier qualification records](archive/README.md)
- [Cross-project AirSpot CLI](AIRSPOT_CLI.md) — describes the wider development workspace; not every component is included in this repository.

Investigation notes preserve observations from specific devices and firmware
versions. Use the current source and [status page](STATUS.md) to establish what
applies today.
