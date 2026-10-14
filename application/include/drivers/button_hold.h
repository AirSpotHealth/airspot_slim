/* Shared timing policy for running and System OFF wake gestures. */
#ifndef AIRSPOT_BUTTON_HOLD_H
#define AIRSPOT_BUTTON_HOLD_H
#include <stdbool.h>
#include <stdint.h>
#define BUTTON_NORMAL_HOLD_MS 5000U
#define BUTTON_RECOVERY_WARNING_MS 10000U
#define BUTTON_RECOVERY_READY_MS 15000U
#define BUTTON_RECOVERY_EXPIRED_MS 18000U

enum button_hold_action {
  BUTTON_HOLD_CANCEL,
  BUTTON_HOLD_NORMAL,
  BUTTON_HOLD_RECOVER,
};

/* Completion always requires release. A trapped button never confirms. */
static inline enum button_hold_action button_hold_release(uint32_t elapsed,
                                                         bool charging)
{
  if (elapsed >= BUTTON_RECOVERY_READY_MS && elapsed < BUTTON_RECOVERY_EXPIRED_MS)
    return BUTTON_HOLD_RECOVER;
  if (charging && elapsed >= BUTTON_NORMAL_HOLD_MS && elapsed < BUTTON_RECOVERY_WARNING_MS)
    return BUTTON_HOLD_NORMAL;
  return BUTTON_HOLD_CANCEL;
}
#endif
