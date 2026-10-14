# Slim 0.18.9: complete factory reset and retained fault evidence

## Reset semantics

The app's complete Factory Reset requires 0.18.9+. Earlier 0xFA implementations
cleared saved readings and settings but did not erase the STCC4 learned baseline.

On confirmed firmware, 0xFA now:
1. Runs the explicit STCC4 factory calibration erase and verifies its result.
2. Erases device reading/event history.
3. Saves factory settings, including zero manual correction and default ASC policy.
4. Acknowledges success, then reboots. Volatile software calibration state is lost.

A failed step returns failure and does not reboot or clear app data. This is not
an atomic transaction across sensor and flash: an earlier successful erase cannot
be undone if a later step fails. The reset can be retried. Trial firmware remains
blocked. This does not reinstall firmware or change the hardware identity. The
separate service fault journal is retained for diagnosis, including reset failures.

Reset Sensor with ASC on uses 0x3A RST: erase learned history and retain ASC on.
With ASC off it uses 0x24: retain learned history and clear manual correction.
Automatic recovery and MCU restart never erase learned calibration.

## Diagnostic journal

The existing read-only 0x37 journal and app JSON export now support these events:

| Code | Event | 32-bit detail, most significant field first |
| --- | --- | --- |
| 10 | I2C failed | address u8, operation u8 (1 write, 2 read, 3 write/read), signed errno i16 |
| 11 | Sample failed | sensor fault u8, stage u8 (1 compensation, 2 CO2 sample), errno i16 |
| 12 | CO2 context | sensor status u16, raw signed CO2 i16 |
| 13 | Recovery result | original sensor fault u8, stage u8 (2 recovered, 3 failed), errno i16 |
| 14 | Factory reset failed | reserved u8, stage u8 (1 sensor/trial gate, 2 readings, 3 settings), errno i16 |
| 15 | Sensor power | battery mV u16, flags u16 (bit 0 charging, bit 1 raw CO2 available, bit 2 single shot) |

Records also include firmware version, boot reset cause, uptime, sequence and CRC.
Each event type is recorded at most once per boot; the journal retains 16 records.
This deliberately captures first evidence without writing to flash every sample.
Recovery result is the first attempt's result, not necessarily the final state.
The live 0x3B response remains authoritative for latest recovery state and counters.

I2C evidence can describe a transient that later succeeded on retry. Scan NACKs
are excluded. An I/O error does not identify wiring, power, sensor firmware or
silicon as the cause. CRC errors (-EBADMSG), invalid range (-ERANGE), and policy
mismatch (-EACCES) are distinguished in sample evidence. Raw CO2/status are not
valid unless the power-context raw-available flag is set. Compensation failures
are recorded where they occur so a subsequent CO2 read cannot overwrite errno.

Journal persistence is asynchronous and best effort: abrupt power loss before
storage completes or storage failure can lose new records. Factory reset retains
these service records, while ordinary device readings and user settings are erased.
