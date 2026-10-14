# Slim factory reset with power-off — 0.19.2 candidate

Slim factory reset (0xFA) is explicitly destructive: it erases STCC4 learned
calibration and host corrections, device preferences, and saved readings. The
history erase invalidates durable metadata and commits a zero cursor/count.
Native ASC and five-second continuous sampling are selected for settling.
After acknowledgement, main disconnects BLE and asserts nPM1100 SHPACT on
P2.03. The battery remains physically attached; the power rail is disconnected.
The customer must press the button to wake. There is no automatic wake timer.

Before erasure, firmware requires a confirmed image, a valid empty request,
no detected Qi pad, a released button, and a usable SHPACT GPIO. It reserves
sensor calibration access, stops sampling/recovery, wakes STCC4 and verifies its
factory-reset result. It does not require LPS22HH initialization to succeed.
On any erase/settings error it returns failure and records the failed stage;
partial reset is possible and never reported as success. No rollback of already
erased calibration is possible. Sampling remains stopped; retry or restart is
needed after a failure. The ACK certifies committed erasures, not measured rail
voltage. If charger/button state changes during erase, shutdown waits for pad
removal and button release. A surviving power-cut attempt lowers SHPACT and
retries rather than falsely claiming success.

Mobile exposes this through the existing Factory Reset item for Slim only,
requires firmware 0.19.2, and waits for a valid acknowledgement before removing
local history/settings. Confirmation explains irreversible erasure, charger
removal, button release, power-off and button wake after five seconds. Screen's
command flow and confirmation are retained. Restart device remains 0x27 MCU
reboot. Erase sensor calibration remains the dedicated 0x3A action and preserves
saved readings and unrelated preferences. The internal 0x3D preferences-reset
command remains compatible but is not the Factory Reset UI action.

Validation includes host execution of the actual erase, factory-reset protocol,
STCC4 erase and power-control paths, injected failures, production firmware build
and signature/layout validation, and Flutter command/settling tests. Physical
81A9DC results are recorded separately under diagnostics/slim-2026-10-14.
No public release has been made.

The first on-device test (0.19.1) was interrupted by a watchdog reset and did
not return success. The long operation now executes on the watchdog-owning main
thread; the protocol worker only queues it. Each completed flash sector feeds
the watchdog, accounting for up to 300 ms per sector rather than assuming 32
sectors finish within five seconds. BLE generation is captured with the request;
a queued request from a disconnected session is discarded before erasure.

Final device test: 81A9DC self-confirmed 0.19.2 image
b7d688a4f0bd5702a2d39e1bf166627997754882a8dceb55d18148c119a95c1d.
Factory reset returned success (FF AA FA 01 01 A5), RTT recorded all history
erased, and J-Link subsequently measured VTref=0.000 V. No watchdog restart
occurred in that reset. Button-wake and post-reset cursor/settling verification
remain pending user wake; do not treat the entire end-to-end test as complete.
