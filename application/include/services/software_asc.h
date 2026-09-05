/* AirSpot-owned calibration. STCC4 internal ASC must always remain paused. */
#ifndef SOFTWARE_ASC_H
#define SOFTWARE_ASC_H
#include <stdbool.h>
#include <stdint.h>
void software_asc_init(void);
/* Exclude the first 3 minutes after boot/reset from automatic observations.
 * Live readings and explicitly requested manual calibration are unaffected. */
void software_asc_sensor_restarted(void);
int software_asc_set_enabled(uint8_t enabled);
int software_asc_set_target(uint16_t target);
void software_asc_set_check_duration(uint16_t seconds);
uint8_t software_asc_get_day_count(void);
void software_asc_process_measurement(uint16_t raw_ppm);
void software_asc_tick(void);
/* Manual calibration owns the correction until finish; no sensor calls here. */
int software_asc_begin_manual(void);
int software_asc_finish_manual(uint16_t target, uint16_t average, bool have_samples,
                               int16_t *delta);
void software_asc_end_manual(void);
bool software_asc_manual_active(void);
#endif
