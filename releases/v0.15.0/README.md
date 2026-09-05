# AirSpot Slim 0.15.0

Manual OTA test release, built 2026-09-05 from `develop` base `9bd2e926d29cd2eff338ca5a034a1fcaee3437e1`
with only `application/VERSION` changed from 0.14.6 to 0.15.0. Source is tagged
`v0.15.0`. No driver, protocol, settings, memory-layout or bootloader changes.

## File to upload or select in the app

**`airspot_slim_v0.15.0_dfu.zip`** contains exactly `app_update.bin` (the signed MCUboot
application) and `manifest.json`. Select this ZIP for a local Slim update; do not
extract it or flash a bootloader. The embedded firmware version is 0.15.0.

For the update-server upload, select device model **Slim** and version **0.15.0**.
Deploy the server's model-isolation fix before publishing Slim firmware to the
shared feed. Inactive releases are available to beta app clients, not hidden drafts.
Choose the minimum app version according to the mobile build being tested.

## Verification

- `bash scripts/airspot build` and `bash scripts/airspot check`: passed.
- Full firmware suite: 24 tests passed, including signature, memory layout,
  rollback configuration and settings-preservation checks.
- Actual ZIP passed the mobile hosted-update parser and 0.15.0 version match.
- Same pinned product public key, MCUboot configuration and bootloader binary as
  the preceding local 0.14.6 production build.
- Image hash (MCUboot): `20545889ca46d111e7bc5988408732f087e122388703457ad4d2f56632508a8f`.
- Original complete build hashes: `firmware-artifact.json`.
- Package/document checksums: `SHA256SUMS`.

No OTA upload or device flashing was performed to create this release. Physical
0.14.6 → 0.15.0 update, confirmed image/version, fresh readings and retained
settings/history remain for manual testing. The version number does not imply
that these physical checks have already passed.
