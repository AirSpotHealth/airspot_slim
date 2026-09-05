/* Signed Bluetooth updates with device-controlled startup confirmation. */
#include <zephyr/kernel.h>
#include <zephyr/dfu/mcuboot.h>
#include <zephyr/logging/log.h>
#include <zephyr/mgmt/mcumgr/mgmt/callbacks.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/reboot.h>
#include <services/ota_service.h>

LOG_MODULE_REGISTER(ota_service, CONFIG_AIRSPOT_LOG_LEVEL);

static atomic_t startup_healthy;
static atomic_t sample_healthy;
static atomic_t battery_mv;
static bool needs_confirmation;
static int64_t deadline;

/* Preserve the app's existing unpaired BLE flow. Signatures authorize code;
 * nearby clients can still request management operations (see OTA.md).
 * Block writes until startup passes, including remote premature confirmation.
 */
static enum mgmt_cb_return command_check(uint32_t event,
    enum mgmt_cb_return previous, int32_t *rc, uint16_t *group,
    bool *abort_more, void *data, size_t size)
{
    ARG_UNUSED(event);
    ARG_UNUSED(group);
    ARG_UNUSED(abort_more);
    if (previous != MGMT_CB_OK || size != sizeof(struct mgmt_evt_op_cmd_arg)) {
        return previous;
    }
    const struct mgmt_evt_op_cmd_arg *cmd = data;
    if (cmd->op != MGMT_OP_WRITE) {
        return MGMT_CB_OK;
    }
    if (!atomic_get(&startup_healthy) || !atomic_get(&sample_healthy)) {
        *rc = MGMT_ERR_EBUSY;
        return MGMT_CB_ERROR_RC;
    }
    /* Image writes (upload, erase, test/confirm) and OS reset require enough
     * battery. Use the sensor's cached reading; never race its ADC driver.
     */
    if ((cmd->group == MGMT_GROUP_ID_IMAGE ||
         (cmd->group == MGMT_GROUP_ID_OS && cmd->id == 5)) &&
        atomic_get(&battery_mv) < 3600) {
        *rc = MGMT_ERR_EBADSTATE;
        return MGMT_CB_ERROR_RC;
    }
    return MGMT_CB_OK;
}

static struct mgmt_callback command_callback = {
    .callback = command_check,
    .event_id = MGMT_EVT_OP_CMD_RECV,
};

void ota_service_init(void)
{
    needs_confirmation = !boot_is_img_confirmed();
    deadline = k_uptime_get() + 120000;
    mgmt_callback_register(&command_callback);
    LOG_INF("Bluetooth OTA enabled; image %s",
            needs_confirmation ? "awaiting startup checks" : "confirmed");
}

void ota_service_startup_complete(bool healthy)
{
    atomic_set(&startup_healthy, healthy);
    if (!healthy) {
        LOG_ERR("Startup checks failed; update will not be confirmed");
    }
}

void ota_service_observe_sample(const sensor_data_t *data)
{
    atomic_set(&battery_mv, data->battery_mv);
    if (data->valid) {
        atomic_set(&sample_healthy, 1);
    }
}

void ota_service_poll(void)
{
    if (!needs_confirmation) {
        return;
    }
    if (atomic_get(&startup_healthy) && atomic_get(&sample_healthy)) {
        int err = boot_write_img_confirmed();
        if (!err) {
            needs_confirmation = false;
            LOG_INF("Firmware confirmed after startup and live sensor checks");
            return;
        }
        LOG_ERR("Image confirmation failed: %d", err);
    }
    if (k_uptime_get() >= deadline) {
        LOG_ERR("Startup deadline expired; rebooting for MCUboot rollback");
        sys_reboot(SYS_REBOOT_COLD);
    }
}
