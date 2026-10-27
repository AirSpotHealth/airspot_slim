# Slim 0.22.3 selected amber

Full red, 6% green, blue off: mix B selected by the user on wired Slim D30E3C. Production uses 192 microseconds green on / 3008 microseconds off, with 625 timer callbacks per second only while amber is lit. No busy-wait in the normal indicator. The bench comparison used a different carrier; verify normal flashes on hardware before declaring production optical validation complete.

Includes 0.22.1 compensation at 1 Hz in both measurement modes. Signed nRF54L15 Slim application for compatible MCUboot DFU. Retain trial boot and health confirmation. Do not use as a bootloader image. Physical battery qualification remains pending.
