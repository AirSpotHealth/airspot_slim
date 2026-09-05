#!/usr/bin/env python3
"""Validate production wiring, MCUboot layout, signatures and artifact hashes."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import re
import sys
import zipfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "external/zephyr/scripts/dts/python-devicetree/src"))
sys.path.insert(0, str(ROOT / "external/bootloader/mcuboot/scripts"))
from devicetree import dtlib
from intelhex import IntelHex
from imgtool import keys
from imgtool.image import Image, VerifyResult

PUBLIC_KEY = ROOT / "application/config/ota-signing-public.pub"
PARTITIONS = {"boot_partition": [0, 0x10000], "slot0_partition": [0x10000, 0xa6000],
              "slot1_partition": [0xb6000, 0xa6000], "storage_partition": [0x15c000, 0x9000]}


def require(condition, message):
    if not condition:
        raise ValueError(message)


def read_config(zephyr):
    return dict(line.split("=", 1) for line in (zephyr / ".config").read_text().splitlines()
                if line.startswith("CONFIG_") and "=" in line)


def validate_board(zephyr):
    tree = dtlib.DT(str(zephyr / "zephyr.dts"))
    nodes = tree.label2node
    for label, port, pin, flags in (
        ("sw0", "gpio0", 2, 0), ("led0", "gpio2", 9, 0),
        ("led1", "gpio2", 8, 0), ("led2", "gpio2", 6, 0),
        ("batt_adc_en", "gpio1", 4, 0), ("qi_chg_status", "gpio0", 0, 1),
    ):
        prop = nodes[label].props["gpios"]
        values = dtlib.to_nums(prop.value)
        require(tree.phandle2node[values[0]] is nodes[port] and values[1:] == [pin, flags],
                f"Incorrect {label} wiring")
    require(tree.alias2node["sw0"] is nodes["sw0"], "Incorrect button alias")
    for label, pin in (("flash_wp", 7), ("flash_hold", 6)):
        node = nodes[label]
        require(node.parent is nodes["gpio1"] and node.props["gpios"].to_nums() == [pin, 0]
                and "output-high" in node.props, f"Incorrect {label} output")
    for label in ("led3", "button1", "button2", "button3", "mx25r64"):
        require(label not in nodes, f"DK-only device {label} remains")
    require(nodes["pwm20"].props["status"].to_string() == "disabled", "DK PWM still enabled")
    require(nodes["at25xe161d"].props["size"].to_num() == 16 * 1024 * 1024,
            "External flash must be 16 Mbit")
    for label, region in PARTITIONS.items():
        require(nodes[label].props["reg"].to_nums() == region, f"Bad {label} partition")
    return tree


def validate_build(build):
    files = []
    for domain, start, size, name in (("application", 0x10000, 0xa6000, "airspot_slim"),
                                      ("mcuboot", 0, 0x10000, "zephyr")):
        zephyr = build / domain / "zephyr"
        config = read_config(zephyr)
        require(config.get("CONFIG_SOC_NRF54L15_CPUAPP") == "y", "Not an nRF54L15 build")
        require(config.get("CONFIG_PARTITION_MANAGER_ENABLED") != "y", "Unexpected PM layout")
        require(config.get("CONFIG_USE_DT_CODE_PARTITION") == "y", "Missing partition bounds")
        require(int(config["CONFIG_FLASH_LOAD_OFFSET"], 0) == start, "Wrong linker start")
        require(int(config["CONFIG_FLASH_LOAD_SIZE"], 0) == size, "Wrong linker size")
        require(config.get("CONFIG_RTT_CONSOLE") == "y" and config.get("CONFIG_SERIAL") != "y",
                "Diagnostics must use RTT")
        tree = validate_board(zephyr)
        chosen = tree.get_node("/chosen").props["zephyr,code-partition"].to_path()
        require(chosen is tree.label2node["slot0_partition" if domain == "application" else "boot_partition"],
                "Incorrect code partition")
        if domain == "application":
            require(config.get("CONFIG_SEGGER_RTT_INIT_MODE_ALWAYS") == "y", "RTT must reset stale image pointers")
            for symbol in ("BOOTLOADER_MCUBOOT", "MCUMGR", "MCUMGR_GRP_IMG", "MCUMGR_TRANSPORT_BT",
                           "MCUMGR_SMP_COMMAND_STATUS_HOOKS"):
                require(config.get("CONFIG_" + symbol) == "y", f"Missing {symbol}")
            require(config.get("CONFIG_MCUBOOT_GENERATE_CONFIRMED_IMAGE") != "y", "OTA must test before confirm")
            require(config.get("CONFIG_MCUMGR_GRP_IMG_ALLOW_CONFIRM_NON_ACTIVE_SLOT") != "y", "Client can bypass trial boot")
        else:
            require(config.get("CONFIG_LTO") != "y", "Boot handoff wrapper requires LTO disabled")
            from elftools.elf.elffile import ELFFile
            with (zephyr / "zephyr.elf").open("rb") as stream:
                symbols = ELFFile(stream).get_section_by_name(".symtab")
                require(symbols.get_symbol_by_name("__wrap_sys_clock_disable"), "Missing GRTC handoff fix")
            for symbol in ("BOOT_SWAP_USING_MOVE", "BOOT_SIGNATURE_TYPE_ECDSA_P256",
                           "BOOT_VALIDATE_SLOT0", "BOOT_WATCHDOG_FEED_NRFX_WDT"):
                require(config.get("CONFIG_" + symbol) == "y", f"Missing {symbol}")
            # The compiled bootloader key must match the pinned product public key.
            compiled_source = (zephyr / "autogen-pubkey.c").read_text()
            compiled_key = bytes(int(v, 16) for v in re.findall(r"0x([0-9a-fA-F]{2})", compiled_source))
            files.append("mcuboot/zephyr/autogen-pubkey.c")
            require(compiled_key == keys.load(str(PUBLIC_KEY)).get_public_bytes(),
                    "Bootloader signing key differs from the product key")
        import yaml
        runner = yaml.safe_load((zephyr / "runners.yaml").read_text())
        require("--device=nRF54L15_M33" in runner["args"]["jlink"], "Incorrect J-Link device")
        for ext in ("bin", "hex", "elf"):
            rawfile = zephyr / f"{name}.{ext}"
            require(rawfile.is_file(), f"Missing {rawfile}")
            files.append(str(rawfile.relative_to(build)))
        raw = (zephyr / f"{name}.bin").read_bytes()
        image = IntelHex(str(zephyr / f"{name}.hex"))
        require(8 <= len(raw) <= size, "Empty or oversized image")
        require(image.tobinarray(start=start, size=len(raw)).tobytes() == raw, "HEX/BIN mismatch")
        require(all(start <= lo < hi <= start + size for lo, hi in image.segments()), "HEX outside partition")
        files.extend(f"{domain}/zephyr/{f}" for f in (".config", "zephyr.dts", "runners.yaml"))
    signed_path = build / "application/zephyr/airspot_slim.signed.bin"
    result, version, digest, _ = Image.verify(str(signed_path), keys.load(str(PUBLIC_KEY)))
    require(result == VerifyResult.OK, f"Invalid OTA signature: {result}")
    raw = signed_path.read_bytes()
    header_size = struct.unpack_from("<H", raw, 8)[0]
    body_size = struct.unpack_from("<I", raw, 12)[0]
    unsigned = signed_path.with_name("airspot_slim.bin").read_bytes()
    require(raw[header_size:header_size + body_size] == unsigned[header_size:],
            "Signed image does not match this application build")
    require(len(raw) <= 0xa6000 - 0x5000, "OTA image exceeds swap capacity")
    signed_hex = IntelHex(str(signed_path.with_suffix(".hex")))
    hex_result, hex_version, hex_digest, _ = Image.verify(str(signed_path.with_suffix(".hex")), keys.load(str(PUBLIC_KEY)))
    require(hex_result == VerifyResult.OK and hex_version == version and hex_digest == digest,
            "Signed HEX/BIN payload or signature mismatch")
    require(all(0x10000 <= lo < hi <= 0xb6000 for lo, hi in signed_hex.segments()),
            "Signed HEX outside primary slot")
    files += [str(signed_path.relative_to(build)), str(signed_path.with_suffix(".hex").relative_to(build))]
    return {"soc": "nrf54l15", "layout": "mcuboot-internal-v1", "version": list(version),
            "public_key_sha256": hashlib.sha256(PUBLIC_KEY.read_bytes()).hexdigest(),
            "image_hash": digest.hex(),
            "sha256": {f: hashlib.sha256((build / f).read_bytes()).hexdigest() for f in files}}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("record", "verify"))
    parser.add_argument("build", type=Path)
    args = parser.parse_args()
    try:
        result = validate_build(args.build)
        manifest = args.build / "firmware-artifact.json"
        if args.action == "record":
            # One signed application, never an arbitrary .bin chosen from a bundle.
            with zipfile.ZipFile(args.build / "airspot_slim_dfu.zip", "w", zipfile.ZIP_DEFLATED) as package:
                package.write(args.build / "application/zephyr/airspot_slim.signed.bin", "app_update.bin")
                package.writestr("manifest.json", json.dumps({"format-version": 1, "files": [
                    {"file": "app_update.bin", "type": "application", "image_index": 0}]}))
            result["sha256"]["airspot_slim_dfu.zip"] = hashlib.sha256((args.build / "airspot_slim_dfu.zip").read_bytes()).hexdigest()
            manifest.write_text(json.dumps(result, indent=2) + "\n")
        else:
            result["sha256"]["airspot_slim_dfu.zip"] = hashlib.sha256((args.build / "airspot_slim_dfu.zip").read_bytes()).hexdigest()
            require(json.loads(manifest.read_text()) == result, "Artifacts changed; rebuild before flashing")
        print(f"Production wiring, MCUboot and signed image verified: {args.build}")
    except (OSError, ValueError, KeyError, struct.error, dtlib.DTError) as error:
        parser.exit(1, f"Firmware validation failed: {error}\n")


if __name__ == "__main__":
    main()
