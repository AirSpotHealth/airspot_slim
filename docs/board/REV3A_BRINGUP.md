# Rev3A firmware bring-up

Status: hardware startup and live sensor readings verified; not yet qualified for production. Branch
`feature/rev3a-nrf54l15` starts at main commit `4012c39`. The user requested
discarding the STCC4 test-branch edits; they are not part of this port.

## Build and inspect

From the repository root, with the installed NCS 3.2.1 toolchain:

```bash
source scripts/airspot
airspot build
airspot build --release
airspot build --rtt
airspot check
airspot check --release
airspot check --rtt
airspot memory --release
```

Rev3A is the default hardware. Builds are monolithic and use
`nrf54l15dk/nrf54l15/cpuapp` with the dedicated application overlay.
Each profile has its own `application/build_rev3a_<profile>` directory and
`rev3a-artifact.json`. Image names include `rev3a`. `application/build.sh`
delegates to this same path. MCUboot/DFU options and arbitrary image paths
are rejected on the Rev3A CLI. No release packages are published automatically.

`USE_CCACHE=0` avoids the broken Homebrew ccache installation observed here.
The artifact checker uses the NCS Python, including its existing IntelHex and
PyYAML dependencies. No system packages need changing.

The legacy build remains selectable with `export AIRSPOT_HARDWARE=legacy`
followed by `source scripts/airspot`. Return to Rev3A by exporting `rev3a` and
sourcing again. Never flash a legacy build onto Rev3A.

## Electrical mapping

Source: AH-013-0001-SCH Rev3A, approved 25-Jun-2026. MCU is nRF54L15-QFAA.

| Function | Rev3A connection | Firmware handling |
| --- | --- | --- |
| Button | P0.02, Q3 inverter | Active high, external bias; existing debounce/polling retained |
| Pressure interrupt | P2.10 | Existing pressure driver polls; no interrupt enabled |
| Flash HOLD / WP | P1.06 / P1.07 | GPIO hogs hold high before application startup |
| RGB | R=P2.08, G=P2.06, B=P2.09 | Existing service indices 1/2/0 already correct |
| I2C | SCL=P0.03, SDA=P0.04 | 400 kHz; external 4.7k pull-ups |
| Flash SPI | SCK=P2.01, MOSI=P2.02, MISO=P2.04, CS=P2.05 | 2 MHz; AT25XE161D, 16 Mbit (2 MiB) |
| Battery | Enable=P1.04, AIN1=P1.05 | Existing 0.6 divider scaling retained |
| UART | TX=P1.02, RX=P1.03 | 115200; NFC disabled |
| Qi status | P0.00, active low | External R19 pull-up; receiver status, not proof of battery charging |
| nPM ERR / CHG | P0.01 / P1.08 | Not yet monitored |
| nPM MODE / SHPACT | P1.09 / P2.03 | Leave schematic pull-down defaults; no shipping command |
| Qi EN1 / EN2 | P1.12 / P1.13 | Leave internal pull-down defaults (enabled with handshake) |

The historical `main.c` override of P1.11/P1.13 was removed. P1.13 is now Qi
EN2, so changing only the overlay would not have been sufficient. DK-only
buttons, fourth LED, PWM LED and MX25 flash are removed/disabled. DCDC and
crystal configuration currently inherit the NCS DK settings; crystal load
capacitance must be checked against the assembled crystal BOM during RF testing.

## Memory and wired programming

Baseline non-sysbuild builds had partition manager disabled: the YAML layout
was unused. Actual NVS was at `0x74000`, size `0x9000`. Rev3A preserves that
location and enables `CONFIG_USE_DT_CODE_PARTITION` to enforce:

- Application: `[0x00000, 0x74000)` (464 KiB).
- Settings: `[0x74000, 0x7d000)` (36 KiB).
- Remaining RRAM: unallocated, pending a future validated update layout.

For a connected, identified Rev3A test board:

```bash
export AIRSPOT_JLINK_SERIAL=603003276  # replace if using a different probe
airspot flash                       # debug profile
airspot logs uart /dev/cu.usbserial-AQ04QI7V 115200
```

Flash verifies the generated wiring/configuration, runner target and image
hashes, then uses the NCS J-Link runner with `nRF54L15_M33`, full HEX addressing,
reset and no mass erase. HEX data must match the BIN and remain inside the app
partition. There is no automatic recover/erase fallback. A protected or
previously provisioned target needs separate diagnosis. Artifact integrity
checks do not provide cryptographic firmware authenticity or identify the
physical PCB attached to a probe.

## Verification evidence (2026-09-05)

| Build | Flash used | RAM used | Result |
| --- | ---: | ---: | --- |
| Main baseline, nRF54L05 | 248,008 B | 59,896 B | Build passed after disabling broken host ccache |
| Rev3A UART debug | 245,520 B | 59,824 B | Build and artifact checks passed |
| Rev3A release | 198,540 B | 59,552 B | Build and artifact checks passed |
| Rev3A RTT | 245,172 B | 76,344 B | Build and artifact checks passed |

Release has `CONFIG_SERIAL=n` and `CONFIG_LOG=n`. Existing Kconfig assignments
still emit warnings around PRINTK and logging dependencies; do not interpret
this as a warning-free build or proof of low power. The UART device reference
in startup is now conditional, fixing the RTT/release link failure.

Eleven regression tests verify valid builds and rejection of wrong MCU/button/
flash pins, moved settings, an overlapping linker region, truncated HEX,
out-of-partition writes, a changed runner/ELF, and missing success metadata:

```bash
/opt/nordic/ncs/toolchains/322ac893fe/bin/python3 -m unittest discover -s tests -v
```

## Hardware results (2026-09-05)

The user confirmed probe 603003276 and FTDI AQ04QI7V connect to an unmodified
Rev3A board running on battery. FICR PART read `0x00054b15`. Before programming,
1,560,576 bytes of RRAM were backed up to
`local_backups/rev3a-20260905/before_port_rram.bin`, with size/hash metadata.
This local-only recovery copy is excluded from Git and normal build cleanup;
it does not include external flash or UICR.

J-Link programmed the UART build; a separate `verifybin` matched every image
byte. UART produced no captured output despite the port being released; this
connection still needs electrical diagnosis. RTT over SWD works, and the board
currently runs the RTT profile. Use `airspot logs rtt`; the helper resolves the
RTT control-block address from the built ELF. The RTT buffer was increased to
16 KiB so the host can attach without losing most of the startup log.

Real hardware exposed and verified these corrections:

- Blank flash took about 12 seconds to scan and repeatedly tripped the five-second
  watchdog. Feeding it as each sector scan progresses lets recovery finish and
  write metadata. A subsequent boot recovered that metadata successfully.
- STCC4 compensation commands require 1 ms execution time (datasheet sections
  3.4.4/3.4.5). Missing delays caused single-shot NACKs. After adding them, the
  board completed conditioning and reported 2862 ppm CO2, 26.9 C, 41% RH,
  1004 hPa and approximately 3.74 V battery. These are observed readings, not
  a calibration/accuracy certification. Failed CO2 reads mark samples invalid.
- Flash identity was `1F 46 0C`; the driver now rejects a wrong/missing part
  before exposing storage writes. Battery ADC errors no longer fabricate a
  voltage with an incorrect reference or wrap signed samples into high values.
- Button initialized on GPIO0 pin 2, released baseline; user observed LED blinks.
  BLE advertising started as `AirSpot-C58CC9`. Full mobile/gesture acceptance
  is pending user confirmation.

Runtime logs are local in `application/build_rev3a_backup/` and
`application/build_rev3a_rtt/rtt.log`. Early captures contain watchdog resets and
some retained/truncated RTT data from earlier attempts; use the current log for
latest behavior. The latest captured boot has successful conditioning, fast
history recovery and a live CO2 sample.

## Hardware acceptance still required

1. Repeat cold battery boot and wired updates; verify settings/history persistence
   and investigate the missing UART output.
2. Verify three sensors (SHT40 0x44, STCC4 0x64, LPS22HH 0x5D), flash identity,
   correct RGB colors and every button gesture.
3. Verify BLE pairing/reconnect and all supported app commands, measurements,
   history retrieval, calibration and settings after reset/update.
4. Check ADC readings against a meter over battery range; check acquisition time
   for the high-impedance divider. ADC error-path changes are compiled and normal
   readings work; dedicated fault injection remains pending.
5. Test charger attachment/removal, low battery recovery and power transitions.
   System OFF remains disabled until button/charger wake is verified. Do not
   mistake soft-off for shipping current.
6. Run flash interruption, sensor failure and watchdog tests, then a long soak.
   Wrong-part detection is implemented; verify its fault behavior on a test setup.

References: [Nordic SAADC reference](https://docs.nordicsemi.com/r/bundle/ps_nrf54l15/page/saadc.html-padc_reference),
[Zephyr nRF54L15 DK](https://docs.zephyrproject.org/latest/boards/nordic/nrf54l15dk/doc/index.html).
