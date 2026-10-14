# AirSpot Slim firmware

Current source: **0.19.2 candidate**, not a customer release. Includes automatic
sensor recovery, settling guidance, and a full factory reset that clears settings,
readings and calibration before electronic power-off. On 81A9DC, erasure and
0 V power-off were verified; button-wake and post-reset history/settling checks
remain pending. See [factory reset test status](docs/SLIM_FACTORY_RESET_POWER_OFF.md).

Signing private keys, local device backups, and current bench captures are not
included. Obtain the authorized product signing key separately to build signed
firmware; never commit it.

Production hardware: nRF54L15 AirSpot Slim, STCC4 CO₂, SHT40 temperature/humidity,
LPS22HH pressure, AT25XE161D history storage and Qi charging.
The hardware source of truth is the [board schematic](docs/datasheets/AH-013-0001-SCH%20CO2%20Monitor%20%28Standard%20Build%29%20PWA%20Schematics%20Rev3A.PDF).

Uses the pinned nRF Connect SDK workspace and one configuration: MCUboot,
signed Bluetooth OTA, the existing AirSpot BLE protocol, and SEGGER RTT logs.

```sh
source scripts/airspot
airspot build
airspot check
airspot flash    # Install firmware; back up memory and preserve settings bytes.
airspot logs
```

`airspot` shows a styled command guide; `airspot menu` opens the interactive menu.
Use `airspot devices` (or `info`), `reset`, `version`, `memory`, and `ports` to inspect
and control devices. `airspot bf` builds and flashes; `airspot bfl` also opens logs.
`airspot erase` confirms before removing internal firmware, bootloader, settings and
UICR; `-y` skips confirmation. External history flash is not erased. Reinstall with
`airspot flash`. Styling respects `NO_COLOR` and is disabled when output is redirected.

Flash and logs list connected J-Link probes, automatically use a single probe, and
prompt for a selection when several are connected. Use `--serial SERIAL` to select
a probe explicitly (required for noninteractive use with multiple probes). No serial
environment export is needed. Logs stream in the terminal until Ctrl-C and are also
saved to `application/build_production/rtt.log`. `airspot logs rtt` remains supported.

Flashing does not require a recognized bootloader or local build history. It preserves
36 KiB at `0x15c000` by default. Use `airspot flash --reset-settings` for fresh settings,
or `airspot flash --settings-source 0x74000` to migrate a device known to use that
older settings address. Arbitrary legacy layouts are not detected automatically.

For subsequent wired updates, use `airspot flash` for a checked wired reinstall, or upload
`application/build_production/airspot_slim_dfu.zip` through the mobile app's Slim
updater. The raw OTA file is
`application/build_production/application/zephyr/airspot_slim.signed.bin`.
Keep the same product signing key for subsequent builds; set `AIRSPOT_SIGNING_KEY`
if it is stored outside the ignored `application/keys/` directory.

- [Current firmware status](docs/STATUS.md)
- [Archived investigations and qualification records](docs/archive/README.md)
- [Wireless updates, recovery and signing](docs/OTA.md)
- [Board wiring and qualification](docs/board/PRODUCTION_BOARD.md)

Build success is not hardware qualification. The qualification record distinguishes
observed behavior from checks still needing a phone, charger or extended testing.
Historical release packages and older design notes describe earlier hardware and
must not be used to program this product.
