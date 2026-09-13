#!/usr/bin/env python3
"""Select connected J-Link probes and stream RTT to the current terminal."""
import json
import os
from pathlib import Path
import subprocess
import sys
import time


def select_probe(serial=None):
    result = subprocess.run(
        ["nrfutil", "device", "list", "--traits", "jlink", "--json"],
        capture_output=True, text=True, timeout=30, check=True,
    )
    devices = {}
    native_serials = {}
    for line in result.stdout.splitlines():
        event = json.loads(line)
        if event.get("type") == "info":
            for device in event.get("data", {}).get("devices", []):
                sn = device.get("serialNumber", "")
                if device.get("traits", {}).get("jlink") and sn.isdecimal():
                    native_serials[str(int(sn))] = sn
                    devices[str(int(sn))] = device.get("usb", {}).get("product", "J-Link")
    if not devices:
        raise ValueError("No J-Link probes found. Connect a probe and try again.")
    serials = list(devices)
    print("Connected J-Link probes:", file=sys.stderr)
    for index, sn in enumerate(serials, 1):
        print(f"  {index}. {devices[sn]} — serial {sn}", file=sys.stderr)
    serial = serial or os.environ.get("AIRSPOT_JLINK_SERIAL")
    if serial:
        if not serial.isdecimal() or str(int(serial)) not in devices:
            raise ValueError(f"Probe {serial} is not connected.")
        serial = str(int(serial))
    elif len(serials) == 1:
        serial = serials[0]
    else:
        if not sys.stdin.isatty():
            raise ValueError("Multiple probes connected; pass --serial SERIAL to select one.")
        while True:
            print("Select probe number (q to cancel): ", end="", file=sys.stderr, flush=True)
            choice = sys.stdin.readline().strip()
            if not choice or choice.lower() == "q":
                raise ValueError("Probe selection cancelled.")
            if choice.isdecimal() and 1 <= int(choice) <= len(serials):
                serial = serials[int(choice) - 1]
                break
            print("Enter a number from the list.", file=sys.stderr)
    print(f"Using probe {serial}", file=sys.stderr)
    return native_serials[serial]


def stream_logs(path, arguments):
    # Start from an empty file so old sessions are never replayed.
    path = Path(path)
    path.write_bytes(b"")
    diagnostics = path.with_suffix(".jlink.log")
    with path.open("rb") as log, diagnostics.open("wb") as status:
        logger = subprocess.Popen(
            ["JLinkRTTLogger", *arguments, str(path)],
            stdin=subprocess.PIPE, stdout=status, stderr=subprocess.STDOUT,
            start_new_session=True,
        )
        try:
            print("Streaming RTT logs; press Ctrl-C to stop.", flush=True)
            while True:
                data = log.read()
                if data:
                    sys.stdout.buffer.write(data)
                    sys.stdout.buffer.flush()
                elif logger.poll() is not None:
                    if logger.returncode:
                        print(f"RTT logger failed; inspect {diagnostics}", file=sys.stderr)
                    return logger.returncode
                else:
                    time.sleep(0.1)
        finally:
            if logger.poll() is None:
                logger.terminate()
                try:
                    logger.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    logger.kill()
                    logger.wait()
            logger.stdin.close()


if __name__ == "__main__":
    try:
        if sys.argv[1] == "select":
            print(select_probe(sys.argv[2] if len(sys.argv) > 2 else None))
        else:
            sys.exit(stream_logs(sys.argv[2], sys.argv[3:]))
    except KeyboardInterrupt:
        sys.exit(130)
    except (ValueError, OSError, subprocess.SubprocessError) as error:
        sys.exit(f"Probe error: {error}")
