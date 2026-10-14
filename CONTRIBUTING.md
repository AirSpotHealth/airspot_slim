# Working on Slim

Use the [setup guide](docs/GETTING_STARTED.md) for a new checkout. Before changing
hardware behavior, check the [Rev3A board reference](docs/board/PRODUCTION_BOARD.md)
and the [current test status](docs/STATUS.md).

## Keep changes reviewable

Describe the problem, what changes on the device, and how you checked it.
Keep unrelated changes separate. If a protocol response changes, update the
[command reference](application/docs/protocol_commands.md) and its tests together.

For firmware changes, run:

```sh
/opt/nordic/ncs/toolchains/322ac893fe/bin/python3 -m unittest discover -s tests
source scripts/airspot
airspot build
airspot check
```

Record physical tests with the device ID, board revision if known, firmware
version, power source, steps and observed result. Say what remains untested.
A host test cannot establish sensor accuracy or charging behavior.

## Things that stay local

- Private signing keys and credentials.
- Device memory backups, raw customer readings and local bench captures.
- SDK downloads, build caches and editor files.

Use the existing product signing key through the agreed secure channel. Do not
replace it in order to make a local build pass. Old release bundles should only
be added deliberately, with their version, target board and test record.

## Reporting a problem

Include the device model and firmware version, whether it was charging, and the
shortest sequence that reproduces the issue. Attach relevant RTT lines with
secrets and customer data removed. Separate what you measured from what you
think caused it.
