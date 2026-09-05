# AirSpot Slim firmware

Production hardware: nRF54L15 AirSpot Slim, STCC4 CO₂, SHT40 temperature/humidity,
LPS22HH pressure, AT25XE161D history storage and Qi charging.
The hardware source of truth is the [board schematic](docs/datasheets/AH-013-0001-SCH%20CO2%20Monitor%20%28Standard%20Build%29%20PWA%20Schematics%20Rev3A.PDF).

Uses the pinned nRF Connect SDK workspace and one configuration: MCUboot,
signed Bluetooth OTA, the existing AirSpot BLE protocol, and SEGGER RTT logs.

```sh
source scripts/airspot
export AIRSPOT_JLINK_SERIAL=603003276  # Set to your board's probe.
airspot build
airspot check
airspot flash    # First installation; backs up memory and migrates settings.
airspot logs rtt
```

For subsequent wired updates, use `airspot flash` for a checked wired reinstall, or upload
`application/build_production/airspot_slim_dfu.zip` through the mobile app's Slim
updater. The raw OTA file is
`application/build_production/application/zephyr/airspot_slim.signed.bin`.
Keep the same product signing key for subsequent builds; set `AIRSPOT_SIGNING_KEY`
if it is stored outside the ignored `application/keys/` directory.

- [Wireless updates, recovery and signing](docs/OTA.md)
- [Board wiring and qualification](docs/board/PRODUCTION_BOARD.md)

Build success is not hardware qualification. The qualification record distinguishes
observed behavior from checks still needing a phone, charger or extended testing.
Historical release packages and older design notes describe earlier hardware and
must not be used to program this product.
