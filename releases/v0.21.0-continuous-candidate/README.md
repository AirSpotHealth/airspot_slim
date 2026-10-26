# AirSpot Slim 0.21.0 continuous-sensing candidate

Engineering evaluation only; not yet physically qualified or installed.

Firmware source commit: `051a729`, branch `codex/stcc4-operating-policy`.
App source commit: `8025fc4`, branch `codex/stcc4-recovery-guidance`.
Both branches preserve their pre-existing evaluation changes in separate baseline commits.

`airspot_slim_dfu.zip` contains the signed application for the existing compatible AirSpot Slim nRF54L15 MCUboot OTA layout. It is not for Screen, Hub, nRF52832 or bootloader provisioning. Use the existing signed OTA workflow on a backed-up test Slim; this package does not perform installation. Keep normal trial-image validation and rollback enabled.

The matching app source implements the 12-hour recovery flow. No mobile release binary is included.

See instructions-and-battery.md for customer wording, current-consumption assumptions, test evidence, build/cache usage and required bench qualification. `build-verification.json` describes the complete build directory; only the application files are included here. `SHA256SUMS` covers this package's files.

No keys are included. No release was published and no Basecamp task was closed.
