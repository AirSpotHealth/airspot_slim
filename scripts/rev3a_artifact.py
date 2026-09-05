#!/usr/bin/env python3
"""Validate locally built Rev3A wiring, partitions and images before flashing.

Uses Zephyr's generated DTS (text, not executable/pickled input). The manifest
detects stale/mixed artifacts; it is not a firmware signature or hardware test.
"""
import argparse
import hashlib
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "external/zephyr/scripts/dts/python-devicetree/src"))
from devicetree import dtlib

APP_END = 0x74000


def require(condition, message):
    if not condition:
        raise ValueError(message)


def validate_build(build):
    zephyr = build / "zephyr"
    config = dict(line.split("=", 1) for line in
                  (zephyr / ".config").read_text().splitlines()
                  if line.startswith("CONFIG_") and "=" in line)
    require(config.get("CONFIG_SOC_NRF54L15_CPUAPP") == "y", "Not an nRF54L15 build")
    require(config.get("CONFIG_BOOTLOADER_MCUBOOT") != "y", "MCUboot is not validated")
    require(config.get("CONFIG_USE_DT_CODE_PARTITION") == "y", "Missing linker partition limit")
    require(int(config["CONFIG_FLASH_LOAD_OFFSET"], 0) == 0, "Image must start at zero")
    require(int(config["CONFIG_FLASH_LOAD_SIZE"], 0) == APP_END, "Incorrect application limit")
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
    require(tree.alias2node["sw0"] is nodes["sw0"], "Button alias is not Rev3A")
    for label, pin in (("flash_wp", 7), ("flash_hold", 6)):
        node = nodes[label]
        require(node.parent is nodes["gpio1"] and node.props["gpios"].to_nums() == [pin, 0]
                and "output-high" in node.props, f"Incorrect {label} output")
    for label in ("led3", "button1", "button2", "button3", "mx25r64"):
        require(label not in nodes, f"DK-only device {label} remains")
    require(nodes["pwm20"].props["status"].to_string() == "disabled", "DK PWM still enabled")
    require(nodes["at25xe161d"].props["size"].to_num() == 16 * 1024 * 1024,
            "External flash must be 16 Mbit")
    require(nodes["app_partition"].props["reg"].to_nums() == [0, APP_END], "Bad app partition")
    require(nodes["storage_partition"].props["reg"].to_nums() == [APP_END, 0x9000],
            "Settings location changed")
    name = config["CONFIG_KERNEL_BIN_NAME"].strip('"')
    require(name.startswith("airspot_slim_rev3a_") and Path(name).name == name,
            "Image must identify Rev3A")
    import yaml
    runners = yaml.safe_load((zephyr / "runners.yaml").read_text())
    require("--device=nRF54L15_M33" in runners["args"]["jlink"],
            "Incorrect J-Link target")
    for ext in ("bin", "hex", "elf"):
        require(runners["config"][f"{ext}_file"] == f"{name}.{ext}",
                "Runner points to a different image")
    image = zephyr / f"{name}.bin"
    require(8 <= image.stat().st_size <= APP_END, "Image overlaps settings or is empty")
    # IntelHex is already an NCS dependency. Check full data, including extended
    # addresses: the historical helper mistook any image >48 KiB for MCUboot.
    from intelhex import IntelHex
    hex_image = IntelHex(str(zephyr / f"{name}.hex"))
    raw = image.read_bytes()
    require(hex_image.tobinarray(start=0, size=len(raw)).tobytes() == raw,
            "HEX and BIN do not contain the same application")
    require(all(0 <= start < end <= APP_END for start, end in hex_image.segments()),
            "HEX writes outside the application partition")
    files = [f"zephyr/{name}.{ext}" for ext in ("bin", "hex", "elf")]
    files += ["zephyr/.config", "zephyr/zephyr.dts", "zephyr/runners.yaml"]
    return {"hardware": "rev3a", "soc": "nrf54l15", "layout": "monolithic",
            "sha256": {file: hashlib.sha256((build / file).read_bytes()).hexdigest()
                       for file in files}}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("record", "verify"))
    parser.add_argument("build", type=Path)
    args = parser.parse_args()
    try:
        result = validate_build(args.build)
        manifest = args.build / "rev3a-artifact.json"
        if args.action == "record":
            manifest.write_text(json.dumps(result, indent=2) + "\n")
        else:
            require(json.loads(manifest.read_text()) == result,
                    "Artifacts changed since the successful build; rebuild before flashing")
        print(f"Rev3A wiring, partitions and image integrity verified: {args.build}")
    except (OSError, ValueError, KeyError, dtlib.DTError) as error:
        parser.exit(1, f"Rev3A validation failed: {error}\n")


if __name__ == "__main__":
    main()
