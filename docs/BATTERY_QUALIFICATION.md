# Battery qualification

No runtime result is available yet. Optimize for the best practical runtime;
there is no fixed days-per-charge requirement. Preserve measurement behavior.
The schematic lists 75 mAh nominal; this is not measured usable capacity.

| Mode | Firmware mode | Sensor operation | Current | Runtime |
|---|---:|---|---|---|
| 5 seconds | 3 | STCC4 continuous | Not measured | Not measured |
| 1 minute | 2 | STCC4 single-shot | Not measured | Not measured |
| 3 minutes | 1 | STCC4 single-shot | Not measured | Not measured |

## Preparation and traffic characterization

1. Finish active functional/OTA qualification before occupying the only board.
2. Record signed image hash, board identity, cell label/condition, phone model,
   OS/app version, room temperature and enabled app features.
3. With RTT attached for this preliminary check, connect the real app, keep its
   normal device screen foregrounded and capture its existing command logs.
   Record refresh/history traffic and disconnects. Do not enable extra polling.
4. Verify modes 3, 2, 1 via the app and observe at least two scheduled samples per
   mode. Record actual spacing; configured delay is not necessarily start-to-start
   cadence. Verify battery response and history sync without erasing user data.

## Full rundown — one board, sequential runs

Test 5 seconds, then 1 minute, then 3 minutes, using the same final firmware.
Before each run fully charge the cell using the supported charger and verify
charge completion independently of Qi-presence indication. Remove the charger,
UART and debugger. Keep the phone powered and app foregrounded. Do not manually
refresh, use LEDs, flash firmware or change settings during a run.

Record UTC events in a CSV outside tracked source:

```csv
timestamp,event,interval_s,notes
```

The first row must be `start`, interval 5/60/180, with identifying conditions in
notes. Subsequent events may be `sample`, `connected`, `disconnected`,
`observation`, `low_battery_shutdown`, `interrupted` or `crash`. Use ISO timestamps
with timezone. Existing app notifications or external observation supply events;
this file is an observation record, not a tool that polls or connects to the board.
Log gaps and the last successful reading. Preserve raw app logs separately.

End only on observed low-battery lockout/loss of useful operation. A dropped BLE
link alone is not a cutoff. If firmware changes, charging resumes, the app is
terminated, or the experiment stops, mark interrupted. Mark a diagnosed crash
as crash, not battery exhaustion. Include uncertainty between last valid sample
and observed cutoff in the notes. Incomplete runs establish only elapsed time.

```sh
python3 scripts/battery_report.py local_backups/run-events.csv
```

The report summarizes observed elapsed hours/days and flags incomplete or crashed
runs; it never converts battery percentage into predicted runtime. It cannot
verify the truth of manually recorded events. Review raw evidence before calling
a result an always-connected endurance measurement. Record disconnect duration
and app interruptions alongside the report.

## Later profiler session

Identify the instrument before choosing wiring/ranges. Measure battery-side
current including regulators, sensors, flash and LEDs; avoid debugger/UART
back-power. Use a supported cell-voltage range and adequate transient capture.
Profile the same app workload in every mode: cold startup, idle/connected BLE,
sampling, history transfer, LED interaction, OTA, soft-off and lockout. Measure
across representative battery voltages. Keep existing firmware behavior first;
compare changes against the same workload and configuration.

Integrate charge/energy over complete cycles, reporting measurement duration,
range, voltage, sample rate and instrument limits. Estimate days from measured
usable capacity and average load, with explicit capacity/temperature assumptions;
validate against full rundown. Do not mix debugger-attached traces with final
battery-runtime claims. One cell/board is single-unit evidence, not batch life.

## Automated firmware smoke test (not an endurance measurement)

With the phone disconnected, run in a Python environment containing `bleak`:

```sh
python tests/hardware/ble_smoke.py --name AirSpot-C58CC9
```

This changes modes, checks malformed commands and receives two valid samples per
mode. It restores the original mode on normal exit or a connected failure. If
the connection fails before restoration, restore the original mode through the
app before continuing. The full sequence takes roughly nine minutes. It checks
Mac interoperability; it does not reproduce the real app workload or measure
battery life. History-position command 0x0B intentionally responds with 0x0C,
and the legacy battery/history-position length quirks are preserved.

## Functional cadence measured on 5 September 2026

0.14.3, Mac BLE client, RTT attached: two valid samples per mode, observed
spacing 5.070 s / 60.645 s / 180.619 s. All three passed; the original 3-minute
setting was restored. No battery runtime or energy measurement was performed.
Evidence: `local_backups/audit-final/ble_smoke.log`.
