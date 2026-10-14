# Building AirSpot Slim

The firmware targets the AirSpot Slim Rev3A board with an nRF54L15. The build
uses the Nordic DK target plus the Slim board overlay; it is not a DK wiring map.

## What you need

- Access to this private repository.
- nRF Connect SDK **v3.2.1** and its toolchain, including `west`.
- The authorized AirSpot product signing key.
- A powered Slim and SEGGER J-Link for wired programming or RTT logs.

The checked-in helpers currently expect the macOS toolchain at
`/opt/nordic/ncs/toolchains/322ac893fe`. That path is set in
[`scripts/airspot`](../scripts/airspot); it is not automatically discovered.
The programming helpers also use Nordic `nrfutil` and SEGGER's J-Link tools.

## First checkout

Clone into a new directory outside any existing west workspace:

```sh
git clone https://github.com/AirSpotHealth/airspot_slim.git
cd airspot_slim
source scripts/airspot
west init -l application
west update
source scripts/airspot
```

`west init` is only needed once. The manifest is
[`application/west.yml`](../application/west.yml); SDK dependencies are downloaded
into the workspace and are not committed. The second `source` loads the Zephyr
environment after the dependencies exist.

## Signing key

Use the existing product key, obtained through the team's secure channel:

```sh
export AIRSPOT_SIGNING_KEY="/absolute/path/to/airspot-signing-key-ecdsa.pem"
```

The default location is `application/keys/airspot-signing-key-ecdsa.pem`, which Git
ignores. Do not generate a replacement just to get a build through: existing
bootloaders trust the corresponding product public key. The build checker
compares the artifacts against the public key stored in this repository.

## Build and check

```sh
airspot build
airspot check
```

Both run locally without programming a device. Outputs are in
`application/build_production/`:

| File | Purpose |
| :--- | :--- |
| `airspot_slim_dfu.zip` | Bluetooth update package |
| `application/zephyr/airspot_slim.signed.bin` | Signed application for an SMP client |
| `firmware-artifact.json` | Version, hashes and signing-key identity used by the checker |

## Connect a device

Use the [board wiring reference](board/PRODUCTION_BOARD.md) and the Rev3A
schematic when connecting SWD. Then:

```sh
airspot devices
airspot flash
airspot logs
```

With several probes attached, choose one explicitly:

```sh
airspot flash --serial SERIAL
airspot logs --serial SERIAL
```

The flash helper creates a local memory backup, preserves the current settings
partition and verifies readback. Logs also go to
`application/build_production/rtt.log`. Press Ctrl+C to stop following them.

For older settings layouts, recovery and the exact memory map, use the
[wired installation guide](OTA.md#initial-wired-installation).

## Tests

From the repository root:

```sh
/opt/nordic/ncs/toolchains/322ac893fe/bin/python3 -m unittest discover -s tests
```

The suite exercises firmware logic on the host and checks build artifacts.
It does not prove sensor accuracy, battery life or behavior during a physical
power interruption. Those results belong in the [test status](STATUS.md).
