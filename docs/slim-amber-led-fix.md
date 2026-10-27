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

## 0.22.3 visually selected amber

On 27 October 2026 the wired Slim (D30E3C) displayed correct individual
red/green/blue channels, but 25% green still appeared greenish. A temporary
500 Hz bench comparison showed full red with 12%, 6%, and 3% green; the
user selected the second mix (6%). The original 0.20.1 firmware and settings
were restored afterward with a complete RRAM byte-for-byte verification.

Production amber now uses full red, blue off, and nominal 6% green:
192 microseconds on / 3008 microseconds off. Both durations are exact multiples
of the production 32 microsecond kernel tick. A one-shot timer alternates
these intervals at 312.5 Hz, requiring 625 callbacks per second only while
amber is lit (previously 1000). Flash gaps, cancellation and colour changes
stop the timer. No busy-wait is added to the normal indicator.

The selected optical ratio is hardware-tested; the production timer waveform
and its appearance in normal flashes still require on-device confirmation.
Battery consumption has not been measured. Slim T/E368FD remains on 0.22.2
until a separate successful firmware transfer is recorded.
