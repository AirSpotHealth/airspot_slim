#ifndef POWER_MANAGER_H
#define POWER_MANAGER_H
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif

int power_manager_init(void);
/* Request the same soft-off state used by the existing button gesture.
 * Main consumes the request and performs teardown outside the BLE worker.
 * Wake remains the existing five-second hold on the Qi charger.
 * This does not enter unqualified hardware System OFF.
 */
void power_manager_enter_off_state(void);
bool power_manager_take_off_request(void);

#ifdef __cplusplus
}
#endif
#endif
