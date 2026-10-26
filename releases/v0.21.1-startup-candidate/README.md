# Slim 0.21.1 startup candidate

Supersedes the 0.21.0 candidate for engineering evaluation. Not installed, published or physically qualified.

Firmware source: 01cbfe7 (`codex/stcc4-operating-policy`). Matching app source: 7b92eac (`codex/stcc4-recovery-guidance`). No app binary is included.

Changes: conservative 20-second guard after continuous-mode starts; compensation continues; no bypass samples count as valid CO2, OTA health, or green LED status. Actual 390 ppm readings are allowed after the guard. Normal preparation is not treated as a recovery fault. Periodic modes publish the first valid post-bypass sample promptly; Now remains request-driven. Conditional idle-history-based conditioning remains enabled and is distinct from the guard and 12-hour recovery policy.

Validation: 116 host firmware tests passed; signed artifact verification passed. App: 184 tests passed, one skipped; final targeted tests and analysis passed. No supported Nordic devices were found by the read-only connected-device scan. Hardware current/runtime, reference accuracy, physical BLE/OTA and endurance checks are pending.

The ZIP contains one signed application for the existing compatible AirSpot Slim nRF54L15 MCUboot OTA layout. It is not for Screen, Hub, nRF52832 or bootloader provisioning. Keep normal trial-image validation and rollback enabled. No signing keys are included. The build manifest describes the full build; SHA256SUMS describes the files supplied here.

See instructions-and-battery.md for operating guidance and remaining qualification checks.
