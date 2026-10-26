# STCC4 operating policy — implementation plan

Basecamp: https://app.basecamp.com/5949526/buckets/49366756/card_tables/cards/10440435332
Date: 27 October 2026 (Australia/Sydney)

## Confirmed scope

Use native STCC4 calibration and preserve learned history. The user selected
12 hours of continuous measurements followed by fresh-air exposure as the
recovery procedure for now. Do not substitute forced recalibration to an assumed
outdoor reference. Temperature/humidity and pressure compensation must update
every second in 1-minute, 3-minute and Now modes.

Confirmed: CO2 measures continuously in every mode; modes select reporting cadence only. Battery optimization and reversible obsolete-build cleanup are also authorized.

## Baseline

The isolated branch codex/stcc4-operating-policy starts from c6da700 and snapshots
the existing rob-native-frc evaluation work as 051126b. The original checkout
and evaluation worktree are preserved. That baseline already bypasses host
correction, supports native FRC, and conditions after long idle periods. It is
not evidence of hardware accuracy or a production-qualified release.

## Implementation sequence

1. Separate compensation/acquisition cadence from reporting cadence as clarified.
   Verify all four modes, immediate requests, compensation failure, mode changes,
   conditioning, and restart. Keep stored mode and diagnostic flags truthful.
2. Make the 12-hour tracker usable without erasing calibration. Count only valid
   continuous operation with native ASC enabled; restart interrupted intervals.
   Require a separate user-confirmed fresh-air exposure step. Never claim that
   elapsed time or completion proves accuracy.
3. Expose customer guidance in the Flutter app with firmware capability checks,
   device-owned progress, disconnect handling and accessible narrow-screen layout.
4. Produce customer instructions, factory acceptance checklist and a traceable
   release package. Run host regressions, app checks, and firmware compilation.
5. Validate on physical Slims before deployment or closing the Basecamp card.

## Release evidence required

- At least three Slims compared with a suitable reference, including attached to
  a phone, charging, changing CO2, power cycles and extended operation.
- Measured battery life under the new compensation/acquisition cadence.
- Continuous 12-hour progress and fresh-air completion with interrupts tested.
- No automatic native FRC or calibration-history erasure in the customer flow.
- Factory preparation recorded per serial number; do not say devices are
  prepared before shipping until that process is actually implemented.

## Sources and limits

The full Basecamp card body was read; its Word attachment download failed in the
connector. The card includes the policy and customer-flow text.
Sensirion STCC4 datasheet, D1 June 2025, sections 1.1.2–1.1.4 and 2.5:
https://sensirion.com/resource/datasheet/STCC4
Native ASC requires regular fresh-air exposure. Initial operation and post-idle
warm-up are different; the short conditioning command is not proof of accuracy.
