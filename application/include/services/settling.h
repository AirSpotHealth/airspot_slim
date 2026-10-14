#ifndef AIRSPOT_SETTLING_H
#define AIRSPOT_SETTLING_H
#include <stdbool.h>
#include <stdint.h>
#define SETTLING_SECONDS (12U * 60U * 60U)
#define FRESH_AIR_SECONDS 600U
/* NONE means no tracked erase, not calibrated. DONE is customer acknowledgement. */
enum settling_stage { SETTLING_NONE, SETTLING_RUN, SETTLING_FRESH,
                      SETTLING_EXPOSING, SETTLING_DONE, SETTLING_ERASE_PENDING };
typedef struct {
  uint8_t stage;
  uint32_t generation, reset_time, seconds, fresh_seconds;
  bool running;
  int error;
} settling_status_t;
int settling_prepare_erase(uint32_t reset_time);
int settling_erased(void);
void settling_observe(bool eligible);
void settling_get(settling_status_t *out);
int settling_action(uint8_t action, uint32_t generation);
#endif
