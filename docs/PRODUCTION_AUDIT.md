# Production audit — 5 September 2026

Status: **not qualified for production release**. This is a source review and a
single-board qualification record, not a certification or a completed soak.
Baseline: e3d0c44 / signed 0.14.1. Baseline artifact/signature tests: 14 passed.
The baseline package, generated configuration and hashed artifacts are preserved
in `local_backups/audit-baseline-e3d0c44/`. See CLEANUP_INVENTORY.md for deletions.

## Corrected findings

| Severity | Evidence and impact | Correction / verification |
|---|---|---|
| High | `user_settings.c` accepted short reads; a truncated DND record copied uninitialized stack bytes into live settings | Stage records, require exact lengths/read counts, validate mode/boolean/time fields. Native fault-injection tests compile the real C implementation with address/undefined sanitizers |
| High | Most scalar/name/DND setters changed RAM before a failed NVS write | Publish only after successful persistence; injected -EIO leaves previous values unchanged |
| High | `main.c` fed failed/cached CO2 samples into calibration and returned before battery lockout | Validate before calibration, handle battery independently; actual callback tested with failed samples and charging/noncharging combinations |
| High | NUS notify used a connection pointer concurrently released on disconnect | Protect pointer/CCC state and retain a connection reference for each send; native test injects disconnect during notify and verifies reference lifetime; hardware reconnect qualification required |
| Medium | DND command accepted four payload bytes and consumed checksum as end-minute | Require all five payload bytes; reject invalid hours/minutes in settings |
| Medium | Protocol accumulated impossible declared lengths until buffer overflow reset | Reject lengths exceeding the receive buffer immediately; validate complete frame length before dispatch. Native tests exercise real RX code across every split of a frame, concatenated frames, maximum size and oversized-header recovery |
| Medium | STCC4 compensation failures did not invalidate samples; mode commands raced measurements | Propagate compensation failure into validity, serialize mode switches with measurement mutex |
| Medium | Mobile sleep acknowledged success but called a disabled System OFF branch | Route it to main's existing soft-off path; preserve charger/button wake gesture; remove unreferenced old gesture implementation |
| High | Stopping periodic work left STCC4 continuous mode running during soft-off/lockout | Cancel work synchronously and command sensor sleep; wake uses existing reboot path; shutdown from continuous mode verified over NUS/RTT; current remains unmeasured |
| Medium | Drivers forced DEBUG despite production INFO selection | Use CONFIG_AIRSPOT_LOG_LEVEL consistently; retain RTT |
| Medium | NVS load errors were logged but startup reported success | Return failure so trial-image health confirmation does not accept failed settings startup |

## Open release findings

These require follow-up; they are not waived by the successful build or OTA test.

- **High — persistence transactions:** threshold pairs use separate NVS writes;
  factory reset deletes keys individually and can report success after errors.
  Power interruption can leave a mixed configuration. Introduce a versioned
  atomic settings record with tested legacy migration, or equivalent recovery,
  before claiming atomic configuration/factory-reset behavior. Scalar fixes do
  not solve transactions or concurrent multi-field snapshots.
- **High — calibration lifecycle:** start/countdown/sample accumulation run from
  different contexts without a common state lock; completion ignores some
  persistence errors and signed correction arithmetic can exceed int16 range.
  Needs lifecycle serialization and explicit failure reporting compatible with
  the app, including zero-valid-sample and interrupted-calibration tests.
- **High — sensor ownership:** factory-test driver calls bypass the measurement
  mutex. Mode-transition failures can still leave the stored requested mode
  different from actual hardware operation. Unify ownership and propagate
  transition failures before qualifying simultaneous factory/refresh/mode work.
- **High — watchdog coverage:** main feeds the watchdog independently of sensor
  and protocol workers. A stuck worker need not trigger reset. Add worker health
  deadlines with fault injection; a functioning main loop is insufficient proof.
- **High — OTA/access policy:** signed images prevent unsigned execution, but
  management remains unpaired and permits nearby reset/upload and older signed
  images. Authorization/anti-downgrade product policy remains unresolved.
- **Medium — BLE session isolation:** incomplete RX frames/queued work are not
  explicitly associated with connection generations. Audit reconnect boundaries
  and commands already executing when a connection ends. Initial discovery once
  required a debugger restart; root cause remains unknown.
- **Medium — timing/power:** periodic work schedules after measurement completion,
  so observed spacing includes measurement duration. Main wakes at 100 ms,
  button at 50 ms, and soft-off remains polling. No measured current savings or
  runtime claims are justified yet. System OFF/wake and crystal loading remain
  unqualified.
- **Medium — history:** metadata has CRC and data-log operations use a mutex, but
  interrupted erase/program, metadata wrap, full capacity and fault recovery
  have not been comprehensively injected on hardware.
- **Medium — charging:** Qi presence is not proof of cell charge completion.
  Charger status, voltage thresholds, ADC accuracy, brownout, attach/detach and
  battery aging/temperature need physical qualification.

## Compatibility and mobile workload

Legacy command IDs, packed state fields and the battery response's historical
length quirk are preserved. No app behavior was changed. The current Flutter
communicator discovers SMP/NUS, subscribes to notifications and already logs
sent command bytes. Widget/live-activity/manual refresh paths issue 0x21;
auto-sync/history flows can add traffic. A source review does not establish
actual traffic frequency: record the real phone/app workload before endurance.

## Evidence limits

The 0.14.1 baseline passed Mac Bluetooth upload/test/reset/confirmation and wired
failed-startup rollback, as recorded in board/PRODUCTION_BOARD.md. Neither test
qualifies the Flutter updater, interrupted transfers or every production fault.
New-image hardware results are recorded separately after execution. No profiler
capture or full battery rundown has been completed by this audit.

## Audit candidate hardware checkpoint

Signed production 0.14.3 (code through d69f516) installed from 0.14.1 by Mac
Bluetooth upload/test/reset. It confirmed at 27.7 seconds after boot following
a valid live sample. A fresh SMP connection reported active/confirmed image
`7bca199a319459c3a28173a23e6c9db616883753dd670e84091aab60278483de`.
Baseline 0.14.1 remains in the secondary slot. Local evidence/package:
`local_backups/audit-final/`. Intermediate staged candidates were not activated.

Twenty-two host tests pass. Native C tests use address/undefined sanitizers and
fault-injectable OS/storage/BLE boundaries; they do not replace hardware faults.
Malformed mode, short DND and oversized frame recovery passed over BLE; battery
and history-position replies remained compatible. The initial test runner
expected history response 0x0B; source inspection confirmed legacy 0x0C, and the
runner was corrected without changing firmware. Scheduled-mode results and
shutdown evidence are appended after completion.

The connected Mac smoke test passed all three modes with two valid scheduled
notifications each: 5.070 s, 60.645 s and 180.619 s observed spacing. No
disconnect/reset occurred during that sequence; mode 1 was restored. This is
short functional evidence, not a soak or a phone-app endurance measurement.

Shutdown check passed from mode 3: NUS sleep ACK, STCC4 stop/sleep messages,
advertising suppression and BLE disconnect. A debugger reset was used to resume
operation; this does not qualify the physical charger/button wake gesture.
Battery runtime, interrupted-update faults and the actual mobile-app tests
remain pending. No production-readiness declaration is made.
