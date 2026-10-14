# Sensor failure followed by inability to power on

Investigation: 23 September 2026. Customer reports CO2 error 0x44, attempted
resets/calibration, successful off/on once, then no LED or power-on response.

## Scope and evidence

Tests run against the existing local working tree, application VERSION 0.17.7,
which contains pre-existing uncommitted changes. This is not verification of
an exact customer binary. No firmware was flashed or production code changed.

The new test `tests/test_sensor_failure_power_cycle_runtime.py` compiles the
actual sensor stop, main shutdown, wake qualification and post-sensor-init
startup blocks with AddressSanitizer and UndefinedBehaviorSanitizer. It injects
persistent -EIO for sensor sleep and initialization across two off/on cycles.
System OFF/reset, GPIO, BLE, scheduler and driver boundaries are simulated.

Results:

- Sensor sleep returning -EIO does not prevent shutdown.
- With a held button and charging=true, both simulated wakes are accepted.
- Sensor initialization returning -EIO marks startup unhealthy but execution
  reaches BLE advertising in both cycles.
- Charging=false rejects wake at about 310 ms in the simulated timing, before
  the LED fade starts. Holding longer cannot override this gate.
- An unavailable initial charger read also returns to System OFF.
- Existing tests cover permanent conditioning failure becoming fault 0x44,
  invalid sample rejection for calibration/history, wake debounce and watchdog
  worker deadlines. All seven selected tests passed.

Reproduce:

```sh
PYTHONPATH=tests python3 -m unittest \
  test_sensor_failure_power_cycle_runtime test_conditioning_recovery_runtime \
  test_wake_gate test_button_runtime test_sensor_callback \
  test_beta_recovery_runtime -v
```

This does NOT reproduce the complete customer fault. It demonstrates that
ordinary returned sensor errors alone do not explain an unwakeable device in
these code paths. It reproduces the logical dark/no-start outcome when charger
qualification fails. It does not establish that the customer's charger signal
failed. Physical rail faults, bus hangs, interrupt scheduling, bootloader timing,
and the full factory-reset/calibration sequence remain untested here.

## Current recovery limitations

- Five-second on/off and five-press reboot depend on the Qi status input.
- System OFF wake qualification occurs before sensors/BLE are initialized.
- Five presses call sys_reboot; despite the nearby factory-reset comment, this
  gesture does not itself erase settings or remove sensor power.
- All normal button events are ignored while calibration_service_is_active().
  Recovery should not depend on calibration finishing successfully.
- MCU reset/System OFF is not a cold sensor power cycle.
- A five-second watchdog exists; it cannot cure a persistent powered sensor
  latch-up merely by resetting the processor.

## Proposed recovery behaviour (not implemented)

1. Keep the normal five-second charger gesture. Add a deliberate, longer hold
   (candidate: 15 seconds) that accepts wake without Qi qualification. The wake
   gate must continue observing the button when Qi is false/unavailable instead
   of immediately sleeping. Release before the threshold returns to sleep.
2. Implement the same recovery gesture while running, independent of calibration
   and sensor mutexes. Avoid the existing five-second shutdown consuming the
   hold before the longer gesture can complete. Test this interaction explicitly.
3. Enter recovery with BLE diagnostics/OTA available, measurements invalid, and
   sensor startup optional/bounded. Keep battery voltage protection and OTA
   authorization. Preserve user settings and history. Give an explicit LED cue.
4. Treat this as a software emergency wake/reboot, not a guaranteed hardware reset
   when the CPU or its GPIO handling cannot execute.

## True power-cycle option

The existing Rev3A investigation in SLIM_SENSOR_DIAGNOSTICS.md records SHPACT
on P2.03/TP41 and the button connected to SHPHLD. Nordic specifies that nPM1100
ship mode disconnects the battery, requires VBUS absent and SHPHLD high, and
requires SHPACT high for at least 200 ms. Exit is by VBUS or SHPHLD low for at
least 200 ms. Source:
https://docs.nordicsemi.com/r/bundle/ps_npm1100/page/product_overview.html

A separate recovery action could therefore request removal from the charging
pad, wait for button release, then enter ship mode; the next button press could
restore power. This cannot be entered while the button keeps SHPHLD low or
VBUS is present. Board revision, rail discharge, debug back-power and safe GPIO
sequencing must be verified on a sacrificial bench unit before enabling it.
Do not assume nPM1100 implements another PMIC model's autonomous long-press reset.
For recovery from an entirely hung MCU, an independent reset/power supervisor
may be required; the software gesture is insufficient.

## Next physical reproduction

Use a test unit and the exact installed signed image/build configuration.
Capture UART and reset cause; disconnect debug probes for final OFF tests.
Use a dedicated, non-release fault-injection build that fails actual STCC4
transactions (not only the app value). Keep production builds free of injection.

Run the customer sequence with: persistent measurement failure, init failure,
sleep failure, bounded I2C timeout, and an actually stuck transaction. For each:
normal boot -> readings fail -> app reset -> calibration attempt -> off -> on ->
off -> on. Capture error code, battery mV, raw/debounced Qi signal, wake reason,
watchdog events, LED and BLE reconnection after each step. Repeat with Qi true,
false, toggling, and unavailable, and with calibration in progress. Verify both
sensor-fault and restored-sensor cases, and separate confirmed from trial images.

Acceptance: a failed sensor may prevent readings but must not permanently remove
access to recovery. A long emergency hold must work with a valid battery and
failed Qi qualification. Hardware sensor recovery requires measured rail collapse
and successful reinitialization; a host test cannot establish it.
