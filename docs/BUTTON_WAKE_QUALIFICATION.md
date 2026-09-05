# Button and System OFF qualification

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

The routine wired updater rejected this candidate before writing because its
rebuilt MCUboot bytes differ from the installed bootloader. A complete device
backup was captured and the unchanged device resumed. Application-only signed
OTA preserves the installed bootloader; this does not authorize overwriting an
unknown bootloader or bypassing the wired updater's layout guard.
