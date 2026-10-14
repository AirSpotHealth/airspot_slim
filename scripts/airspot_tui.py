#!/usr/bin/env python3
"""Keyboard-driven AirSpot device console; BLE stays on one asyncio loop."""
import asyncio
import curses
import time

from airspot_live import Display, Row, watch_device


class Picker:
    def __init__(self):
        self.devices = {}
        self.selected = set()
        self.cursor = 0

    def discovered(self, device, advertisement):
        name = advertisement.local_name or device.name or ""
        if name.startswith("AirSpot-"):
            self.devices[device.address] = (device, advertisement, name)

    def move(self, offset):
        self.cursor = max(0, min(len(self.devices) - 1, self.cursor + offset))

    def toggle(self):
        if self.devices:
            address = list(self.devices)[self.cursor]
            if address in self.selected:
                self.selected.remove(address)
            else:
                self.selected.add(address)

    def chosen(self):
        return [value for address, value in self.devices.items() if address in self.selected]


class LiveDisplay(Display):
    def __init__(self):
        super().__init__()
        self.message = ""

    def event(self, device, kind, **values):
        if kind in ("connection_error", "frame_error", "query_timeout"):
            self.message = f"{device}: {values.get('message', kind)}"


class Console:
    def __init__(self, screen, device_type=None):
        self.screen = screen
        self.device_type = device_type
        screen.nodelay(True)
        screen.keypad(True)
        try:
            curses.curs_set(0)
        except curses.error:
            pass
        if curses.has_colors():
            curses.start_color()
            curses.use_default_colors()
            for index, color in enumerate((curses.COLOR_CYAN, curses.COLOR_GREEN, curses.COLOR_YELLOW, curses.COLOR_RED), 1):
                curses.init_pair(index, color, -1)

    def write(self, y, text, color=0, selected=False):
        height, width = self.screen.getmaxyx()
        if not 0 <= y < height or width < 2:
            return
        attr = curses.color_pair(color) if curses.has_colors() else 0
        if selected:
            attr |= curses.A_REVERSE
        try:
            self.screen.addnstr(y, 2 if width > 4 else 0, text, max(1, width - 4), attr)
        except curses.error:
            pass

    def draw(self, title, lines, footer, message=""):
        self.screen.erase()
        self.write(1, "AIRSPOT  /  " + title, 1)
        for index, (text, color, selected) in enumerate(lines, 4):
            if index >= self.screen.getmaxyx()[0] - 4:
                break
            self.write(index, text, color, selected)
        height = self.screen.getmaxyx()[0]
        self.write(height - 3, message, 3)
        self.write(height - 2, footer, 1)
        self.screen.refresh()

    async def key(self):
        await asyncio.sleep(0.05)
        return self.screen.getch()

    async def menu(self):
        cursor = 0
        while True:
            self.draw("DEVICE CONSOLE", [("1. Scan devices", 0, cursor == 0), ("2. Exit", 0, cursor == 1)],
                      "↑/↓ Move   Enter Choose   1 Scan   2 Exit")
            key = await self.key()
            if key in (ord('2'), ord('q'), 27, 3):
                return
            if key in (curses.KEY_UP, curses.KEY_DOWN):
                cursor = 1 - cursor
            if key == ord('1') or key in (10, 13, curses.KEY_ENTER) and cursor == 0:
                await self.scan_and_pick()
            elif key in (10, 13, curses.KEY_ENTER) and cursor == 1:
                return

    def picker_lines(self, picker):
        items = list(picker.devices.items())
        visible = max(1, self.screen.getmaxyx()[0] - 8)
        start = max(0, picker.cursor - visible + 1)
        return [(f"[{'x' if address in picker.selected else ' '}] {name:<20} {adv.rssi:>4} dBm",
                 2 if address in picker.selected else 0, i == picker.cursor)
                for i, (address, (_device, adv, name)) in enumerate(items) if start <= i < start + visible]

    async def scan_and_pick(self):
        while await self.scan_once():
            pass

    async def scan_once(self):
        from bleak import BleakScanner
        picker = Picker()
        message = ""
        try:
            async with BleakScanner(detection_callback=picker.discovered):
                deadline = time.monotonic() + 8
                while time.monotonic() < deadline:
                    remaining = max(0, deadline - time.monotonic())
                    self.draw(f"SCANNING  {remaining:.1f}s  /  {len(picker.devices)} found",
                              self.picker_lines(picker), "Esc / Q Back", "Devices appear live. Selection opens when scanning finishes.")
                    if await self.key() in (27, ord('q'), 3):
                        return
        except Exception as error:
            message = f"Scan failed: {error}"
        while True:
            self.draw(f"SELECT DEVICES  /  {len(picker.selected)} selected", self.picker_lines(picker),
                      "↑/↓ Move   Space Select   Enter Connect   R Rescan   Esc/Q Back",
                      message or ("No devices found. Press R to scan again." if not picker.devices else "Scan complete. Select the devices you want to connect."))
            key = await self.key()
            if key in (27, ord('q'), 3):
                return
            if key in (ord('r'), ord('R')):
                return True
            if key in (curses.KEY_UP, ord('k')):
                picker.move(-1)
            elif key in (curses.KEY_DOWN, ord('j')):
                picker.move(1)
            elif key == ord(' '):
                picker.toggle()
            elif key in (10, 13, curses.KEY_ENTER):
                if picker.selected:
                    await self.monitor(picker.chosen())
                    message = "Disconnected. Change selection or press Enter to reconnect."
                else:
                    message = "Press Space to select at least one device first."

    async def monitor(self, chosen):
        display = LiveDisplay()
        tasks = []
        for device, _adv, name in chosen:
            display.rows[name] = Row(name, device.address)
            tasks.append(asyncio.create_task(watch_device(name, device, display, self.device_type)))
        try:
            while True:
                lines = [("Device           Type    CO₂ ppm  Battery  Mode    Age   Connection", 1, False)]
                for row in display.rows.values():
                    ppm = "ERROR" if row.fault else str(row.co2) if row.co2 is not None else "—"
                    age = f"{int(time.monotonic() - row.last_sample)}s" if row.last_sample is not None else "—"
                    battery = f"{row.battery}%" if row.battery is not None else "—"
                    text = f"{row.device:<16} {row.variant:<7} {ppm:<8} {battery:<8} {row.mode:<7} {age:<5} {row.connection}"
                    lines.append((text, 4 if row.fault else 2 if row.connection == 'Connected' else 3, False))
                self.draw("LIVE READINGS", lines, "Esc / Q Disconnect and return to selection", display.message)
                if await self.key() in (27, ord('q'), 3):
                    return
        finally:
            self.draw("DISCONNECTING", [], "", "Closing device connections…")
            for task in tasks:
                task.cancel()
            await asyncio.gather(*tasks, return_exceptions=True)


def run_console(device_type=None):
    curses.set_escdelay(30)
    # Raw mode makes Ctrl+C a handled key, allowing BLE cleanup and menu return.
    def start(screen):
        curses.raw()
        asyncio.run(Console(screen, device_type).menu())
    curses.wrapper(start)
    return 0
