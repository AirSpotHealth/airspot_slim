# Slim 0.22.0 native measurement candidate

This replaces 0.21's four reporting choices with two measurement modes. Both
report and log every 10 seconds. Wire mode 1 is single shot every 10 seconds;
wire mode 3 is continuous acquisition and compensation every second. Modes 0
and 2 are rejected and saved obsolete modes migrate to continuous. New devices
default to continuous. Existing saved mode 1 becomes single shot. Deploy the
matching mobile controls before installing this candidate: older apps show
obsolete interval names. Manual refresh/button requests can request extra
measurements, so avoid interacting during battery comparisons.

Native Sensirion ASC is the default. An explicitly saved native OFF preference
is preserved. Host ASC and offsets cannot be re-enabled through legacy commands.
Native FRC works with ASC on or off. It temporarily measures continuously for
60 seconds, requires at least 30 valid samples and no acquisition gaps above
2.5 seconds after the first sample, stops measurement, applies the sensor's FRC
command and restores the selected mode. Sensor failures prevent calibration;
restoration failures are reported. The returned correction is diagnostic only:
AirSpot never applies it again. This preparation window does not establish
reference-air stability; users must establish stable known CO2, temperature and
humidity before starting. No host averaging or host calibration calculation is
used. FRC accepts the sensor's 0–32000 ppm reference range.

The first 20 seconds of continuous operation and first two single-shot commands
following a measurement restart are marked as preparation, not valid CO2. A
legitimate later 390 ppm reading is retained. The existing idle-history-triggered
22-second conditioning command remains separate from first-hour guidance.

After pairing, show a 100 ms white tick every 10 seconds for approximately one
hour of successful sensing. Other LED indications take priority and low battery
suppresses the tick. Completion is persisted once; normal boots after completion
do not restart it. Power loss before completion restarts the hour conservatively.
Factory sensor calibration reset restarts it; ordinary preference/history reset
does not. Devices upgrading without this marker receive the indication once.
The tick is guidance, not proof of ASC convergence or specified accuracy. The
existing 12-hour recovery guide remains available; starting it selects continuous
mode. The amber GPIO duty correction from 0.21.2 is included.

## Battery comparison

Use the same device, full-charge procedure, ambient conditions, BLE connection
interval, advertising, logging, app connection duration and LED usage for both
runs. Avoid manual refresh, find-device, FRC and firmware upload during a run.
Start steady-state comparisons after first-hour indication ends, and separately
record the first-hour cost. Record elapsed runtime, voltage/current, valid sample
counts and mode. Battery percentage alone cannot quantify consumption reliably.
Do not publish a runtime promise until physical current/runtime tests are done.
Single-shot keeps the sensor idle between shots; the optional sleep comparison
build is disabled. This candidate has not been physically current-profiled.

## Scope and release status

Slim nRF54L15 MCUboot candidate only. Actual AirSpot Screen firmware is unchanged.
Matching Flutter changes are gated to Slim 0.22.0+. No image assets were changed.
A Shorebird asset/native compatibility build is still needed before patch release.
Do not install this as a bootloader image. Use signed application DFU with trial
boot and the existing health-based image confirmation. No release has been
published and no device has been flashed with 0.22.0 during this task.

Validation: final production signed build verified; 121 firmware host/artifact tests passed.
