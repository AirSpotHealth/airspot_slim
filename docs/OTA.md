# Bluetooth firmware updates

AirSpot Slim uses MCUmgr SMP over Bluetooth LE and MCUboot with ECDSA P-256
signatures. The existing mobile `SlimDfuService` uses the compatible
`testOnly` update mode (device-controlled confirmation). Upload the production `airspot_slim_dfu.zip` or
`airspot_slim.signed.bin`; never upload the unsigned application or bootloader.

## Memory layout

| Region | Start | Size |
|---|---:|---:|
| MCUboot | `0x000000` | 64 KiB |
| Primary application | `0x010000` | 664 KiB |
| Secondary update | `0x0B6000` | 664 KiB |
| Settings/NVS | `0x15C000` | 36 KiB |

Swap metadata and image headers reduce the usable application payload. The SDK
reserves 96 KiB NVM and 68 KiB RAM for the auxiliary processor; the main processor
has 1,428 KiB NVM and 188 KiB RAM. External SPI flash remains sensor history storage.

## Initial wired installation

1. Connect J-Link; the flash command lists probes and selects one automatically or prompts if several are connected.
2. Run `airspot build`, then `airspot flash` (`airspot provision` is also accepted).
3. Open `airspot logs` and check startup, live readings and confirmation.

Flashing installs independently of the previous firmware: blank, older, unknown,
or damaged images do not require a bootloader allowlist or local build history.
The helper still validates the new build and MCU identity, backs up all 1,524 KiB
RRAM under `local_backups/`, writes firmware, and verifies readback and unchanged
auxiliary memory. External flash and UICR are not erased or recovered.

By default, the 36 KiB at `0x15c000` is preserved byte-for-byte. This is the current
settings address, not a claim that arbitrary old firmware used the same layout.
Use `airspot flash --reset-settings` for fresh settings. For a device known to use
the old bring-up layout, use `airspot flash --settings-source 0x74000` to copy its
settings to the current address. Both modes retain the full original memory backup.

Wired installation clears pending updates and rollback images. A readback mismatch
leaves the device halted and names the recovery snapshot. Hardware/debug access
failures can still stop flashing; unknown old firmware alone cannot.

## Wireless update behavior

The client uploads to the secondary internal slot and requests a **test** boot.
MCUboot checks the signature and uses swap-with-move, retaining the previous
application for rollback. The bootloader feeds a watchdog inherited across reset.

The pinned SDK's default timer shutdown stalled application GRTC startup on this
board. The bootloader-only `boot_support` module preserves GRTC/LFCLK until
Nordic's existing peripheral cleanup disables its interrupt/compare channels.
The application also reinitializes RTT metadata on each boot so an update cannot
reuse stale buffer addresses. LTO is disabled in MCUboot to retain this explicit link wrapper; the artifact
checker verifies its presence. Re-evaluate this workaround when upgrading NCS.

The new application confirms only after driver/service startup and a valid live
sensor sample. An unconfirmed application that reaches its 120-second startup
deadline reboots, allowing MCUboot to revert. A hard lock is handled by the
hardware watchdog started before driver initialization. Remote writes are blocked
while the running image is unconfirmed; confirmed firmware permits repair uploads
even when sensor startup fails. The client cannot confirm the trial remotely.

Image writes and reset requests require a successful battery reading no older
than five seconds and at least 3,600 mV (lower of raw and filtered voltage).
Refresh is independent of sensor sampling; ADC failure rejects the operation.
This guard does not replace brownout and interrupted-update qualification.

See [beta hardening](archive/BETA_HARDENING.md) and [attached-device verification](archive/BETA_HARDWARE_CHECK.md) for the 0.17.1 candidate, recovery
boundaries, diagnostic protocol and outstanding physical release gates.

## Signing and access

The repository pins only the **public** verification key in
`application/config/ota-signing-public.pub`. The existing private product key is
ignored under `application/keys/`, or supplied using `AIRSPOT_SIGNING_KEY`. Back it
up securely before provisioning a batch: losing it prevents future signed updates.
The helper checks the compiled bootloader key and both signed artifacts against
the pinned public key. The artifact manifest detects stale or mixed local builds.

BLE management preserves the existing unpaired connection flow. Signatures
prevent arbitrary executable code from booting; they do not authenticate the
nearby phone. A nearby client can upload data, request a reset, or install an older
correctly signed image. Physical update authorization, pairing-based access, and
anti-downgrade counters are not implemented. Do not describe this as fully
qualified secure field deployment until those product requirements are resolved.

## Verification

```sh
airspot build
airspot check
/opt/nordic/ncs/toolchains/322ac893fe/bin/python3 -m unittest discover -s tests -p test_firmware_artifact.py
```

Automated checks cover real build artifacts, signature tampering, MCU identity,
bootloader offsets, rollback configuration, key mismatch, writes outside slots,
and preservation of settings during provisioning. Physical test results are
recorded in the board qualification document.

The production 0.14.0 to 0.14.1 wireless path was verified on the board using a
macOS MCUmgr client: upload, test request, BLE reset, startup sensor validation,
automatic confirmation and reconnect. The mobile app itself still needs an
end-to-end update test. Allow at least 30 seconds for sensor conditioning after
reboot; the observed image confirmation occurred at 27.4 seconds.
