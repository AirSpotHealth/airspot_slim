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
/* Factory reset only: preflight before erasing; main performs the power cut. */
int power_manager_prepare_factory_off(void);
void power_manager_request_factory_off(void);
bool power_manager_factory_off_requested(void);
int power_manager_cut_battery(void);

#ifdef __cplusplus
}
#endif
#endif
