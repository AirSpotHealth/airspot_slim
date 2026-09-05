# Button and System OFF qualification

Current result: physical System OFF wake is verified on the single test board.
Production configuration enables it starting with 0.14.6. The chronological
notes below describe earlier candidates and disabled defaults at those stages.

The production button is active-high P0.02. Candidate 0.14.4 replaces periodic
button polling with GPIO SENSE edge interrupts and 20 ms debounce on a dedicated
workqueue. Native tests compile the driver with GPIO/kernel stubs and sanitizers;
all 23 host tests pass. Signed build and artifact checks pass.

System OFF code is present behind `CONFIG_AIRSPOT_POWER_OFF_ENABLED`, which remains
disabled pending physical qualification. Shutdown waits for button release, then
arms active-level wake with interrupts locked. On a confirmed-image GPIO wake,
startup requires the existing charger plus five-second hold before starting
sensors/BLE. Short or off-charger presses return to System OFF. A held wake button
is ignored until release to prevent a second power-off gesture.

Qualification order:

1. Install interrupt-only candidate; verify single tap, double tap, five-second
   charger hold, release, and existing software power-on gesture using RTT.
2. Build a distinct signed candidate with System OFF enabled. Verify shutdown,
   short-press rejection, off-charger rejection, and sustained charger-hold wake.
3. Repeat with the debugger detached; verify low-power wake reset cause after
   reconnecting. Debugger-attached sleep is insufficient evidence of true OFF.
4. Check BLE, live sensing, stored mode/history, and continued held-button behavior
   after wake. Measure current separately when instrumentation is available.

Candidate 0.14.4 was installed by signed Bluetooth upload/test/reset. RTT recorded
successful P0.02 SENSE interrupt setup, a valid sensor sample, and automatic image
confirmation at 27.676 seconds. SMP subsequently reported the candidate active
and confirmed (image hash
`f3b7b0ba95a386bd0f78e41e9d5c958b15fccabbf6f10314e53c988552b51f0c`).
Artifacts and update logs are in `local_backups/button-qualification/`.
Physical gesture results are pending. Neither current consumption nor battery
runtime has been measured for this change.

Follow-up review found that the wake gate ran before Qi's initial asynchronous
debounce, so the initially false cache could reject a valid charger hold. The gate
now waits up to one second for that first reading. A native test of the actual
wake-gate function covers delayed readiness, timeout, release, off-charger holds,
and release during the LED ramp. All 24 host tests pass. A separate build with
`CONFIG_AIRSPOT_POWER_OFF_ENABLED=y` links and signs successfully; it has not been
installed. The board still runs the interrupt-only 0.14.4 candidate above.
RTT observed two physical press/release pairs; LED response and double-tap
qualification still require user confirmation.

Subsequent RTT evidence confirms a physical double-tap at uptime 3:35, followed
by a manual measurement and `Double tap: new reading CO2 2466 ppm` at 3:37.052.
This verifies interrupt delivery through gesture recognition and sensor readout;
the visible LED response has not yet been confirmed. The six-second charger-hold
test has been requested and remains pending.

The user reported no charging LED after the charger-hold attempt. RTT recorded
a 7.51-second hold without charger detection or a power-off event. Read-only
J-Link inspection found P0.IN = 0x19 (P0.00 high, inactive Qi status). P1.12 and
P1.13 PIN_CNF both read 0x02: inputs, disconnected input buffers, no MCU pulls.
The schematic specifies internal receiver pull-downs and EN1/EN2 = 0/0 for
enabled handshake. The MCU therefore is not driving these pins to disable Qi.
This does not prove receiver power transfer or exclude a status-line hardware
fault. Pad power/alignment and receiver operation need physical confirmation
before the charger-gated wake gesture can be qualified. Register-read logs are
preserved in the qualification backup directory. System OFF remains disabled on
the installed image.

Qi later asserted CHARGING at uptime 11:09.730. The following physical hold
generated `Power OFF gesture` at 11:43.081, sensor sleep at 11:45.322, and stopped
BLE advertising. The next charger hold caused the software power-on reboot at
12:19.188; BLE, valid sensing, and charger detection resumed. These qualify the
existing software-off gestures with interrupt-driven input, not System OFF.
The earlier inactive Qi observation therefore is not evidence of a permanent
receiver failure; its cause has not been established.

System OFF candidate 0.14.5 includes the initial charger-read fix. Its signed
image hash is `09f6195333c31597d5430a70fbf7eb3dfffb2bd1e0ddf78c11d0512cf78ac66e`.
The separate qualification build has System OFF enabled; the default production
configuration remains disabled. Both builds and all 24 host tests pass. Physical
debugger disconnection has been requested before activating the qualification
image. Its artifact manifest and package are preserved with `system-off-` names
in the qualification backup directory.

Both debug adapter serials disappeared from the Mac USB tree before the OTA
test/reset commands. Bluetooth SMP subsequently reported 0.14.5 active and
confirmed with the qualification hash above. The System OFF image is now
installed on the test board; this is not a completed wake qualification or a
change to the default production configuration. The physical shutdown hold,
short-press rejection, sustained charger-hold wake, and low-power reset-cause
verification remain pending.

## Physical System OFF result

With debug adapters removed before activation, the user performed the shutdown
hold and reported the board stayed dark. A short tap on the charger stayed dark;
a six-second hold off the charger also stayed dark. A 30-second BLE observation
during rejected-wake testing received no device advertisements. These checks do
not measure current or independently prove every rejected transition.

The subsequent charger hold brought the board back. The user held for a further
six seconds after the light cue. Bluetooth confirmed the same active image; NUS
returned persisted mode 1, a valid battery response, history-position response,
and fresh CO2 of 2535 ppm. After reconnecting J-Link, buffered RTT showed:

- `Reset cause: 0x00000080 LOWPOWER_WAKE` at 3:34.056.
- `GPIO wake gesture accepted` at 3:39.070.
- Valid CO2 at 3:39.860 and button release at 3:46.062, without another shutdown.

This is evidence of actual GPIO wake from System OFF and suppression of a held
wake button until release. The debugger was reattached only after Bluetooth
operation resumed. The clock timestamp persists across this reset; relative
hold timing is used, not an assumption that uptime starts at zero.

Evidence: `system-off-wake-rtt.log`, `airspot-deep-wake-status.log`,
`airspot-deep-wake-smoke.log`, and rejected-wake scan logs in the qualification
backup directory. Current consumption, long endurance, low-battery cutoff and
the wider production audit remain unqualified. A history-position reply alone
does not qualify full history integrity. The user's final LED description was
a green indication; exact visual fade shape has not been instrumented.

The routine wired updater rejected this candidate before writing because its
rebuilt MCUboot bytes differ from the installed bootloader. A complete device
backup was captured and the unchanged device resumed. Application-only signed
OTA preserves the installed bootloader; this does not authorize overwriting an
unknown bootloader or bypassing the wired updater's layout guard.
