# AirSpot Slim application

Use the [workspace build commands](../README.md). This application requires the
nRF54L15 target and a sysbuild MCUboot build. Diagnostics use SEGGER RTT.

`VERSION` is the source of firmware version numbers for MCUboot and BLE reporting.
`prj.conf` is the production configuration. The application and bootloader share
the board overlay; Nordic's internal partition map supplies the two update slots.

See [OTA and recovery](../docs/OTA.md) for installation, signing and verification.
