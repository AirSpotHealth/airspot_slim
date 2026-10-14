# Developing Slim firmware

Slim Rev3A uses an nRF54L15, Zephyr and nRF Connect SDK v3.2.1.
Start with [Getting started](../GETTING_STARTED.md) to configure the toolchain
and build the firmware.

## Where to work

| Area | Location |
| :--- | :--- |
| Application startup | [application/src](../../application/src) |
| Hardware drivers | [drivers](../../application/src/drivers) |
| Sampling, history and Bluetooth | [services](../../application/src/services) |
| Board configuration | [boards](../../application/boards) |
| Build and programming helpers | [scripts](../../scripts) |
| Host regression tests | [tests](../../tests) |

Check the [production board reference](../board/PRODUCTION_BOARD.md) before
changing GPIO or power behavior. For app-facing changes, update the
[BLE command reference](../../application/docs/protocol_commands.md) alongside
the implementation.

## Checking a change

[Contributing](../../CONTRIBUTING.md) covers build checks, tests and what to
record during a device test. The [status page](../STATUS.md) lists completed
checks and unresolved hardware questions for the current candidate.

Use RTT for device logs. Keep bench captures, memory backups and signing keys
local. Record enough detail in an investigation note for someone else to repeat
the test without relying on a chat history.
