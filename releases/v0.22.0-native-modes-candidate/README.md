# Slim 0.22.0 native modes candidate

Two modes, both reporting every 10 seconds: single shot every 10 seconds or continuous acquisition every second. Native ASC default on; native FRC available with ASC enabled. No host offset or host calibration averaging. Includes startup bypass suppression, first-hour white ticks and the 0.21.2 amber correction.

121 firmware host/artifact tests passed. Production wiring, MCUboot layout and signed application verified. Matching Flutter 0.22-aware controls are required. No device has been flashed with this candidate and it is not published. Physical battery, optical LED and reference-air calibration validation remain pending.

See ../../docs/slim-022-native-modes.md for migration, timing, known limitations and battery test procedure. Use only with compatible Slim nRF54L15 MCUboot devices; retain trial boot and health confirmation. No signing keys or bootloader are included.
