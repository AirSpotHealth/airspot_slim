# Slim app parity fixes — local candidate

16 September 2026. Applies to the current working source, not a published or
hardware-qualified release. Existing in-progress history/calibration work is
preserved. Automatic-calibration controls remain hidden in the Slim app.

## Power reporting

The normal main loop checks battery voltage every 60 seconds regardless of CO2
mode, including on-demand and sensor failure. Charger-state changes and new NUS
subscriptions trigger a check immediately on the next main-loop iteration.
Battery readings use the serialized ADC path with a maximum five-second cache;
charging status always comes from the debounced Qi driver, not a sensor sample.
ADC failure retries after five seconds; failed notifications retry after one
second. Reconnect detection includes the NUS connection generation.

Both BAS and the app's NUS `0x20` path receive periodic updates. `0x20` preserves
its historical length byte of 1 despite two payload bytes. Percentage `0xff`
means unavailable; the app displays unknown without discarding pad state.
Low-battery lockout `0x46` uses the same fresh power source before BLE teardown.
Normal monitoring is absent from soft-off/lockout loops; their existing low-power
behavior remains in control.

The power flag means **Qi pad detected**, not proof of charge current, full charge,
or absence of charger faults. nPM CHG/ERR inputs are not monitored by this change.
The app retains the bolt indicator and uses accurate Slim-specific tooltip/guide
wording. Screen retains its existing charging label.

## Identity and cloud data

`0x30` now returns the chip's factory-programmed 48-bit address in the existing
six-byte serial slot, big endian. This value is independent of alias, NVS,
phone BLE identifiers, OTA and factory reset. It is not a truncated chip hash.
Invalid/unprogrammed identity returns zero bytes rather than a shared fallback.

The app rejects `534C494D3031` (the former ASCII `SLIM01`), zero, all-ones and empty
cloud identities before claim/upload. Old Slim firmware must be updated and
reconnected before cloud sync. Cloud uploads select records by the local BLE
device ID before substituting the unique cloud serial; syncing one device cannot
select another's history. Concurrent per-device syncs have separate guards.
Upload/claim errors are propagated rather than reported as successful syncs.

Existing cloud data under the placeholder cannot be automatically assigned to a
physical device. Migration procedure:

1. Preserve/export existing placeholder rows and ownership records for audit.
2. Update each device and capture its new serial together with its local BLE ID.
3. Use independently attributable phone records/logs to establish provenance.
4. Only with that evidence, perform a separately reviewed server migration or
   per-device historical reimport, verifying ownership and timestamp/type conflicts.
5. Leave ambiguous mixed records quarantined. Never give the entire placeholder
   history to the first upgraded Slim.

No cloud records, ownership, or local synced flags were changed by this task.

## Faults, metadata and app controls

Slim live error frames still use CO2 `0xffff`, with error cause in byte 4:

| Code | Cause |
| --- | --- |
| `0x40` | Starting/not ready |
| `0x41` | I2C initialization |
| `0x42` | Temperature/humidity |
| `0x43` | Pressure |
| `0x44` | CO2 measurement/status |
| `0x45` | Compensation/configuration |

Byte 5 is zero; the app does not apply Screen's three-attempt recovery semantics
to Slim. Invalid samples remain invalid and cannot be logged as valid CO2.
`0x30` pressure is zero/unavailable before a valid finite sample; stack contents
can no longer leak into this field. The metadata UI labels zero as unavailable.

Locate now exposes the existing five-second white-LED command with Slim-specific
instructions. Power-off explains the Qi-pad plus five-second hold needed to wake.

## Pressure recovery limits

The driver retries unreadable startup identity at most three times, 50 ms apart.
It sets and verifies `IF_CTRL.I3C_DISABLE=1` after BOOT and volatile reinitialization,
following [ST sections 7.5 and 9.4](https://www.st.com/resource/en/datasheet/lps22hh.pdf).
Explicit Reset Sensor now also resets/reinitializes pressure configuration before
clearing the CO2 correction. Failed pressure recovery does not clear calibration.

This handles transient startup availability and restores I2C configuration. It
does **not** establish the cause or cure of the previously observed sensor that
remained inaccessible until a longer physical power removal. An I2C write cannot
repair a sensor already inaccessible in I3C-only mode. No interrupt-output pin
was driven and no speculative shared-rail power cycle was added.

## Verification and hardware acceptance

Host tests cover charger edges, subscription/reconnect, failed-send retry, ADC
failure, on-demand low-battery detection, identity stability/separation, invalid
metadata, fault classification, pressure retries/configuration, battery parsing,
and cloud batch isolation. The production build checks signed artifacts and board
wiring. Executed against this candidate:

- 71 firmware host tests passed using the Nordic toolchain Python.
- 123 Flutter tests passed, including parsing the newly built signed DFU ZIP.
- Flutter analysis passed with no issues; both repositories pass `git diff --check`.
- Production firmware build, signing and board-wiring checks passed (0.17.5).
- Native iOS build remains blocked during CocoaPods installation: the installed
  xcodeproj library cannot interpret the existing project's objectVersion 70.
  The pre-existing native project edits were preserved.

These checks do not qualify the physical device or native app release.

Still requires physical qualification before rollout:

- Attach/remove Qi while connected in every power mode; check app indication
  promptly, repeat after reconnect and while CO2 sensing has failed.
- Leave on-demand mode idle while lowering supply through the lockout threshold;
  verify the notification and teardown, then charge through recovery.
- Observe Qi state at full battery and pad misalignment; verify the app's wording
  matches receiver detection rather than claiming actual charge current.
- Verify two units retain distinct serials through reboot, OTA and factory reset.
- Exercise Locate and power-off/wake on iOS and Android.
- Repeat the documented pressure supply-interruption failure. Capture bus/power
  evidence if it persists; host tests are not evidence that this fault is cured.

## Local device installation: 0.17.6

The fixes were rebuilt as 0.17.6 to distinguish them from the previously installed
0.17.5 image. On 16 September 2026, probe 603003276 was flashed at the user's
request. Full RRAM backup and byte-for-byte readback verification passed; settings
were preserved from 0x15c000. Runtime logs confirmed firmware 0.17.6 booting.

Image hash: `aa248a378e5bf1bf5e3c4d7ecfbb86554031cdef39cc45cb5a979f683cf44165`.
Backup: `local_backups/production-000603003276-20260916T134318460316Z/`.
No cloud firmware publication was performed. App charger-transition verification
and the physical qualifications listed above remain separate from successful flashing.


## Advertising lifetime fix: 0.17.7

17 September 2026: the connected Slim's application flag reported advertising,
but the Zephyr advertiser had BT_ADV_ENABLED cleared and no connection. A reset
made AirSpot-81A9DC briefly visible to an independent BLE scan.

Root cause: assigning BT_DATA_BYTES inside ble_service_init stored a pointer to
a block-scope compound literal in the global advertising array. After return,
that stack byte was overwritten. Its limited-discovery bit could become set,
causing Zephyr to schedule CONFIG_BT_LIM_ADV_TIMEOUT (30 seconds) and silently
stop the advertiser. This was separate from the app's stale saved-device filter.

The flags literal now lives at file scope. The regression test compiles actual
advertising initialization with AddressSanitizer stack-use-after-return checking:
the previous source fails, the fix passes. All 72 firmware host tests and the
signed production build passed.

0.17.7 image hash:
`7b899d585381ad335e369a40583f8079503331f3dec3ddd18dfcbeabeac60823`.

Installed 0.17.7 on probe 603003276 with verified readback and preserved settings.
Backup: `local_backups/production-000603003276-20260916T140822554066Z/`.

On-device verification: independent CoreBluetooth scan repeatedly saw
AirSpot-81A9DC over 50 seconds from first detection (beyond the old 30-second
cutoff), including BAS and SMP service UUIDs. The phone subsequently connected,
enabled NUS notifications, synchronized time, read sensor details/device state,
and issued Find My Device successfully. These observations establish discovery
and app communication for this flashed unit; wider hardware qualification remains.
