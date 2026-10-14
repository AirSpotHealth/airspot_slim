# Slim low-battery recovery — 0.18.5

Normal operation enters low-battery lockout below 3300 mV while off charge.
Lockout disables Bluetooth and sensor measurements. Recovery now requires
charging and valid voltage readings of at least 3400 mV for ten continuous
seconds; charger loss, any below-threshold reading or voltage-read failure
restarts the interval. Lockout forces an ADC refresh each one-second poll and
uses the lower of raw and filtered voltage. Recovery reboots into normal startup.
The previous 3700 mV recovery threshold was inherited from Screen.

Battery percentage is an approximate usable-range indicator, linearly scaled
from 1% at/below the 3300 mV operating cutoff to 100% at 4200 mV. It is not a
measured lithium-cell state-of-charge curve. The final low-battery notification
reports 1% on a valid voltage read so smoothing cannot leave a misleading high
percentage on disconnect. ADC failures retain the existing unavailable marker.

Host regressions cover exact voltage/time boundaries, charger removal, voltage
dips, ADC failures, percentage endpoints/monotonicity and low-battery packets.
Physical acceptance remains: discharge a Slim into lockout, verify recovery only
after the stable interval, verify fresh readings/BLE, and remove it from charge
while measuring the startup voltage dip. Repeat if it returns to lockout; the
100 mV margin is a candidate pending board/battery verification.

This does not alter the firmware-update minimum voltage or bypass startup health
checks. New firmware must be installed before this behavior takes effect.
