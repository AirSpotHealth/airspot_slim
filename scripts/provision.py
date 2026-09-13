#!/usr/bin/env python3
"""Install the checked bootloader/application, preserving the device's NVS.

Installation does not depend on the previously installed firmware.
Every write is preceded by a full RRAM snapshot and followed by readback.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess

from firmware_artifact import ROOT, require, validate_build

RRAM_SIZE = 0x17d000
APP_MEMORY_END = 0x165000
SETTINGS_START = 0x15c000
SETTINGS_SIZE = 0x9000


def jlink(commands, serial, directory):
    script = directory / "commands.jlink"
    script.write_text("\n".join(commands + ["exit"]) + "\n")
    result = subprocess.run(["JLinkExe", "-device", "nRF54L15_M33", "-if", "SWD",
                             "-speed", "1000", "-autoconnect", "1", "-SelectEmuBySN",
                             serial, "-ExitOnError", "1", "-CommanderScript", str(script)],
                            capture_output=True, text=True, timeout=180)
    with (directory / "jlink.log").open("a") as log:
        log.write(result.stdout + result.stderr)
    require(result.returncode == 0, f"J-Link failed; inspect {directory / 'jlink.log'}")


def prepare_image(before, boot, app, settings_source=SETTINGS_START, reset_settings=False):
    require(len(before) == RRAM_SIZE, "Incomplete device backup")
    require(0 < len(boot) <= 0x10000, "Bootloader exceeds its partition")
    require(0 < len(app) <= 0xa6000, "Application exceeds its partition")
    require(0 <= settings_source <= APP_MEMORY_END - SETTINGS_SIZE,
            "Settings source is outside main-processor memory")
    # Preserve the configured region as bytes; do not infer a legacy layout from
    # bootloader identity. An explicit source allows migration from older layouts.
    source = None if reset_settings else settings_source
    settings = (b"\xff" * SETTINGS_SIZE if reset_settings else
                before[source:source + SETTINGS_SIZE])
    # Explicitly clear old swap state and staged images. Preserve NVS and the
    # auxiliary processor's memory (outside the image written below).
    image = bytearray(b"\xff" * APP_MEMORY_END)
    image[:len(boot)] = boot
    image[0x10000:0x10000 + len(app)] = app
    image[SETTINGS_START:SETTINGS_START + SETTINGS_SIZE] = settings
    return bytes(image), source


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("build", type=Path)
    parser.add_argument("--update", action="store_true", help=argparse.SUPPRESS)
    settings = parser.add_mutually_exclusive_group()
    settings.add_argument("--settings-source", type=lambda value: int(value, 0),
                          default=SETTINGS_START, metavar="ADDRESS",
                          help="copy settings from this address (default: 0x15c000)")
    settings.add_argument("--reset-settings", action="store_true",
                          help="install with erased settings instead of preserving them")
    args = parser.parse_args()
    serial = os.environ.get("AIRSPOT_JLINK_SERIAL")
    require(serial and serial.isdecimal(), "Set AIRSPOT_JLINK_SERIAL to the connected product probe")
    manifest = validate_build(args.build)
    recorded = json.loads((args.build / "firmware-artifact.json").read_text())
    require(all(recorded.get(k) == v for k, v in manifest.items() if k != "sha256") and
            all(recorded["sha256"].get(k) == v for k, v in manifest["sha256"].items()),
            "Artifacts changed since build")
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S%fZ")
    backup = ROOT / "local_backups" / f"production-{serial}-{stamp}"
    backup.mkdir(parents=True)
    before_path = backup / "before_rram.bin"
    ficr = backup / "ficr.bin"
    try:
        jlink(["h", f'savebin "{ficr}", 0x00ffc31c, 0x14',
               f'savebin "{before_path}", 0, {RRAM_SIZE:#x}'], serial, backup)
        part, variant, package, ram, rram = struct.unpack("<5I", ficr.read_bytes())
        require(part == 0x54b15 and ram == 256 and rram == 1524, "Unexpected MCU capacity/part")
        before = before_path.read_bytes()
        boot = (args.build / "mcuboot/zephyr/zephyr.bin").read_bytes()
        app = (args.build / "application/zephyr/airspot_slim.signed.bin").read_bytes()
        image, source = prepare_image(before, boot, app, args.settings_source, args.reset_settings)
    except Exception:
        # Preflight failure: resume the unchanged device.
        jlink(["g"], serial, backup)
        raise
    (backup / "settings.bin").write_bytes(before[args.settings_source:args.settings_source + SETTINGS_SIZE])
    (backup / "backup.json").write_text(json.dumps({"probe": serial, "part": hex(part),
        "settings_source": hex(source) if source is not None else None,
        "settings_reset": args.reset_settings, "settings_destination": hex(SETTINGS_START),
        "before_sha256": hashlib.sha256(before).hexdigest(), "build": recorded}, indent=2) + "\n")
    install = backup / "install.bin"
    install.write_bytes(image)
    readback = backup / "after_rram.bin"
    print(f"Device backed up to {backup}; installing signed firmware", flush=True)
    jlink(["h", f'loadbin "{install}", 0', f'savebin "{readback}", 0, {RRAM_SIZE:#x}'], serial, backup)
    after = readback.read_bytes()
    require(after[:APP_MEMORY_END] == image and after[APP_MEMORY_END:] == before[APP_MEMORY_END:],
            f"Readback mismatch. Device is halted; recovery snapshot: {before_path}")
    jlink(["r", "g"], serial, backup)
    settings_status = "settings reset" if args.reset_settings else f"settings bytes preserved from {source:#x}"
    print(f"Readback verified; {settings_status}; device reset and running", flush=True)


if __name__ == "__main__":
    try:
        main()
    except (ValueError, OSError, subprocess.TimeoutExpired) as error:
        raise SystemExit(f"Flash stopped: {error}") from None
