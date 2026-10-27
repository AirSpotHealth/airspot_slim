/* Signed Bluetooth updates with device-controlled startup confirmation. */
#include <services/history_events.h>
#include <zephyr/kernel.h>
#include <zephyr/dfu/mcuboot.h>
#include <zephyr/logging/log.h>
#include <zephyr/mgmt/mcumgr/mgmt/callbacks.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/reboot.h>
#include <services/ota_service.h>
#include <drivers/battery.h>
#include <services/health_service.h>
#include <services/diagnostics.h>

LOG_MODULE_REGISTER(ota_service, CONFIG_AIRSPOT_LOG_LEVEL);

static atomic_t startup_healthy;
static atomic_t sample_healthy;
static atomic_t sensor_ready;
static bool needs_confirmation;
static int64_t deadline;

/* Preserve the app's existing unpaired BLE flow. Signatures authorize code;
 * nearby clients can still request management operations (see OTA.md).
 * Trial images cannot be remotely confirmed; confirmed images allow repair.
 */
static enum mgmt_cb_return command_check(uint32_t event,
    enum mgmt_cb_return previous, int32_t *rc, uint16_t *group,
    bool *abort_more, void *data, size_t size)
{
    if (event == MGMT_EVT_OP_CMD_DONE) {
        (void)health_failed();
        health_progress(HEALTH_OTA, 0);
        return previous;
    }
    /* Watch the local command worker, never the time between phone packets. */
    health_progress(HEALTH_OTA, 10000);
    ARG_UNUSED(group);
    ARG_UNUSED(abort_more);
    if (previous != MGMT_CB_OK || size != sizeof(struct mgmt_evt_op_cmd_arg)) {
        return previous;
    }
    const struct mgmt_evt_op_cmd_arg *cmd = data;
    if (cmd->op != MGMT_OP_WRITE) {
        return MGMT_CB_OK;
    }
    /* Trial images remain device-controlled. A confirmed but degraded image
     * must still accept a signed repair when Bluetooth is functional. */
    if (!boot_is_img_confirmed()) {
        *rc = MGMT_ERR_EBUSY;
        return MGMT_CB_ERROR_RC;
    }
    if (cmd->group == MGMT_GROUP_ID_IMAGE ||
        (cmd->group == MGMT_GROUP_ID_OS && cmd->id == 5)) {
        uint16_t mv;
        if (battery_get_ota_voltage(&mv) != 0 || mv < 3600) {
            diagnostics_record(DIAG_OTA_REJECTED, 1);
            *rc = MGMT_ERR_EBADSTATE;
            return MGMT_CB_ERROR_RC;
        }
    }
    return MGMT_CB_OK;
}

static struct mgmt_callback command_callback = {
    .callback = command_check,
    .event_id = MGMT_EVT_OP_CMD_RECV | MGMT_EVT_OP_CMD_DONE,
};

static enum mgmt_cb_return image_event(uint32_t event,
    enum mgmt_cb_return previous, int32_t *rc, uint16_t *group,
    bool *abort_more, void *data, size_t size)
{
    ARG_UNUSED(rc); ARG_UNUSED(group); ARG_UNUSED(abort_more);
    ARG_UNUSED(data); ARG_UNUSED(size);
    if (event == MGMT_EVT_OP_IMG_MGMT_DFU_STARTED)
        (void)history_event_record(1, RECORD_TYPE_DFU);
    else if (event == MGMT_EVT_OP_IMG_MGMT_DFU_STOPPED)
        (void)history_event_record(0, RECORD_TYPE_DFU_FAIL);
    return previous;
}
static struct mgmt_callback image_callback = {
    .callback = image_event,
    .event_id = MGMT_EVT_OP_IMG_MGMT_DFU_STARTED | MGMT_EVT_OP_IMG_MGMT_DFU_STOPPED,
};

void ota_service_init(void)
{
    atomic_set(&startup_healthy, 0);
    atomic_set(&sample_healthy, 0);
    atomic_set(&sensor_ready, 0);
    needs_confirmation = !boot_is_img_confirmed();
    deadline = k_uptime_get() + 120000;
    if (needs_confirmation) diagnostics_record(DIAG_TRIAL, 0);
    mgmt_callback_register(&command_callback);
    mgmt_callback_register(&image_callback);
    LOG_INF("Bluetooth OTA enabled; image %s",
            needs_confirmation ? "awaiting startup checks" : "confirmed");
}

void ota_service_startup_complete(bool healthy)
{
    atomic_set(&startup_healthy, healthy);
    if (!healthy) {
        diagnostics_record(DIAG_STARTUP_FAILED, 0);
        LOG_ERR("Startup checks failed; update will not be confirmed");
    }
}

/* Called only after sensor initialization and selected-mode setup succeed.
 * Discard any sample from before that configuration. Runtime recovery may
 * satisfy this gate without clearing failures in other startup services. */
void ota_service_sensor_ready(void)
{
    atomic_set(&sample_healthy, 0);
    atomic_set(&sensor_ready, 1);
}

void ota_service_observe_sample(const sensor_data_t *data)
{
    static bool sensor_failed;
    if (!data->valid) atomic_set(&sample_healthy, 0);
    if (!data->valid && data->error_code == SENSOR_FAULT_STARTING) return;
    if (data->valid) {
        sensor_failed = false;
        atomic_set(&sample_healthy, 1);
    } else {
        if (!sensor_failed) (void)history_event_record(1, RECORD_TYPE_SENSOR_ERROR);
        sensor_failed = true;
        /* 0x53 marker + fault byte; retained journal survives a later reboot. */
        diagnostics_record(DIAG_SENSOR_FAILED,
            0x53000000U | ((uint32_t)data->error_code << 16));
    }
}

void ota_service_poll(void)
{
    if (!needs_confirmation) {
        return;
    }
    if (atomic_get(&startup_healthy) && atomic_get(&sensor_ready) &&
        atomic_get(&sample_healthy)) {
        int err = boot_write_img_confirmed();
        if (!err) {
            needs_confirmation = false;
            diagnostics_record(DIAG_CONFIRMED, 0);
            LOG_INF("Firmware confirmed after startup and live sensor checks");
            return;
        }
        LOG_ERR("Image confirmation failed: %d", err);
    }
    if (k_uptime_get() >= deadline) {
        LOG_ERR("Startup deadline expired; rebooting for MCUboot rollback");
        diagnostics_reboot_event(DIAG_ROLLBACK_REQUESTED, 0);
        sys_reboot(SYS_REBOOT_COLD);
    }
}
