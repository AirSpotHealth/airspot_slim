# AirSpot Slim production board

This is the only supported hardware target. Build and programming instructions
are in the [workspace README](../../README.md); memory layout, signing and update
behavior are in [OTA.md](../OTA.md). Diagnostics use SEGGER RTT.

Production is the target name, not a claim that every qualification test has passed.

## Electrical mapping

Source: AH-013-0001-SCH Rev3A, approved 25-Jun-2026. MCU is nRF54L15-QFAA.

| Function | Connection | Firmware handling |
| --- | --- | --- |
| Button | P0.02, Q3 inverter | Active high, external bias; GPIO SENSE interrupts with 20 ms debounce |
| Pressure interrupt | P2.10 | Existing pressure driver polls; no interrupt enabled |
| Flash HOLD / WP | P1.06 / P1.07 | GPIO hogs hold high before application startup |
| RGB | R=P2.08, G=P2.06, B=P2.09 | Existing service indices 1/2/0 already correct |
| I2C | SCL=P0.03, SDA=P0.04 | 400 kHz; external 4.7k pull-ups |
| Flash SPI | SCK=P2.01, MOSI=P2.02, MISO=P2.04, CS=P2.05 | 2 MHz; AT25XE161D, 16 Mbit (2 MiB) |
| Battery | Enable=P1.04, AIN1=P1.05 | Existing 0.6 divider scaling retained |
| UART | TX=P1.02, RX=P1.03 | Unused; diagnostics use RTT; NFC disabled |
| Qi status | P0.00, active low | External R19 pull-up; receiver status, not proof of battery charging |
| nPM ERR / CHG | P0.01 / P1.08 | Not yet monitored |
| nPM MODE / SHPACT | P1.09 / P2.03 | Leave schematic pull-down defaults; no shipping command |
| Qi EN1 / EN2 | P1.12 / P1.13 | Leave internal pull-down defaults (enabled with handshake) |

The historical `main.c` override of P1.11/P1.13 was removed. P1.13 is now Qi
EN2, so changing only the overlay would not have been sufficient. DK-only
buttons, fourth LED, PWM LED and MX25 flash are removed/disabled. DCDC and
crystal configuration currently inherit the NCS DK settings; crystal load
capacitance must be checked against the assembled crystal BOM during RF testing.

## Hardware evidence

The assembled board matches the schematic. Probe 603003276 is connected over SWD;
FICR confirms nRF54L15 with 1,524 KiB RRAM and 256 KiB RAM. The board runs on
battery. Local recovery snapshots, NVS copies and readback logs are kept under
`local_backups/`; none contain external history flash or UICR backups.

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

## OTA integration evidence

Final production build: application 263,708 B program memory / 86,024 B RAM;
MCUboot 32,124 B program memory / 19,872 B RAM. Bootloader and application execute
sequentially and reuse RAM. Fourteen host checks pass.


The MCUboot and application builds pass generated wiring, partition and signature
checks. Host tests cover tampered images, wrong keys/MCUs, incorrect offsets,
missing rollback, and settings preservation. Physical update results are recorded
below as each test completes.

- Signed initial installation: full RRAM readback matched, NVS relocated and
  preserved, auxiliary RRAM unchanged.
- GRTC boot handoff correction: application reaches sensor conditioning, NUS/BLE
  startup and live readings through MCUboot. Latest observed CO₂: 2,860 ppm,
  temperature 26.6 °C, humidity 40%, pressure 1,003 hPa, battery 67%.
- Valid trial update staged over J-Link: MCUboot installed it and the application
  logged `Firmware confirmed after startup and live sensor checks`.
- Failed trial recovery: a separately built test image (0.14.1, deliberately
  failing startup health) refused confirmation, hit its 120-second deadline and
  rebooted. MCUboot automatically restored 0.14.0; live sensor readings and BLE
  advertising resumed. The test image is not a release artifact.
- Final wired reinstall: readback verified, settings preserved and secondary slot cleared.
- Wireless update passed on 5 September 2026 using macOS Bleak/MCUmgr:
  263,859 bytes uploaded over BLE from production 0.14.0 to signed 0.14.1.
  The device rejected permanent confirmation of the inactive image (`rc=1`),
  accepted a test request, and rebooted through the BLE management command.
  RTT recorded a valid sample (2,464 ppm CO2, 26.7 C, 38% RH, 1,001 hPa,
  battery 66%) followed by automatic confirmation at 27.4 seconds after boot.
  A fresh BLE connection reported 0.14.1 active and confirmed, with 0.14.0 in
  the secondary slot. Image hash:
  `2f53fb3b2ada0e180bc20aa225d460de69159df55838e1b5bddbc1303a2ef7b4`.
  Local evidence and the tested package: `local_backups/wireless-ota-20260905/`.
- The initial Mac scan did not find the board despite its software advertising
  flag being set and no active connection. One debugger restart restored
  discovery; subsequent connections and the wireless reboot succeeded. The
  initial discovery failure remains unexplained and needs reconnect/soak testing.
  End-to-end testing through the Flutter/mobile updater remains pending.


## Hardware acceptance still required

1. Repeat cold battery boot and verify settings/history persistence across updates.
2. Verify three sensors (SHT40 0x44, STCC4 0x64, LPS22HH 0x5D), flash identity,
   correct RGB colors and every button gesture.
3. Verify BLE pairing/reconnect and all supported app commands, measurements,
   history retrieval, calibration and settings after reset/update.
4. Check ADC readings against a meter over battery range; check acquisition time
   for the high-impedance divider. ADC error-path changes are compiled and normal
   readings work; dedicated fault injection remains pending.
5. Test charger attachment/removal, low battery recovery and power transitions.
   System OFF GPIO wake is physically verified; see BUTTON_WAKE_QUALIFICATION.md.
   Current consumption and shipping-current qualification remain unmeasured.
6. Run flash interruption, sensor failure and watchdog tests, then a long soak.
   Wrong-part detection is implemented; verify its fault behavior on a test setup.

References: [Nordic SAADC reference](https://docs.nordicsemi.com/r/bundle/ps_nrf54l15/page/saadc.html-padc_reference),
[Zephyr nRF54L15 DK](https://docs.zephyrproject.org/latest/boards/nordic/nrf54l15dk/doc/index.html).

## Subsequent production audit

The 0.14.3 candidate received additional settings, input-validation, NUS lifetime,
calibration-input and battery-lockout fixes, plus sensor sleep on shutdown.
See ../PRODUCTION_AUDIT.md for complete fixed/open findings and hardware evidence.
Mac BLE upload/confirmation, two samples per mode and soft-off from continuous
mode passed. These checks do not establish battery life or production readiness.
