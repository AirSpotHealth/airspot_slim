"""Explicit hardware test: change sampling modes, then restore the original.

Requires bleak. Supply --name for the intended board. This is not a runtime test
and cannot share the board's single BLE connection with the phone app.
"""
import argparse
import asyncio
import json
import time

from bleak import BleakClient, BleakScanner

RX = '6e400002-b5a3-f393-e0a9-e50e24dcca9e'
TX = '6e400003-b5a3-f393-e0a9-e50e24dcca9e'


def frame(command, payload=b''):
    body = bytes([255, 170, command, len(payload)]) + payload
    return body + bytes([sum(body) & 255])


class ProtocolClient:
    def __init__(self, client):
        self.client = client
        self.notifications = asyncio.Queue()

    def notify(self, characteristic, data):
        packet = bytes(data)
        if len(packet) < 5 or packet[:2] != b'\xff\xaa' or sum(packet[:-1]) & 255 != packet[-1]:
            self.notifications.put_nowait(ValueError('Malformed notification'))
            return
        self.notifications.put_nowait((time.monotonic(), packet))
        print(json.dumps({'time': time.time(), 'rx': packet.hex()}), flush=True)

    async def receive(self, command, timeout=10):
        async def matching_notification():
            while True:
                item = await self.notifications.get()
                if isinstance(item, Exception):
                    raise item
                timestamp, packet = item
                if packet[2] == command:
                    return timestamp, packet
        return await asyncio.wait_for(matching_notification(), timeout)

    async def command(self, command, payload=b'', response=None):
        while not self.notifications.empty():
            item = self.notifications.get_nowait()
            if isinstance(item, Exception):
                raise item
        await self.client.write_gatt_char(RX, frame(command, payload), response=True)
        return await self.receive(command if response is None else response)


async def qualify(name):
    device = await BleakScanner.find_device_by_filter(
        lambda device, advertisement: device.name == name, timeout=60)
    if not device:
        raise RuntimeError('Target not found')
    async with BleakClient(device) as client:
        protocol = ProtocolClient(client)
        await client.start_notify(TX, protocol.notify)
        _, before = await protocol.command(8)
        original_mode = before[6]
        try:
            _, state = await protocol.command(5, b'\xff', response=8)
            assert state == before, 'Invalid mode changed settings'
            _, state = await protocol.command(0x22, bytes([1, 22, 0, 7]), response=8)
            assert state == before, 'Short DND changed settings'
            await client.write_gatt_char(RX, bytes([255, 170, 5, 255]), response=True)
            _, state = await protocol.command(8)
            assert state == before, 'Parser did not recover'
            print('PASS malformed command checks', flush=True)

            _, battery = await protocol.command(0x20)
            assert len(battery) == 7 and battery[4] <= 100
            # The legacy history-position request responds using command 0x0C.
            await protocol.command(0x0b, response=0x0c)

            for mode, interval in [(3, 5), (2, 60), (1, 180)]:
                _, state = await protocol.command(5, bytes([mode]), response=8)
                assert state[6] == mode
                timestamps = []
                deadline = time.monotonic() + 2 * (interval + 30)
                while len(timestamps) < 2:
                    remaining = deadline - time.monotonic()
                    if remaining <= 0:
                        raise TimeoutError('No valid scheduled samples')
                    timestamp, packet = await protocol.receive(1, min(interval + 30, remaining))
                    co2 = int.from_bytes(packet[8:10], 'big')
                    if co2 not in (0, 65535):
                        timestamps.append(timestamp)
                spacing = timestamps[1] - timestamps[0]
                assert interval - 1 <= spacing <= interval + 5, (mode, spacing)
                print(json.dumps({'mode': mode, 'observed_interval_s': spacing,
                                  'valid_samples': 2, 'result': 'PASS'}), flush=True)
        finally:
            if client.is_connected:
                await protocol.command(5, bytes([original_mode]), response=8)
                print('Restored original mode', original_mode, flush=True)
            else:
                print(f'Connection lost: restore mode {original_mode} through the app', flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--name', required=True)
    arguments = parser.parse_args()
    asyncio.run(qualify(arguments.name))
