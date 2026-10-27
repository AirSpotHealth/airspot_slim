# Recovery-aware OTA confirmation — 0.22.9

## Failure observed on AirSpot-3B2AE0

The 0.22.8 journal recorded sensor initialization failure (startup stage 11,
I2C error -5), followed by sensor recovery activity. A live 0x39 response from
the running 0.22.8 trial showed valid CO2/pressure (flags 7, fault 0). The next
SMP image read showed confirmed 0.20.1 active and 0.22.8 inactive/unconfirmed:
the device had rolled back despite producing readings before the deadline.

The old aggregate startup flag permanently included the first sensor failure.
Later valid samples could not clear it. A green CO2 LED is not proof that an
image is confirmed; Bluetooth availability is also not proof of sensor health.

## Fix

- Keep essential non-sensor startup failures blocking confirmation.
- Record sensor startup failures separately. Require successful sensor
  initialization and selected-mode configuration, then a fresh valid sample.
  A successful runtime recovery can satisfy these gates.
- Clear the sample gate when another invalid sample arrives, so an old healthy
  sample cannot mask a subsequent fault before confirmation.
- Preserve signed-image verification, device-only confirmation, settings and
  the 120-second maximum trial window. A healthy image confirms immediately
  when its gates pass. A reset while unconfirmed can still trigger rollback.

The app now distinguishes reconnecting from sensor checks, shows the remaining
verification time, and exits immediately when an observed trial is followed by
the confirmed previous image. Individual Bluetooth reads and polling delays
are bounded by a monotonic overall verification deadline.

## Validation

- All 125 firmware tests pass, including recovered sensors, persistent sensor
  faults, stale healthy samples, and non-sensor startup failures.
- All 199 mobile tests pass, including the actual signed 0.22.9 package.
  Static analysis reports no issues.
- Normal Bluetooth DFU trial on 3B2AE0 upgraded confirmed 0.20.1 to 0.22.9.
  No remote confirmation or permanent-install bypass was used.
- Retained journal: transient sensor read failure at uptime 18 seconds,
  device confirmation at uptime 24 seconds. Host polling observed confirmation
  about 40 seconds after its reset request; that includes polling latency.
- Fresh post-update diagnostic sample: flags 7, fault 0, CO2 700 ppm,
  pressure 1011 hPa. Sensor recovery status was idle with no recorded attempt.
- A normal MCU restart then resumed fresh accepted samples: CO2 799, 769 and
  758 ppm, pressure 1011 hPa, flags 7, fault/error 0, advancing sample counters.

Signed image hash:
`45878bd76ce8cb9213c8d5731a3d7b28d5c8728f194985d7ac43e11f4c6cae93`.

Local hardware evidence is retained in
`local_backups/slim-3b2ae0-confirmation-0229/`. This verifies the trial on the
identified device; it does not certify every device in the fleet or remove the
need to investigate persistent sensor communication failures.
