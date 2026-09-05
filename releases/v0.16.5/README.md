# AirSpot Slim 0.16.5 — restore high-mode compensation on boot

Includes 0.16.4's one-second temperature/humidity/pressure compensation and
five-second CO2 reporting in High mode. Startup now applies the saved hardware
measurement mode as well as its interval. Switching from single-shot to continuous
operation reschedules compensation even when the configured interval is already
5000 ms. No BLE command is required to activate continuous operation after reboot.

Supersedes diagnostic candidate 0.16.4, which required reselecting High mode after
boot. Read-only command 0x36 reports the running mode and actual compensation
counts/gaps for qualification. Its payload is schema 1, continuous byte, then five
big-endian uint32 values: report interval, successful updates, failures, latest
gap and maximum gap (all intervals in milliseconds).

Settings and calibration are preserved during OTA. STCC4 internal ASC remains
paused, no FRC or factory reset is performed, and PWM remains off by default.
Compensation-only ticks do not feed software auto-calibration or publish CO2.
This fixes operating cadence; it does not establish CO2 measurement accuracy.

Validation: signed production build and all 47 current firmware tests pass.
Runtime coverage includes the actual main startup block for all saved power
modes, equal-interval hardware-mode changes, environmental input/write failures,
manual refresh, stop/reset, 1 s updates versus 5 s reports, and BLE encoding.
No Flutter code changed. Physical results are recorded in the investigation doc.

Image digest: `01c58af2019fb745a57811a107449a96d3325e2d3da54535fbff73ae19198ffb`.
