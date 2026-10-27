# Slim 0.22.4 calibration and first-hour status

Auto-calibration is always enabled in the current Slim product profile. Boot
normalizes an older saved OFF preference to native learning without erasing
the sensor baseline. Native policy changes and the settings setter reject
disable requests; the legacy software calibration setting remains unavailable.
Forced recalibration still temporarily uses continuous sensing and restores the
saved measurement mode. It leaves automatic calibration enabled.

The first-hour white indicator is separate from the 12-hour recovery workflow.
Read-only command 0x3E takes an empty payload (request FF AA 3E 00 E7). Its
11-byte response contains the standard 4-byte header (payload length 6), schema
version 1, active byte (0/1), big-endian uint32 remaining seconds (0..3600), and
the additive checksum. Reads take the sensor mutex for a coherent snapshot.
Only successful sensing contributes to progress; phone elapsed time is never
used. Completion persists; a sensor reset restarts the interval. Unpaired
devices report inactive. Completion does not certify accuracy. Existing 0x3C
recovery packets are unchanged.

The app uses this status on connected Slim device cards, the reading view and
settings. Firmware versions before 0.22.4 do not expose it. The UI hides the
legacy recovery-history card for current Slim firmware and has no auto-
calibration toggle. It reports verified sensor state; an older OFF device gets
firmware-update guidance rather than an inaccurate enabled label.

Verification: signed production build and 123 firmware tests passed, including
read-only packet bounds, initial-hour progress/reset, saved-OFF migration,
disable rejection, compensation cadence and FRC preserving ASC. Physical
verification of this release and battery measurements remain outstanding.
