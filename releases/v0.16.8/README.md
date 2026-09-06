# 0.16.8 — read-only history forensic candidate

Diagnostic firmware only. CONFIG_AIRSPOT_FORENSIC_HISTORY blocks program/erase
commands through the external flash driver and dumps the metadata and first
history sector over RTT. Normal logging is intentionally disabled. Do not use
this as a production logging release.

Installed on 81A9DC with settings preserved and readback verified. All 49 tests
and the signed production-wiring build passed. The 8192-byte raw capture showed
four CRC-valid metadata checkpoints (0, 32, 64, 96 records), latest uptime 28827
seconds; the complete first history sector was FF. All 32/128-byte read prefix
comparisons matched. No readable CO2 records were recovered. This does not prove
when or why the history sector became blank. Pressure startup remains failed.
