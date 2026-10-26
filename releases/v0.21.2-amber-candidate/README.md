# Slim 0.21.2 amber candidate

Firmware source: a06b1bb plus VERSION patch bump to 0.21.2. App source: c25a2d0.

Includes continuous sensing, startup suppression and the amber LED correction.
Amber now drives green at 50% duty alongside steady red. The timer stops while
LEDs are off. Optical colour and battery cost still require hardware validation.

117 host tests passed before the version bump; the signed 0.21.2 build and
artifact tests passed after it. For the existing compatible Slim nRF54L15
MCUboot OTA layout only. Retain trial boot and firmware health confirmation.
No bootloader or signing keys are included. No app binary is included.
