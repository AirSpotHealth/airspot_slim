# AirSpot Protocol Specification

This document details the binary communication protocol used between the AirSpot Slim firmware and the mobile application via BLE Nordic UART Service (NUS).

## 1. Frame Format

All commands and responses follow this generic frame structure:

| Byte Index | Field    | Length | Description                            |
| :--------: | :------- | :----: | :------------------------------------- |
|     0      | Header 1 |   1    | Fixed `0xFF`                           |
|     1      | Header 2 |   1    | Fixed `0xAA`                           |
|     2      | Command  |   1    | Command ID (e.g., `0x01` for Get CO2)  |
|     3      | Length   |   1    | Length of the DATA payload only        |
|   4...N    | Data     |  Var   | Command parameters or Response data    |
|    N+1     | Checksum |   1    | Sum of bytes 0 to N (Low 8 updatebits) |

**Checksum Calculation:**
`Checksum = (Sum(Byte[0]...Byte[N])) & 0xFF`

> **Note:** All multi-byte integer values are **Big Endian** unless specified otherwise.

---

## 2. Command List

### **Realtime Data**

#### **Get Realtime CO2 Value (0x01)**

Requests the latest sensor reading.

- **Request:** `[FF][AA][01][00][CS]`
- **Response:**
  | Byte | Value | Description |
  |---|---|---|
  | 0-3 | Header+Len | `[FF][AA][01][0A]` (Len = 10) |
  | 4-7 | Time | Timestamp (Unix/Device time, uint32) |
  | 8-9 | CO2 | CO2 Concentration in PPM (uint16) |
  | 10-11 | Temp | Temperature × 100 (int16) (e.g., 2550 = 25.50°C) |
  | 12-13 | Hum | Humidity × 100 (uint16) (e.g., 5050 = 50.50%RH) |
  | 14 | Checksum | |

#### **Refresh CO2 Value (0x21)**

Triggers an immediate measurement and returns the result (Active polling).

- **Request:** `[FF][AA][21][00][CS]`
- **Response:** Same as `0x01` (Get Realtime CO2 Value).

#### **Get Battery Level (0x20)**

- **Request:** `[FF][AA][20][00][CS]`
- **Response:**
  | Byte | Value | Description |
  |---|---|---|
  | 0-3 | Header+Len | `[FF][AA][20][01]` |
  | 4 | Level | Battery Percentage (0-100); `0xFF` = unavailable |
  | 5 | Charging | `1` = Qi pad detected, `0` = not detected; not charge-current/full/fault status |
  | 6 | Checksum | (Note: Payload is 2 bytes but Len is usually 1 due to legacy bug) |

Also notified on pad-state changes, new NUS subscriptions and every 60 seconds,
independently of CO2 power mode. The lockout notification `0x46` uses the same
power fields with length 2. See [parity contract and qualification](../../docs/SCREEN_PARITY_FIXES.md)
for the unique `0x30` serial, unavailable pressure, and Slim live error codes.

#### **Get System Time (0xFE)**

- **Request:** `[FF][AA][FE][00][CS]`
- **Response:** `[FF][AA][FE][04][Time(4)][CS]`

---

## 3. Device Configuration

#### **Get Device State (0x08)**

Retrieves a full dump of device settings.

- **Request:** `[FF][AA][08][00][CS]`
- **Response:** `[FF][AA][08][Len][...Payload...][CS]`
  **Payload Structure:**
  - `[0]`: Reserved buzzer field (always 0)
  - `[1]`: Reserved vibrator field (always 0)
  - `[2]`: Power Mode (0=Normal, 1=Saver, etc.)
  - `[3-4]`: Yellow Threshold (e.g., 800)
  - `[5-6]`: Red Threshold (e.g., 1200)
  - `[7]`: Screen Const On (Legacy, unused)
  - `[8]`: Auto Calibrate Enabled (1/0)
  - `[9]`: Reserved DND field (always 0)
  - `[10-13]`: Reserved DND schedule (all 0)
  - `[14-15]`: Software Calibration Target
  - `...`: (Legacy/Unused fields for graph/alarms)
  - `[63-66]`: Legacy CO2 scale factor (fixed 1.0 float)
  - `[67]`: Reserved flight-mode field (always 0)

#### **Set Device Name (Alias) (0x0A)**

Sets the user-friendly name displayed in the app.

- **Request:** `[FF][AA][0A][Len][NameString...][CS]`
- **Response:** `[FF][AA][0A][01][01][CS]` (Success)

#### **Get Device Name (0x09)**

- **Request:** `[FF][AA][09][00][CS]`
- **Response:** `[FF][AA][09][Len][NameString...][CS]`

#### **Set Timebase (0x04)**

Syncs device time with phone time.

- **Request:** `[FF][AA][04][04][Time(4)][CS]`
- **Response:** `[FF][AA][04][01][01][CS]` (Success)

#### **Set Buzzer State (0x02)**

Unsupported on Slim. Ignored without saving settings or adding history records;
the unchanged device state is returned for legacy-client compatibility.

- **Request:** `[FF][AA][02][01][State(1)][CS]`
- **Response:** Triggers `CMD_GET_DEVICE_STATE` response.

#### **Set Vibrator State (0x03)**

Unsupported on Slim. Ignored without saving settings or adding history records;
the unchanged device state is returned for legacy-client compatibility.

- **Request:** `[FF][AA][03][01][State(1)][CS]`
- **Response:** Triggers `CMD_GET_DEVICE_STATE` response.

#### **Set Power Mode (0x05)**

- **Request:** `[FF][AA][05][01][Mode(1)][CS]`
- **Response:** Triggers `CMD_GET_DEVICE_STATE` response.

#### **Set Do Not Disturb (DND) (0x22)**

Unsupported on Slim. Ignored without saving settings or adding history records;
the unchanged device state is returned for legacy-client compatibility.

- **Request:** `[FF][AA][22][05][Enabled][StartH][StartM][EndH][EndM][CS]`
- **Response:** Triggers `CMD_GET_DEVICE_STATE` response.

#### **Set Flight Mode (0x32)**

Unsupported on Slim. Ignored without saving settings or adding history records;
the unchanged device state is returned for legacy-client compatibility.

- **Request:** `[FF][AA][32][01][Mode(1)][CS]`
- **Response:** Triggers `CMD_GET_DEVICE_STATE` response.

#### **Set BLE Disconnect (0x06)**

Forces the device to disconnect BLE.

- **Request:** `[FF][AA][06][00][CS]`
- **Response:** None (Connection dropped).

#### **Set Device Sleep (Ship Mode) (0xEE)**

Turns the device off completely.

- **Request:** `[FF][AA][EE][00][CS]`
- **Response:** `[FF][AA][EE][01][01][CS]` (ACK before shutdown)

#### **Reset Device (0x27)**

Reboots the device.

- **Request:** `[FF][AA][27][00][CS]`
- **Response:** None (Reboot).

#### **Factory Reset (0xFA)**

Slim 0.19.2 clears settings, history (including durable flash cursors), and
STCC4 calibration. Enables native ASC and continuous settling guidance, then
enters nPM1100 ship mode. Requires confirmed firmware, pad absent and button
released before erasure. Press the button to wake; this is not an MCU reboot.
A stuck pressure sensor does not gate the STCC4 erase. No Screen change.

- **Request:** `[FF][AA][FA][00][CS]`
- **Response:** `[FF][AA][FA][01][01][CS]` (erasures committed; power-off follows)

---

## 4. History Data (Data Logging)

#### **Get Current Write Page (0x0B)**

Gets the current half-page index where data is being written.

- **Request:** `[FF][AA][0B][00][CS]`
- **Response:** `[FF][AA][0C][01][Page(2)][CS]`
  > **Note:** Response uses command ID `0x0C`.

#### **Request History Page (0x0C)**

Requests a specific half-page of history data.

- **Request:** `[FF][AA][0C][02][Page(2)][CS]`
- **Response:**
  | Byte | Value | Description |
  |---|---|---|
  | 0-3 | Header+Len | `[FF][AA][0C][80]` (Len=128) |
  | 4-131 | Data | 16 Records (8 bytes each) |
  | 132-133 | Page | Page Number (uint16) |
  | 134 | Checksum | |

  **Record Format (8 Bytes):**
  - `[0-3]`: Timestamp (uint32 Big Endian)
  - `[4-5]`: Value (uint16 **Little Endian**)
    Firmware 0.17.2 corrects the flash-to-wire conversion to match this format.
    Earlier Slim builds transmitted the stored big-endian value without decoding
    it first, causing byte-swapped readings in clients using this wire format.
  - `[6]`: Type (uint8)
  - `[7]`: Reserved

#### **Erase History (0xFD)**

- **Request:** `[FF][AA][FD][00][CS]`
- **Response:** `[FF][AA][FD][01][01][CS]` (Sent when erase is complete)

---

## 5. Calibration & Sensors

#### **Set Self Calibration (0x11)**

Enables/Disables Automatic Self Calibration (ASC).

- **Request:** `[FF][AA][11][01][Enable(1)][CS]`
- **Response:** `[FF][AA][11][01][02][CS]`

#### **Set CO₂ LED thresholds (0x07)**

- **Request:** `[FF][AA][07][04][YellowStart(2)][RedStart(2)][CS]` (green/amber and amber/red boundaries in ppm)
- **Response:** Full device state (`0x08`)

#### **Start Manual Calibration (0x0D)**

- **Set target only:** `[FF][AA][0D][02][Target(2)][CS]` (does not start)
- **Start:** `[FF][AA][0D][00][CS]`
- **Response (Start):** `[FF][AA][0D][01][02][CS]`
- **Response (Done):** `[FF][AA][0D][01][Correction(2)][Sign(1)][CS]`
- **Countdown:** `[FF][AA][0F][01][Seconds(2)][CS]`

#### **Get ASC Count (Correction) (0x25)**

- **Request:** `[FF][AA][25][00][CS]`
- **Response:** `[FF][AA][25][01][Abs(2)][Sign(1)][CS]`

#### **Reset Sensor (0x24)**

Restarts the CO2 sensor module. From 0.19.0 this preserves the manual correction
as well as native ASC/FRC history. Use explicit 0x3A `RST` to erase calibration.

- **Request:** `[FF][AA][24][00][CS]`
- **Response:** `[FF][AA][24][01][01][CS]`

### Calibration recovery guidance (0x3C, firmware 0.19.0)

Read with LEN=0. Write with LEN=5: action u8 followed by the reset generation
u32 big-endian. Action 1 starts the fresh-air exposure; action 2 acknowledges
completion after 600 seconds of eligible measurements. A stale generation is
rejected. Unsupported action/stage returns an error, never advances progress.

The 27-byte response has LEN=22 and the following payload (all integers BE):

| Payload offset | Field |
| --- | --- |
| 0 | Schema 1 (u8) |
| 1 | Stage: 0 no tracked erase, 1 settling, 2 fresh air needed, 3 exposing, 4 user acknowledged, 5 erase outcome uncertain |
| 2 | Running (u8 boolean) |
| 3 | Reserved zero |
| 4 | Signed errno (i16); action error takes precedence over persistence error |
| 6 | Reset generation (u32) |
| 10 | Reset time in seconds since 2000 (u32), zero if clock unknown |
| 14 | Eligible settling seconds (u32, maximum 43200) |
| 18 | Eligible fresh-air exposure seconds (u32, maximum 600) |

The firmware owns progress in a separate CRC-protected settings record.
No tracked erase means unknown history, not calibrated. Stage 4 records customer
acknowledgement, not sensor accuracy or a successful internal ASC event.
See [recovery policy](../../docs/SLIM_SETTLING_GUIDANCE.md).

### Reset preferences (0x3D, firmware 0.19.0)

LEN=0 only. Reply LEN=2 contains signed BE errno. Reboot follows only a successful
save. Restores name/LED/pairing preferences while preserving sensor calibration,
calibration policy, sampling mode, readings and recovery guidance.

New mobile clients require 0.19.0 for the separated reset actions and never use
0xFA for an ordinary Slim preferences reset. The Factory Reset UI requires
0.19.2 and uses 0xFA. It remains explicitly
destructive, including sensor calibration and history, and starts recovery
guidance with native ASC and continuous operation enabled. 0x27 remains an MCU
restart. 0x3A `RST` deliberately erases sensor calibration without deleting saved
readings; it records durable intent before erasure and enables native ASC plus
continuous operation. Lost responses must be checked with read-only status,
not automatically retried as another erase.

#### **Get Sensor Variant (0x2B)**

- **Request:** `[FF][AA][2B][00][CS]`
- **Response:** `[FF][AA][2B][01][02][CS]` (02 = STCC4)

#### **Get Sensor Details (0x30)**

- **Request:** `[FF][AA][30][00][CS]`
- **Response:** `[FF][AA][30][10][...Details...][CS]`

---

## 6. Utilities

#### **Find My AirSpot (0x10)**

Blinks LEDs.

- **Request:** `[FF][AA][10][00][CS]`
- **Response:** `[FF][AA][10][01][01][CS]`

#### **Get Hardware Version (0x12)**

- **Request:** `[FF][AA][12][00][CS]`
- **Response:** `[FF][AA][12][Len][String...][CS]`

#### **Get Software Version (0x13)**

- **Request:** `[FF][AA][13][00][CS]`
- **Response:** `[FF][AA][13][Len][String...][CS]`

#### **Enter Factory Test (0xD0)**

- **Request:** `[FF][AA][D0][00][CS]`
- **Response:** `[FF][AA][D0][01][01][CS]`

#### **Get Factory Test Result (0xDD)**

- **Request:** `[FF][AA][DD][00][CS]`
- **Response:** `[FF][AA][DD][0A][Report...][CS]`

### Diagnostic history compatibility (0.17.4)

Slim now records Screen-style reset, first-clock-sync, calibration context,
flight, battery, sensor, DFU, SDK and fault events in the circular history.
Manual start is the ordered group `(14, 1), (13, old correction), (9, target)`.
Correction payloads retain signed 16-bit two's-complement bits; time-sync payload
is **unsigned** elapsed seconds modulo 65536, timestamped with the newly set clock.
Only the first clock sync after boot emits type 10. IDs 21 and 22 require the
mobile SDK-error/fault-reset mappings; old mobile versions treat them as empty.
See `docs/SLIM_HISTORY_RECOVERY.md` for event triggers and recovery limitations.

### Slim hardware scope (0.17.5)

Removed flight-mode, buzzer, vibrator and DND settings, their runtime APIs and
the flight-mode condition in software ASC. Slim uses LPS22HH pressure readings
for STCC4 compensation; it has no manual or automatic flight-mode state.
Shared state-frame slots and schema-1 snapshot offsets remain reserved, so
upgrades preserve the real settings and old app parsers retain correct offsets.
Old Screen-only snapshot fields are ignored and zeroed on the next settings save.
Record ID 19 stays reserved for shared Screen history compatibility; Slim does
not emit it. LED CO2 thresholds, software calibration and diagnostic history stay
supported. The 0.17.4 flight-history addition above is superseded by this cleanup.

### 0x38 — Native STCC4 auto-calibration policy (0.18.0+)

Request LEN=0 reads; LEN=1 with byte 0 or 1 pauses/enables sensor learning.
Other lengths/values return -EINVAL without changing policy. This is separate
from 0x11, which is the legacy host software-ASC command.

Response LEN=7: schema=1, mode (0=host/manual correction, 1=native baseline
held with ASC paused, 2=native ASC learning), verified (0/1), operation errno
(i16 big endian), sensor status (u16 big endian). Total frame length=12.
Verified means ready sensor and a successfully checked testing-mode policy,
not a calibration event or NVM save. Mode 2 requires testing flag 0x0080 clear;
other modes require it set. Hardware is verified before policy persistence;
failed persistence restores the prior policy. ON bypasses host offsets and
rejects manual target/calibration and FRC. OFF after ON retains the native
baseline and continues bypassing old host offsets until a new explicit manual
calibration successfully commits its correction. No FRC/factory reset occurs
when changing this setting. The reading rate is retained.

Native policy is stored in magic-tagged reserved bytes of the existing 49-byte
settings snapshot. Ordinary builds default to mode 0 for untagged settings;
native_asc_experiment.conf defaults to mode 2 for existing experiment units.
A saved native policy overrides either build's default. Older firmware may
ignore these reserved bytes, so rollback does not guarantee policy retention.

### Read CO2 Measurement Diagnostic (0x39, firmware 0.18.1+)

Read-only snapshot; no measurement, settings write, reset or calibration is
triggered. Request length must be zero. Response length 23, schema 1. Payload:
schema u8; flags u8 (1=CRC-checked signed raw available, 2=driver ASC-policy
valid, 4=normal measurement accepted); live fault u8; CO2-read errno i16;
original signed CO2 i16; sensor status u16; attempt sequence u32; MCU uptime
ms u32; supplied temperature centi-C i16; humidity centi-%RH u16; pressure
hPa u16. All multibyte fields big endian. Total frame 28 bytes with additive
checksum. Raw/status are meaningful only with flag 1. Before the first attempt,
sequence=0, errno=-EAGAIN and fault=STARTING. Sequence advances on normal
reporting attempts, not diagnostic queries. Native ASC policy response 0x38
and existing error frames remain unchanged. This exposes whether error 0x44
arises from a rejected zero/negative value, transport/CRC errno or policy
mismatch, without declaring any invalid value to be a valid CO2 measurement.

### Explicit native calibration history reset (0x3A, firmware 0.18.3)

Request payload must be exactly ASCII `RST` (three bytes). This separate command
stops measurement, sends STCC4 `perform_factory_reset` (0x3632), validates the
90 ms CRC-protected zero result, reapplies compensation and the selected native
ASC policy, resumes the selected sampling mode. Native modes retain the inactive host
correction without attempting a settings write; legacy host-offset mode clears
its active correction.
It erases native FRC/ASC history. It does not erase logged history or change the
selected ASC setting, and is never called automatically at startup or on a
calibration toggle. Existing device reset and 0x24 sleep/wake reset are unchanged.
Reply payload: signed 16-bit big-endian errno; zero means the reset sequence
completed, not that sensor accuracy or the new baseline has been validated.
The sensor reenables its initial bypass/warm-up after this operation; do not
interpret the initial fixed output as a successful accuracy comparison.


### Native reset failure report (0x3A, firmware 0.18.7 test image)

A zero-length request reads the last explicit native-reset attempt without
resetting anything. The original `RST` acknowledgement remains unchanged.
Reply payload (9 bytes): schema u8=1, operation stage u8, erase stage u8,
sensor result u16, operation errno i16, recovery errno i16; multibyte fields
are big endian. Operation stages: 0 none, 1 pressure initialization, 2 stop,
3 erase, 4 ASC verification, 5 settings, 6 sampling restore, 7 complete.
Erase stages: 0 not attempted, 1 command write, 2 response read, 3 CRC,
4 nonzero sensor result, 5 acknowledged zero result. Sensor result is meaningful
only at erase stage 4/5. The report is volatile and is retained across ordinary
sensor restarts, but not MCU reboots. A failed erase still attempts to restore
ASC policy and sampling; the original error is preserved even after recovery.
A successful recovery does not mean calibration history was erased.

## Per-measurement diagnostic history (unreleased)

The eight-byte history record and existing type IDs are unchanged. Type 23
remains reserved for the mobile app's persisted `empty` sentinel. New types are:

| ID | Value |
| --- | --- |
| 24 | Raw CRC-checked STCC4 CO2, signed int16 ppm |
| 25 | Applied software correction, signed int16 ppm |
| 26 | SHT40 temperature, signed int16 hundredths of Celsius |
| 27 | SHT40 humidity, unsigned hundredths of %RH |
| 28 | LPS22HH pressure, integer hPa |
| 29 | Display battery estimate, percent |
| 30 | Filtered battery voltage, mV |
| 31 | ASC policy: 0 software off, 1 software on, 2 native paused, 3 native learning |
| 32 | CRC-checked STCC4 status word, unsigned |
| 33 | Flags: bit 0 raw present, bit 1 driver accepted, bit 2 full sample accepted, bit 3 conditioning completed, bit 4 continuous operation, bit 5 selected mode present, bits 6–7 selected mode (0 on-demand / 1 low / 2 mid / 3 high) |
| 34 | Qi receiver active, 0/1 (not proof of battery charging) |
| 35 | Successful measurement mode change: 0 on-demand / 1 low / 2 mid / 3 high |

Each attempt is enqueued as a batch with one timestamp. Accepted samples include
the existing type-0 final CO2 record. Rejected attempts contain flags, policy,
power information and raw/status if CRC checked; they never contain a fabricated
valid CO2 reading or compensation inputs presented as fresh. Unknown battery
voltage (zero) omits both battery fields. Battery is the existing filtered display
estimate; the driver can retain its prior estimate following an ADC read failure.
Native sensor-internal calibration offsets are not exposed by these records.
Native paused means learning is paused, not that its learned baseline is erased.
Conditioning complete is the firmware startup state, not an accuracy or ASC
learning-completion guarantee. Reset/fault events retain their existing IDs.

The mobile CSV consolidates diagnostic records into columns on one row per
measurement. It starts with DateTime, Type and CO2 (ppm); event values/details
have separate columns, so calibration and mode changes cannot be mistaken for
CO2 measurements. Failed attempts and partial groups each produce one labelled
row. Native ASC policy changes without an explicit event are labelled with the
first reading where the change was observed, not an invented switch time.
The app joins by device and timestamp across pages, never carrying present-day
battery or settings into old readings. Conflicting same-second records leave
ambiguous diagnostic columns blank. Older firmware leaves unavailable fields
blank. Import reconstructs the measurement records from the new columns and
continues to support legacy CSVs. No firmware reflash or Isar/cloud schema change
is needed for the consolidated layout; the 0.20.0 wire format is unchanged.

A complete accepted sample uses 12 records / 96 bytes, compared with 8 bytes before.
The ~1.6 MiB history partition holds roughly 17,500 complete samples (about 36 days
at three minutes, or 24 hours at five seconds), less with other events. Downloads
and flash writes increase accordingly. The queue accepts/rejects each batch as a
unit and reserves space for events, but flash persistence is still record-by-record;
a power loss can leave a partial group, whose missing CSV fields remain blank.

For a continuous comparison use the existing High power mode (STCC4 continuous,
five-second reporting). Low mode uses three-minute single shots. Calibration policy
is separately recorded and should be held constant during comparisons. No new mode
command or automatic change to calibration policy is introduced.

Before release: install the updated app and firmware on a test Slim; compare CSV
raw/final/correction, mode changes and battery with live diagnostics; download
across page boundaries; exercise reconnect/power interruption and a sensor fault.
These checks require hardware and have not been established by host tests.

The CSV's `Measurement Mode` column names the selected cadence on each reading
and on timestamped `Measurement Mode Change` event rows. Failed requests and
requests for the already selected mode do not emit change events. `Sensor
Operation` separately describes continuous versus single-shot acquisition. Mode
bits are captured from the active sensor schedule under its measurement lock;
older flag records without bit 5 leave the selected-mode column blank.


### Native evaluation build 0.20.2: idle conditioning

With `CONFIG_AIRSPOT_STCC4_CONDITION_ON_BOOT=y`, this option now selects the
idle policy rather than unconditional conditioning at every boot. A missing or
corrupt clean-shutdown timestamp causes one startup conditioning cycle. Otherwise
normal sensing starts while the saved timestamp is held in RAM; `CMD_SET_TIMEBASE`
resolves the pre-startup idle gap using the app epoch and monotonic startup time.
Only a gap strictly greater than 10,800 seconds schedules conditioning. A delayed
connection does not count time already spent measuring as idle. Invalid/backward
clock values leave the decision pending. Subsequent time syncs do not repeat it.

The asynchronous transaction shares the native-calibration lease, stops sensing,
conditions for 22 seconds, restores the selected measurement mode, and publishes
a fresh reading. It does not send FRC or factory reset and does not change ASC.
Known single-shot/on-demand idle gaps are tracked with monotonic uptime; an unused
on-demand device is conditioned when another measurement is requested, rather than
repeatedly every three hours. Failed conditioning is retried with a 30-second delay.

The separate CRC-protected `conditioning/last` NVS key is invalidated before
startup measurements and written with the last successful sensor-read timestamp
on clean shutdown, provided valid app time was received and the decision resolved.
Unexpected power loss/reboot therefore becomes a missing timestamp on next start,
not a stale inferred idle gap. No per-reading flash writes are required. A build
upgrade has no such timestamp initially and conditions once. Pending/failed time
synchronization never silently substitutes an uptime value for Unix time.

This policy does not certify STCC4 accuracy. The separate 12-hour settling tracker
is customer guidance after an explicit erase; it is not an internal ASC-ready flag.
