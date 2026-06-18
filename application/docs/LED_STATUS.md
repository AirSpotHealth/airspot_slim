# AirSpot Slim — RGB LED guide

The Slim has no screen. The RGB LED shows air quality, charging, pairing, and calibration.

## CO₂ colours (double-tap or after charging white pulse)

| Colour | Meaning |
|--------|---------|
| **Green** | CO₂ below green threshold (default 800 ppm) |
| **Amber** | CO₂ between green and yellow thresholds (default 800–1000 ppm) |
| **Red** | CO₂ at or above yellow threshold (default 1000 ppm) |

Thresholds are set from the mobile app (**Settings → LED & charging guide**) via BLE command `0x07` and stored in NVS.

## Gestures

| Action | LED |
|--------|-----|
| **Single press** | Status: red if low battery, green if BLE connected, white if idle |
| **Double tap** | Current CO₂ colour for 5 s |
| **On Qi pad (debounced)** | White 3 s → CO₂ colour 5 s |
| **5× press while charging** | Factory-style reboot (white flash) |

## Other patterns

| Pattern | Meaning |
|---------|---------|
| **Blue flash** (short pulse every 5 s) | Manual calibration in progress |
| **Blue fast flash** | Pairing / advertising emphasis |
| **White slow flash** | Power-off hold while on charger (3 s hold remaining) |
| **Red flash** | Low battery while charging |

## Qi false “charging”

The Qi status line (P0.00, active low) can read “charging” at boot if the pin floats. Firmware debounces reads and enables pull-up. If the app still shows charging off-pad, refresh battery in the app and power-cycle the device.

## Calibration

Start from the app (**Calibrate Device**) with payload length 0 on command `0x0D`. Setting the target only uses `0x0D` with 2-byte payload (does not start calibration). Countdown frames use `0x0F`; completion uses `0x0D` with correction bytes.
