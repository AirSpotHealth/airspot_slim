# Automatic sensor recovery — 0.18.8 candidate

On 13 October 2026, AirSpot-2325D4 (0.18.7) repeatedly returned CO2 fault
0x44 and read errno -5 while ambient and pressure measurements continued.
The measurement worker advanced normally, so its watchdog correctly did not
restart the MCU. The retained first-failure event was approximately 12 October
08:38 Sydney. These observations localize the failed read path; they do not
identify an electrical cause or prove an uninterrupted fault since that event.

A separate five-second recovery worker now detects 30 seconds without a valid
sample after a fault. It runs in on-demand mode and after failed initialization.
Temperature/humidity faults reset SHT40; pressure faults reset/reinitialize
LPS22HH; CO2/compensation faults perform STCC4 sleep/wake and restore its saved
ASC and sampling policy. Failed manager initialization is retried. Conditioning
is not repeated during automatic retries once it has succeeded this boot.
Only a fresh complete measurement clears the fault. Calibration history, host
correction, settings and stored readings are retained; no factory reset is used.

Failed attempts wait 30, 60, 120, then 300 seconds between attempts. Automatic
recovery shares the calibration lease and measurement/mode locks. Shutdown
cancels both sampling and recovery before putting the sensor to sleep. This
repairs recoverable sensor state, not a guaranteed physical power interruption.
An electrically unresponsive sensor may still need a true power cycle.
Trial-image startup confirmation/rollback checks remain unchanged.

STCC4 measurement CRC failures now report -EBADMSG, separately from -EIO
communication failures. The persistent first sensor-failure event uses detail
0x53FF0000, where FF is the fault byte (e.g. 0x53440000 for CO2). The existing
journal records each event type once per boot. Recovery status is RAM state.

Read-only command 0x3B accepts no payload and returns 13 payload bytes:
schema u8=1, stage u8 (0 no attempt, 1 running, 2 fresh sample verified,
3 failed), triggering fault u8, signed errno i16, total attempts u32,
consecutive attempts u16, retry delay seconds u16. Multibyte fields are big-endian.
A normal successful measurement resets consecutive attempts/backoff. The last
attempt result remains available. Reboot resets these counters. Existing 0x39
provides the latest detailed CO2 read result.

The mobile app exposes Restart device in normal device settings. It uses the
existing 0x27 MCU reboot command, retains stored data, warns of temporary BLE
disconnection and interrupted calibration, prevents duplicate taps, and reports
only that a request was sent. Reconnection is not proof of sensor recovery.

Validation and on-device results are recorded with the candidate release.
