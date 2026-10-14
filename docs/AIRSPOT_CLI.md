# AirSpot command

One command routes to the existing Slim, Screen and ESP32 Hub helpers, the web
projects' package.json scripts, and a read-only Bluetooth monitor. Each firmware
helper runs in its own subprocess so sourcing it cannot replace the global
AirSpot command or leak its toolchain environment into another component.

## Everyday use

```sh
airspot                          # interactive component menu
airspot scan                     # discover nearby AirSpot BLE devices
airspot cli                      # coloured scan / select / connect menu
airspot connect                  # alias for the device menu
airspot watch BE56FF 81A9DC     # selected devices; IDs or full names
airspot slim                     # Slim action menu
airspot slim connect             # device menu, Slim filter
airspot screen connect           # device menu, Screen filter
airspot hub                      # Hub action menu
airspot hub-10 ports              # existing Hub-10 serial-port helper
airspot server                   # Server action menu
airspot server dev               # run its actual dev script
airspot map dev
airspot hub-web dev
airspot dashboard dev
airspot mobile                   # Flutter action menu
airspot help
```

`airspot <component> path` prints the project directory; `shell` opens a shell
there. Available actions come from the existing helper or package.json. The two
Hub Web directories are separate components: `hub-web` and `hub-web-ui`.

`airspot cli` opens **1. Scan devices / 2. Exit**. Scanning shows devices as
advertisements arrive for eight seconds. Once complete, use **↑/↓** to move,
**Space** to toggle selections and **Enter** to connect. **Esc/Q** returns to
the menu; **R** rescans. Connections reuse the discovered Bluetooth devices.
From live readings, **Esc/Q** disconnects and returns to the picker, where you
can change selections and reconnect. Ctrl+C also goes back safely. The terminal
is restored on exit. `airspot watch` remains available for scripts.

Monitoring displays CO2, battery, mode, Auto Calibration state, reading age and
connection status. Screen and Slim live frames differ; the monitor validates
checksums and represents the 0xFFFF fault sentinel as an error rather than
65,535 ppm. Missing readings remain blank. Disconnected readings retain their
age and connection status; they are not presented as fresh measurements.

Disconnect the phone app before connecting from the Mac. Devices must be
powered on, within Bluetooth range, and advertising. Discovery defaults to
eight seconds; use `--scan-seconds 20` if a device is not found. Type-specific
monitoring reads the sensor variant and disconnects devices of the other type.
Variant 0/1 identifies the current SCD40/SCD41 Screens; variant 2 identifies the
current STCC4 Slims.

The monitor reads state and makes one CO2 refresh request (0x21) on connection,
then subscribes at the device's configured cadence. It refreshes battery state
every 30 seconds and retries dropped connections. It never writes sampling,
calibration, timebase, history or reset settings. Ctrl+C disconnects. No phone
or firmware changes are required.

```sh
airspot watch --duration 60 --json > readings.jsonl
airspot watch --scan-seconds 20
```

JSON timestamps use the Mac's local timezone and notification arrival time.
Device clocks can differ, so their timestamps are not used to calculate age.
Sampling rates differ: a one-minute device naturally updates less often than a
five-second device. Watching faster does not change the sensor's mode.

## Setup on another checkout

Requires Python 3.10+, Bluetooth, and macOS Bluetooth permission for the terminal.
From `app_firmware`, run:

```sh
./airspot setup                   # creates firmware/airspot_slim/venv/live
./airspot install                 # installs wrappers, zsh block and direnv auto-load
```

`setup` installs the pinned Bleak dependency in a project-local virtual
environment. `AIRSPOT_LIVE_PYTHON=/path/to/python ./airspot setup` selects another
Python interpreter. Installation creates `~/.local/bin/airspot`, `~/airspot/airspot`
and appends a marked block to `.zshrc`; unrelated existing launchers are preserved.
When direnv is installed, installation also adds and approves a managed block
in the parent `airspothealth/.envrc`. With the existing direnv shell hook,
entering `airspothealth` or any subfolder loads the command automatically;
no manual `source` is needed. New terminals also have the global command through
the managed zsh block. Without direnv, open a new terminal after installation.
Existing project-local scripts remain
available directly. `AIRSPOT_ECOSYSTEM_ROOT` can override the parent directory
containing the Hub and web projects.

The web mappings are `server → airspot_server`, `map → airspot-map`,
`dashboard → airspot-dashboard`, `hub-web → airspot-hub-web`, and
`hub-web-ui → airspot-hub-web-ui`. The router does not implement Hub BLE pairing;
Hub commands delegate to their existing ESP-IDF helpers.

Validation: eight parser/dispatch/command-safety tests, eight existing firmware
CLI tests, interactive menu navigation, safe helper routing, and real live BLE
readings from AF94D2, BE56FF, F1D7C1, 81A9DC and 2325D4.
