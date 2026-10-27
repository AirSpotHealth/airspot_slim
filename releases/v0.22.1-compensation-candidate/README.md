# Slim 0.22.1 compensation correction

Temperature, humidity and pressure compensation update every second in BOTH modes. Continuous CO2 acquisition is every second; single-shot CO2 acquisition is every 10 seconds. Both report every 10 seconds. This replaces the incorrect single-shot compensation cadence in 0.22.0. Includes the other native ASC/FRC, startup and LED changes from 0.22.0.

Signed Slim nRF54L15 application candidate; use compatible MCUboot DFU with trial boot and existing health confirmation. No device flashed with this version during this task. Physical battery and calibration validation remain pending. The additional 1 Hz compensation work must be included in battery estimates.
