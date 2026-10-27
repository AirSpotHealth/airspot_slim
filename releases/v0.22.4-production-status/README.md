# Slim 0.22.4

Keeps auto-calibration enabled, including after upgrading a device with a saved OFF preference. Adds read-only first-hour settling progress for the mobile card. Includes the selected 6% green amber mix and 1 Hz compensation in both measurement modes. FRC preserves auto-calibration and restores the saved measurement mode.

Signed nRF54L15 application for compatible MCUboot DFU. Keep trial boot and health confirmation. Do not use as a bootloader image. Build and all 123 firmware tests passed; flashed onto wired Slim D30E3C on 27 October 2026 with full readback and preserved settings. Post-reset SWD header verifies 0.22.4. Bluetooth advertisement was unavailable during verification, so live sensing and boot-confirmation status were not verified. Battery qualification remains pending.
