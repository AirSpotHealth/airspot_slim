#!/usr/bin/env python3
"""Install the checked bootloader/application, preserving the device's NVS.

Known factory/bring-up images, this bootloader, and blank devices are accepted.
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
# Exact image previously installed during the board bring-up, before MCUboot.
BRINGUP_SIZE = 245172
BRINGUP_SHA256 = "6a4423e55f30123c9ad8258429b2476dbf60cdbefec0f8282d26142f76cbd884"
# Factory image captured before Rev3A migration, and matched on a new board.
# Hash the entire main-processor region, including its erased settings space;
# accepting this does not infer an NVS location from an arbitrary application.
FACTORY_MAIN_SHA256 = "bce98bb80f25c7b5abaa70a1e31ddcf7c744f50d58070a8b26f33fc649e84bb8"


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


def prepare_image(before, boot, app, update):
    require(len(before) == RRAM_SIZE, "Incomplete device backup")
    provisioned = before[:len(boot)] == boot
    blank = before[:APP_MEMORY_END] == b"\xff" * APP_MEMORY_END
    known_bringup = hashlib.sha256(before[:BRINGUP_SIZE]).hexdigest() == BRINGUP_SHA256
    known_factory = hashlib.sha256(before[:APP_MEMORY_END]).hexdigest() == FACTORY_MAIN_SHA256
    require(provisioned or (not update and (blank or known_bringup or known_factory)),
            "Unknown installed firmware/layout; refusing to overwrite or guess settings location")
    source = SETTINGS_START if provisioned or blank or known_factory else 0x74000
    settings = before[source:source + SETTINGS_SIZE]
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
    parser.add_argument("--update", action="store_true")
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
        image, source = prepare_image(before, boot, app, args.update)
    except Exception:
        # Preflight failure: resume the unchanged device.
        jlink(["g"], serial, backup)
        raise
    (backup / "settings.bin").write_bytes(before[source:source + SETTINGS_SIZE])
    (backup / "backup.json").write_text(json.dumps({"probe": serial, "part": hex(part),
        "settings_source": hex(source), "settings_destination": hex(SETTINGS_START),
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
    print("Readback verified; settings preserved; device reset and running", flush=True)


if __name__ == "__main__":
    try:
        main()
    except (ValueError, OSError, subprocess.TimeoutExpired) as error:
        raise SystemExit(f"Flash stopped: {error}") from None
