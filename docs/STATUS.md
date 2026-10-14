# Firmware status

**0.19.2 candidate · updated 14 October 2026**

`main` contains the latest Slim development work. There is no customer release
for this candidate yet. The supported hardware is the nRF54L15 Slim Rev3A board;
the SDK is pinned to nRF Connect SDK v3.2.1.

## Checked so far

| Check | Result |
| :--- | :--- |
| Host regression suite | 108 tests passed on 14 October |
| Firmware build | MCUboot and application built successfully |
| Artifact checks | Signatures, board configuration and memory layout passed |
| 81A9DC update | Final 0.19.2 image booted and confirmed after startup and live sensor checks |
| 81A9DC factory reset | Erasures acknowledged; history erase logged; J-Link measured 0.000 V after power-off |
| Button wake after that factory reset | Pending |
| History pointer and settling state after that wake | Pending |

The first factory-reset device test, on 0.19.1, hit a watchdog reset. The fix
moves the operation to the main thread and feeds the watchdog after each erased
flash sector. The final 0.19.2 test completed without that restart.

[Factory-reset implementation and test record](SLIM_FACTORY_RESET_POWER_OFF.md)

## What changed

- Sensor faults have bounded recovery attempts. A fresh complete measurement is
  required before a fault clears.
- Calibration erasure starts persistent settling guidance. Ordinary restarts
  retain calibration; full factory reset deliberately erases it.
- History erasure clears the saved flash cursor as well as the RAM cursor, so
  startup cannot reload an old page position after a successful erase.
- Full factory reset clears settings, readings and calibration, then disconnects
  battery power electronically. The user presses the button to wake the device.

[Recovery](SLIM_AUTOMATIC_RECOVERY.md) · [Settling](SLIM_SETTLING_GUIDANCE.md) · [History erase](SLIM_HISTORY_ERASE_FIX.md)

## Still open

**Pressure sensor fault.** 81A9DC previously remained stuck during LPS22HH boot.
Removing battery power recovered it. Electronic power-off and button wake were
subsequently demonstrated on a healthy sensor, but the original fault has not
been reproduced and cleared by that method. Its trigger is still unknown.

**CO₂ agreement.** The differences between the Slims and the nearby Screen have
not been fully explained. A working driver and valid CRC do not establish
measurement accuracy, and the Screen has not been established as a calibrated
reference for these tests.

**Field qualification.** Broader phone/update interruption testing, extended
runs in every sampling mode, and battery/current measurements remain open.
Sleeping STCC4 between single shots is an optional qualification build and is
disabled by default.

**Update access.** Firmware is signed, but the existing BLE management flow is
unpaired. See the [access boundaries](OTA.md#signing-and-access) before making
security claims about deployment.

Older test results remain in the [archive](archive/README.md). Their firmware
versions and hardware conditions matter; they are not blanket approval of the
current source.
