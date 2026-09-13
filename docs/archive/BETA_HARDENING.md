# Slim 0.17.0 beta hardening — 6 September 2026

> Archived on 13 September 2026. Historical evidence and plans; experiment
> profiles and commands described here may have been retired. See the
> [current Slim status](../STATUS.md) for supported workflow and open findings.

**Hardware follow-up:** 0.17.0 was subsequently tested on the attached board.
That testing found occupied-slot preparation and interrupted sensor-conditioning
issues, corrected in **0.17.1**. See [hardware findings and results](BETA_HARDWARE_CHECK.md).
The original 0.17.0 artifacts below are historical, not the final candidate.

**Release verdict: NO-GO pending physical qualification.** This is a local candidate; focused hardware testing is recorded separately. Automated checks do not establish mobile OTA reliability,
energy consumption, flash durability or a completed soak.

## Baseline and decisions

Firmware baseline: `5d255a008c8a07f85c0b13b2935b6e462c3fab33`, application 0.16.9.
Mobile baseline: `a2e6f4f9466a3dcc113c1663501214c6aff861c4`.
Pre-existing firmware edits covered README/OTA documentation, production CLI,
provisioning and their tests; those were preserved. The original mobile checkout
was clean. Revisions, original diffs, configs, ELF, signed ZIP and artifact
manifest are saved under `local_backups/beta-hardening-baseline/`.

Target: nRF54L15, NCS v3.2.1, pinned Zephyr 4.2.99, Nordic toolchain 322ac893fe.
The same ECDSA-P256 product key and MCUboot swap-with-move layout are retained.
Baseline public-key SHA-256:
`d0611f49c7136eb88621d6212b6f1ba445b0997895b472c2eec15fd5ca5a3b25`.
Baseline image hash:
`3f594cde46f53137f86acd1b88b3a71d79ca5974ef36fb3ded2a7fdd18848998`.

Approved policies: unpaired BLE management; signed older images remain possible;
recovery while the application exposes BLE; no separate rescue bootloader/image.
If BLE never starts, wired recovery may still be necessary. Protect the private
signing key: there is no field key-replacement mechanism in this change.

## Findings and corrections

| Severity | Finding | Candidate behavior |
|---|---|---|
| High | Failed sensors blocked all management writes, including repair uploads | Confirmed firmware permits repair with a fresh safe battery reading; trial images reject remote writes until device confirmation |
| High | ADC failures returned old EMA voltage, and OTA used a sensor-cadence cache | Serialized OTA voltage refresh, maximum five-second age, failed ADC rejects update; lower of raw/EMA voltage guards falling supply |
| High | Main keepalives masked stuck workers; early startup lacked WDT coverage | Start WDT before drivers; latch expired main/system/protocol/OTA/sensor/readout deadlines before any renewal |
| High | Threshold pair and reset could persist partly or report success after failure | Atomic CRC-protected settings snapshot; failed writes leave RAM unchanged; reset acknowledges only completed operations |
| High | Corrupt settings h_set errors can be ignored by Zephyr loader | Preserve errors in h_commit; never silently overwrite newer corrupt settings with legacy defaults |
| High | Factory probes bypassed sensor ownership; failed mode transitions left saved mode incorrect | Probes share measurement lock; hardware mode accepted before persistence, restore on failed write |
| High | Production config disabled all external-history mutations | Normal candidate records history; forensic.conf retains explicit diagnostic-only mode |
| Medium | Native post-reset failure / later sensor failure lost installation context | Read-only confirmation recovery; distinguish observed rollback, unconfirmed/unavailable image, confirmed install with failed health |
| Medium | Partial/queued commands survived connection boundaries | Generation-bound RX/dispatch and command responses; in-flight accepted operations may finish, but replies retain original connection identity |
| Medium | A new history request could overwrite the address/identity of an active read | Atomic request snapshot, retained session for history replies, later request remains pending |
| Medium | No field-readable fault history | Bounded journal, retained warm-reset hints, read-only app export |

Calibration locking and correction range protection already existed in the
baseline and were retained. Mode start/restore errors now propagate through the
calibration path. Factory reset spans history erase and settings reset; **the
combined operation is not atomic**. Interrupted erase can remove some history;
the candidate no longer acknowledges success before completion or retries failed
erases continuously. Records attempted during an explicit erase can return busy.

## Recovery and watchdog contract

Hardware WDT remains five seconds. Before feeding, all armed deadlines are
checked; expiry latches even if a worker subsequently wakes. Main keepalives
renew only from the main owner; another worker cannot conceal a stalled main.

| Worker | Allowed progress interval |
|---|---|
| Main | 5 seconds; existing long main operations provide progress keepalives |
| System workqueue | 35 seconds, including approximately 22-second conditioning |
| Protocol command | 120 seconds while executing; idle command queue disarmed |
| MCUmgr command | 10 seconds during local command handling; phone silence is not a fault |
| Scheduled sensor | Next scheduled interval plus 35 seconds; on-demand/stopped mode disarmed |
| Button readout worker | 35 seconds while executing; otherwise disarmed |

Recovery time is the applicable deadline plus up to one hardware WDT period;
not every worker stall resets within five seconds. Failed sensor samples are
fault events, not proof of a stalled worker. A responsive confirmed application
can therefore accept repair OTA despite sensor failure.

Trial images still confirm after healthy initialization and a valid sample;
otherwise their 120-second startup deadline requests rollback. Their settings
writes are rejected until confirmation. WDT hardware setup failure prevents trial
confirmation. Initial cold-boot failure before the watchdog/MCUboot is operating
is outside the promised phone-only recovery boundary.

## Persistent interfaces

### Settings snapshot

NVS key `airspot/snapshot`: 49 bytes, schema 1, little endian. Four-byte schema
header; 21-byte NUL-terminated name; power, flight, five DND fields, buzzer,
vibrator; signed 16-bit correction; 16-bit target; auto-calibration and day count;
16-bit yellow/red thresholds; paired byte; CRC32-IEEE over first 45 bytes.

The snapshot is authoritative independent of legacy-key load order. Migration
validates all legacy values, and writes only from a confirmed image. Trial boot
preserves old keys so automatic rollback to the pre-migration image retains its
configuration. Explicit downgrade to pre-0.17 firmware reads the last legacy
values, **not subsequent snapshot changes**; qualify or prohibit such downgrades
operationally. Unknown/corrupt snapshots fail startup health but a confirmed
application with working BLE still permits repair. Ordinary settings writes stay
blocked after failed loading; deliberate factory reset can recover the snapshot.
No settings/history partition
addresses change.

### Diagnostic journal

Sixteen NVS keys `diag/00` through `diag/15`, each an independently written
28-byte record. Maximum one record per event code per boot (nine codes), queue
capacity 16, and no periodic telemetry writes. Failed storage stops persistence
retries for that boot; RAM records remain readable. CRC-invalid/truncated entries
are skipped and exported as gaps. Records share NVS without changing its capacity.
Repeated boot loops still write boot events; flash endurance under real reset
patterns remains a qualification item.

Record layout (big endian): schema/event/firmware-major/firmware-minor bytes;
firmware patch u16; reserved u16; sequence u32; uptime-seconds u32; detail u32;
reset-cause u32; CRC32-IEEE over first 24 bytes. Sequence saturates at UINT32_MAX.
Uptime is per boot, not a wall-clock timestamp. Codes 1–9 are boot, watchdog,
startup-failed, trial, confirmed, rollback-requested, sensor-failed, OTA-rejected,
and storage-failed. Watchdog detail bits follow the worker table order above.
Startup detail is `(subsystem << 16) | abs(errno)`; the first failing subsystem
is recorded. Subsystems 1–16: watchdog, LED driver, button, battery, Qi,
LED indicator, settings, power manager, BLE, protocol, sensors, history,
BLE mode, initial on-demand sample, scheduled-sensor start, saved-mode apply.

Validated `.noinit` data carries watchdog/rollback hints across supported warm
resets without writing flash in a fault context. This is best effort; power loss,
bootloader RAM use or firmware layout changes can invalidate it. Hardware reset
cause and trial/confirmation events remain independent evidence. A rollback
request does not prove MCUboot successfully reverted.

Read-only NUS command `0x37` uses the existing frame/checksum. Request payload:
index u8, expected newest sequence u32 BE (zero for first page). Response payload:
schema=1, status, index, count, newest sequence u32 BE, optional 28-byte record.
Status 0=record, 1=end, 2=not ready, 3=snapshot changed, 4=unreadable slot.
Exporter validates both transport checksum and record CRC, bounds count to 16,
and rejects a changing snapshot. Older firmware times out with a clear message.
The app's Slim status page exports JSON through the user-controlled share sheet.
Nothing is uploaded automatically.

## Performance and resource qualification

Baseline linker allocations: 269,052 program bytes (text+data), 86,284 RAM bytes
(data+bss). MCUboot RAM is reused after handoff, not added to application RAM.
Slots are 664 KiB each; usable payload is smaller because of image metadata.
Application-domain RAM is 188 KiB. These figures are not stack/heap peaks.

One sensor sample previously performed three battery conversions; it now derives
low/critical flags from its single reading. This removes duplicate conversions,
but no battery-runtime improvement is claimed without current measurements.
Production MTU, buffering, pipeline depth and connection defaults are unchanged.

`adaptive_ble.conf` is an explicit experimental candidate: 30–60 ms requested
during traffic, 60–90 ms after five quiet seconds, latency zero, four-second
supervision timeout. Requests are limited to three per mode, at least 30 seconds
apart. Phones may reject or choose different parameters. This follows Screen's
slow/fast approach without assuming identical optimal values on nRF54/iOS/Android.

`qualification.conf` adds stack high-water/CPU and system-heap free/allocated/peak
RTT reports every 60 seconds. The heap probe uses the pinned Zephyr system-heap
symbol; review it on SDK upgrade. Instrumentation changes CPU/RAM/power, so use it
for resource stress tests and disable it for baseline-versus-candidate energy runs.

Build the separate combined qualification image (never ship this as production):

```bash
source scripts/airspot
west build -b "$AIRSPOT_BOARD" -d application/build_qualification application --sysbuild -- \
  '-Dapplication_EXTRA_CONF_FILE=qualification.conf;adaptive_ble.conf' \
  "-DSB_CONFIG_BOOT_SIGNATURE_KEY_FILE=\"$AIRSPOT_PROJECT_DIR/keys/airspot-signing-key-ecdsa.pem\""
```

Measure each phone with the same signed package, radio distance, battery voltage,
sensing mode, history size and foreground/background workload. Record per-stage
OTA duration, reconnect duration, command/history latency, negotiated parameters,
transfer throughput, retries, disconnects and integrated charge/energy. Run at
least ten repeats per workload/profile and compare median/p95 and energy. Keep
baseline parameters wherever gains are not repeatable or battery behavior worsens.

## Physical release gates — all pending

- At least 20 successful signed update cycles on iOS and 20 on Android; record
  phone model/OS/app version, both image hashes, elapsed stages, confirmed hash,
  installed version, fresh sensor readings and settings/history checksums.
- Interrupt download and upload at start/middle/end; lose BLE, kill/background
  the app, cancel, reconnect, and retry without false success or a wired rescue.
- Cut power during upload, swap and confirmation on sacrificial qualified boards;
  record MCUboot recovery and failed-startup rollback. Test damaged/wrong images,
  full slot, low battery, stale/failed ADC, sensor/storage failures and reset cause.
- Inject worker stalls, verify deadline plus WDT bound, reject remote premature
  confirmation and prove repair upload still works with a failed sensor.
- Interrupt settings writes and factory/history erase; test full/wrapped history,
  retained calibration, charger transitions, sleep/wake and Screen coexistence.
- Capture stack/heap peaks during simultaneous BLE, history, sensing and OTA;
  demonstrate positive margin and no long-run growth/fragmentation.
- Complete a 72-hour soak with all sensing modes, repeated reconnects and history
  traffic. Preserve logs and fault exports; no unexplained reset/data loss allowed.
- Complete actual energy/current comparisons. No phone speed or battery-runtime
  claim is supported by the host tests or linker allocation report alone.

Automated results and final artifact identity are recorded in BETA_VALIDATION.md.
