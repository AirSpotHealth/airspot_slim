#!/usr/bin/env python3
"""Read-only AirSpot BLE discovery and live monitoring (Slim and Screen)."""
import argparse
import asyncio
import datetime as dt
import json
import os
import sys
import time
from dataclasses import asdict, dataclass

RX = "6e400002-b5a3-f393-e0a9-e50e24dcca9e"
TX = "6e400003-b5a3-f393-e0a9-e50e24dcca9e"
MODES = {0: "On demand", 1: "3 min", 2: "1 min", 3: "5 sec"}


def frame(command, payload=b""):
    body = bytes([255, 170, command, len(payload)]) + payload
    return body + bytes([sum(body) & 255])


def decode(packet):
    """Accept legacy Screen length=2 and Slim length=10 live frames."""
    b = bytes(packet)
    if len(b) < 5 or b[:2] != b"\xff\xaa" or sum(b[:-1]) & 255 != b[-1]:
        raise ValueError("Invalid BLE frame/checksum")
    cmd = b[2]
    if cmd == 1 and len(b) in (11, 15):
        ppm = int.from_bytes(b[8:10], "big")
        result = {"kind": "reading", "co2": None if ppm == 65535 else ppm,
                  "fault": (f"0x{b[4]:02X}" if len(b) == 15 else "sensor error") if ppm == 65535 else None}
        if len(b) == 15 and ppm != 65535:
            result.update(temperature=int.from_bytes(b[10:12], "big", signed=True) / 100,
                          humidity=int.from_bytes(b[12:14], "big") / 100)
        return result
    if cmd == 0x13 and len(b) >= 6 and b[3] == len(b) - 5:
        return {"kind": "metadata", "version": b[4:-1].decode("ascii", errors="replace")}
    if cmd == 0x2B and len(b) == 6:
        return {"kind": "metadata", "variant": {0: "Screen", 1: "Screen", 2: "Slim"}.get(b[4], "Unknown")}
    if cmd == 8 and len(b) >= 25 and b[3] == len(b) - 5:
        return {"kind": "metadata", "mode": MODES.get(b[6], f"Unknown ({b[6]})"),
                "asc": "ON" if b[12] else "OFF"}
    if cmd == 0x20 and len(b) == 7:
        return {"kind": "metadata", "battery": None if b[4] == 255 else b[4],
                "charging": bool(b[5])}
    if cmd == 0x38 and len(b) == 12 and b[4] == 1:
        return {"kind": "metadata", "asc": {0: "Host/OFF", 1: "OFF", 2: "ON"}.get(b[5], "Unknown"),
                "asc_verified": bool(b[6]), "asc_error": int.from_bytes(b[7:9], "big", signed=True)}
    return None


@dataclass
class Row:
    device: str
    address: str
    connection: str = "Connecting"
    variant: str = "?"
    version: str = "?"
    mode: str = "?"
    asc: str = "?"
    asc_verified: bool = False
    asc_error: int = 0
    battery: int | None = None
    charging: bool = False
    co2: int | None = None
    fault: str | None = None
    temperature: float | None = None
    humidity: float | None = None
    last_sample: float | None = None


class Display:
    def __init__(self, json_output=False):
        self.rows = {}
        self.json_output = json_output
        self.interactive = sys.stdout.isatty() and not json_output

    def event(self, device, kind, **values):
        if self.json_output:
            print(json.dumps({"time": dt.datetime.now().astimezone().isoformat(timespec="seconds"),
                              "device": device, "kind": kind, **values}), flush=True)
        elif not self.interactive:
            stamp = dt.datetime.now().strftime("%H:%M:%S")
            print(f"{stamp} {device}: {kind} {json.dumps(values)}", flush=True)

    def update(self, name, values):
        if name not in self.rows:
            return
        row = self.rows[name]
        if values["kind"] == "reading":
            row.last_sample = time.monotonic()
            row.temperature = row.humidity = None
        for key, value in values.items():
            if key != "kind":
                setattr(row, key, value)
        self.event(name, **values)

    def render(self):
        if not self.interactive:
            return
        paint = lambda text, code: f"\033[{code}m{text}\033[0m" if "NO_COLOR" not in os.environ else text
        lines = [paint("AIRSPOT / LIVE — Ctrl+C disconnects and returns to the menu.", "1;36"),
                 "Device           Type    CO2 ppm  Battery  Mode       AutoCal  Age    Connection"]
        for row in sorted(self.rows.values(), key=lambda r: r.device):
            ppm = f"ERR {row.fault}" if row.fault else str(row.co2) if row.co2 is not None else "—"
            age = f"{int(time.monotonic() - row.last_sample)}s" if row.last_sample is not None else "—"
            battery = f"{row.battery}%{'+' if row.charging else ''}" if row.battery is not None else "—"
            line = (f"{row.device:<16} {row.variant:<7} {ppm:<8} {battery:<8} {row.mode:<10} "
                    f"{row.asc:<8} {age:<6} {row.connection}")
            lines.append(paint(line, "31" if row.fault else "32" if row.connection == "Connected" else "33"))
        print("\033[2J\033[H" + "\n".join(lines), end="\n", flush=True)


class Session:
    def __init__(self, client, name, display):
        self.client, self.name, self.display = client, name, display
        self.waiter = None

    def notify(self, _sender, packet):
        try:
            values = decode(packet)
        except ValueError as error:
            self.display.event(self.name, "frame_error", message=str(error))
            return
        if values:
            self.display.update(self.name, values)
        if self.waiter and packet[2] == self.waiter[0] and not self.waiter[1].done():
            self.waiter[1].set_result(bytes(packet))

    async def request(self, command, response=None, payload=b""):
        # Only read commands and a one-off fresh measurement request are allowed.
        if command not in (1, 8, 0x13, 0x20, 0x21, 0x2B, 0x38):
            raise ValueError("This monitor cannot change settings")
        if payload and (command, payload) != (0x21, b"\x01"):
            raise ValueError("This monitor cannot change settings")
        future = asyncio.get_running_loop().create_future()
        self.waiter = (command if response is None else response, future)
        try:
            await self.client.write_gatt_char(RX, frame(command, payload), response=True)
            return await asyncio.wait_for(future, timeout=8)
        finally:
            self.waiter = None


async def discover(seconds):
    from bleak import BleakScanner
    devices = await BleakScanner.discover(timeout=seconds, return_adv=True)
    found = {}
    for device, adv in devices.values():
        name = adv.local_name or device.name or ""
        if name.startswith("AirSpot-"):
            found[name] = (device, adv)
    return found


def select(found, names):
    selected, missing = {}, []
    for supplied in names:
        name = supplied if supplied.startswith("AirSpot-") else "AirSpot-" + supplied.upper()
        if name not in found:
            missing.append(name)
        else:
            selected[name] = found[name]
    return selected, missing


async def watch_device(name, device, display, device_type=None):
    from bleak import BleakClient
    row = display.rows[name]
    while True:
        disconnected = asyncio.Event()
        row.connection = "Connecting"
        try:
            async with BleakClient(device, timeout=20,
                                   disconnected_callback=lambda _c: disconnected.set()) as client:
                row.connection = "Connected"
                row.last_sample = row.co2 = row.fault = None
                display.event(name, "connected")
                session = Session(client, name, display)
                await client.start_notify(TX, session.notify)
                for command in (0x13, 0x2B, 8, 0x20):
                    try:
                        await session.request(command)
                    except TimeoutError:
                        display.event(name, "query_timeout", command=hex(command))
                if device_type and row.variant.lower() != device_type:
                    display.rows.pop(name, None)
                    return
                if row.variant == "Slim" and row.version not in ("?", ""):
                    try:
                        version = tuple(int(n) for n in row.version.split(".")[:3])
                        if version >= (0, 18, 0):
                            await session.request(0x38)
                    except (ValueError, TimeoutError):
                        pass
                # Get a fresh sample now, then let the configured cadence drive updates.
                try:
                    await session.request(0x21, response=1, payload=b"\x01")
                except TimeoutError:
                    display.event(name, "query_timeout", command="fresh measurement")
                while not disconnected.is_set():
                    try:
                        await asyncio.wait_for(disconnected.wait(), timeout=30)
                    except TimeoutError:
                        await session.request(0x20)
        except asyncio.CancelledError:
            row.connection = "Disconnected"
            raise
        except Exception as error:
            display.event(name, "connection_error", message=str(error))
        row.connection = "Reconnecting"
        display.event(name, "disconnected")
        await asyncio.sleep(5)


async def run(args):
    found = await discover(args.scan_seconds)
    if args.command == "scan":
        records = [{"device": name, "address": d.address, "rssi": adv.rssi}
                   for name, (d, adv) in sorted(found.items())]
        if args.json:
            print(json.dumps(records, indent=2))
        else:
            print("Device            Bluetooth address                         RSSI")
            for r in records:
                print(f"{r['device']:<17} {r['address']:<41} {r['rssi']} dBm")
            if not records:
                print("No AirSpot devices found. Check power, Bluetooth range and phone disconnection.")
        return 0 if records else 1
    selected, missing = (found, []) if not args.devices else select(found, args.devices)
    for name in missing:
        print(f"Not found: {name}. Check power/range and disconnect the phone app.", file=sys.stderr)
    if not selected:
        print("No requested AirSpot devices found.", file=sys.stderr)
        return 1
    display = Display(args.json)
    tasks = []
    for name, (device, _adv) in selected.items():
        display.rows[name] = Row(name, device.address)
        tasks.append(asyncio.create_task(watch_device(name, device, display, args.type)))
    started = time.monotonic()
    try:
        while not args.duration or time.monotonic() - started < args.duration:
            display.render()
            await asyncio.sleep(1)
    finally:
        for task in tasks:
            task.cancel()
        await asyncio.gather(*tasks, return_exceptions=True)
        display.render()
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    for command in ("scan", "watch", "cli"):
        p = sub.add_parser(command)
        p.add_argument("--scan-seconds", type=float, default=8)
        p.add_argument("--json", action="store_true", help="Machine-readable output; watch emits JSONL")
        if command == "cli":
            p.add_argument("--type", choices=("slim", "screen"))
        if command == "watch":
            p.add_argument("--type", choices=("slim", "screen"), help="Monitor only this sensor family")
            p.add_argument("devices", nargs="*", help="Device IDs/names; omit to watch all discovered AirSpots")
            p.add_argument("--duration", type=float, default=0, help="Stop after this many seconds; default: Ctrl+C")
    args = parser.parse_args()
    if args.scan_seconds <= 0 or getattr(args, "duration", 0) < 0:
        parser.error("scan-seconds must be positive; duration cannot be negative")
    try:
        if args.command == "cli":
            from airspot_tui import run_console
            return run_console(args.type)
        return asyncio.run(run(args))
    except KeyboardInterrupt:
        return 0
    except ImportError:
        print("Install Bluetooth dependencies with: ./scripts/airspot-live setup", file=sys.stderr)
        return 1
    except Exception as error:
        print(f"Bluetooth error: {error}. Check Bluetooth permissions and power.", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
