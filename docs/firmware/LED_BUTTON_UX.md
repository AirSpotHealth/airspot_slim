# LED & Button UX (v0.13.0)

Implements the device UX spec: light can be any colour but never stays on
(10 s max), and charger-gated gestures prevent accidental pocket activation.

## Gestures

| Gesture | Where | Action |
| --- | --- | --- |
| 1 press | anywhere | **Device status**, sustained 5 s: 🔴 red = low battery (fast red flash = critical), 🔵 blue = OK and paired, ⚪ white = not paired (open app to connect) |
| 2 presses (≤500 ms apart) | anywhere | **CO₂ readout**, flashes 5 s: starts in the last reading's colour, switches to the fresh reading's colour once measured. 🟢 < yellow_start (default 800 ppm), 🟠 amber zone, 🔴 ≥ red_start (default 1000 ppm) |
| Place on Qi charger | — | Same as a single press (device status, 5 s) |
| Hold 5 s | on Qi charger | **Power on / off** (soft off — see below). Light fades out when turning off; boot LED sequence plays when turning on |
| 5 quick presses (within 2 s) | on Qi charger | **Reset** (cold reboot) |

"Paired" = a phone has connected at least once (persisted in NVS key
`airspot/paired`, set in `ble_service.c` `connected()`, cleared by factory
reset). No pairing button is needed — the device always advertises when not
connected.

## Light rule

No continuous LED patterns exist anymore: the idle "breathing blue" was
removed, the lockout charging flash is capped at 5 s, and every indicator is
timed. The device never stays glowing.

## Soft power-off (hardware constraint)

True System OFF is impossible on the current board: the button is on **P2.10**
(gpio2 has no GPIOTE/SENSE), so nothing could wake the chip. "Off" is instead
a software state in `main.c` (`enter_soft_off()`): BLE, sensors and timers are
torn down, the LED stays dark, and only the 20 Hz button poll and Qi detection
stay alive. The power-on gesture (hold 5 s on charger) triggers
`sys_reboot(SYS_REBOOT_COLD)` for a clean start. Revisit System OFF when the
board rev moves the button to a P0/P1 pin (`CONFIG_AIRSPOT_POWER_OFF_ENABLED`).

## Button polling

`button_driver.c` polls at **20 Hz** (50 ms) with a 2-poll (100 ms) debounce.
The previous 5 Hz cadence could miss a quick tap entirely, and a registered
tap took ≥400 ms — longer than the 300 ms tap classifier — so double-tap and
the 5-press gesture could never fire. The hold gesture duration is
`CONFIG_AIRSPOT_BUTTON_LONG_PRESS_TIMEOUT_MS=5000` (prj.conf).
