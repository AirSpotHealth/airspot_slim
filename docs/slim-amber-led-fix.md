# Slim amber LED correction

Basecamp: https://app.basecamp.com/5949526/buckets/49366756/card_tables/cards/10439833983

The amber colour requested red at 100% and green at 50%, but Zephyr's
`gpio-leds` backend treats every nonzero brightness as fully on. The intended
reduction in green never reached the physical output. CO2 classification still
uses the saved yellow/red thresholds without changing their boundaries.

Rev3A's LED pins are P2.09 (blue), P2.08 (red), P2.06 (green). Nordic's
[PWM register documentation](https://docs.nordicsemi.com/r/bundle/ps_nrf54l15/page/pwm.html-topic)
restricts the hardware PWM peripherals to P1. A 1 ms timer interrupt therefore
alternates green on/off while amber is visible (500 Hz, 50% duty), with red
steady. This does not block the system workqueue or busy-wait. The timer stops
in flash gaps, on cancellation, and when another colour replaces amber. A
spinlock prevents a pending timer interrupt from relighting green after stop.

This adds timer wakeups only while amber is lit; it does not run during ordinary
LED-off sensing. Battery cost and perceived colour must still be measured on
Rev3A hardware. The electrical mix implements the original intended 2:1 ratio;
optical tuning may be needed for the LED package and enclosure.

Validation: host C regression covers default/custom threshold boundaries,
50% green duty with full red, repeated updates, flash gaps, colour changes,
and late interrupts after cancellation. Production signed build and complete
firmware suite are checked before committing. No device was flashed and the
previous release packages have not been replaced.

## 0.22.2 warmer amber candidate

Following a report that amber still appears greenish, reduce green from 50% to
25% duty: 1 ms on / 3 ms off, while keeping red continuously on and blue off.
The timer interrupt rate remains 1 kHz and stops during off periods; no extra
background timer is introduced. This is a colour-tuning candidate, not a verified
optical result. Confirm the affected device/version and compare red, green and
amber on that device before declaring the colour fixed. Includes the 0.22.1
one-second compensation correction in both measurement modes.
