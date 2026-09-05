# Bluetooth firmware updates

AirSpot Slim uses MCUmgr SMP over Bluetooth LE and MCUboot with ECDSA P-256
signatures. The existing mobile `SlimDfuService` uses the compatible
`testAndConfirm` update mode. Upload the production `airspot_slim_dfu.zip` or
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

1. Connect J-Link and set `AIRSPOT_JLINK_SERIAL` to its serial number.
2. Run `airspot build`, then `airspot flash` (`airspot provision` is also accepted).
3. Open `airspot logs rtt` and check startup, live readings and confirmation.

Flashing accepts a blank device, the exact known bring-up image, the archived
pre-Rev3A factory image with erased settings, or the current production bootloader.
The factory image is identified by a hash of the entire main-processor region;
changed or unknown legacy images are refused rather than guessing their settings. It backs up all 1,524 KiB RRAM under the ignored
`local_backups/` directory and verifies the MCU identity. For the known bring-up
image, it copies the 36 KiB settings region from `0x74000` to `0x15C000`. It writes
the bootloader, signed primary image and preserved settings, clears stale update
state, reads everything back and checks that auxiliary memory is unchanged.
No external flash erase or UICR/recover operation is performed.

Subsequent `airspot flash` reinstalls checked firmware and clears pending updates,
preserving current settings. It refuses devices with an unknown bootloader. A
readback mismatch leaves the device halted and names the recovery snapshot; use
that snapshot for diagnosis, rather than a mass erase. Wired reinstall discards
the staged update and its rollback image, so normal field updates should use OTA.

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
hardware watchdog once started. Remote writes are blocked before startup checks
pass, and the client cannot permanently confirm an inactive slot.

Image writes and reset requests require a cached battery reading of at least
3,600 mV. This is a conservative software guard, not a replacement for brownout
and interrupted-update qualification. The battery reading follows the configured
sensor sampling interval.

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

Fourteen automated checks cover real build artifacts, signature tampering, MCU identity,
bootloader offsets, rollback configuration, key mismatch, writes outside slots,
and preservation of settings during provisioning. Physical test results are
recorded in the board qualification document.

The production 0.14.0 to 0.14.1 wireless path was verified on the board using a
macOS MCUmgr client: upload, test request, BLE reset, startup sensor validation,
automatic confirmation and reconnect. The mobile app itself still needs an
end-to-end update test. Allow at least 30 seconds for sensor conditioning after
reboot; the observed image confirmation occurred at 27.4 seconds.
