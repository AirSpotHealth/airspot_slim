/*
 * Copyright (c) 2025 AirSpot
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Sensor Manager - Coordinates all sensor readings
 *
 * Measurement workflow:
 * 1. Read SHT40 (temp/humidity) - required for STCC4 compensation
 * 2. Update STCC4 ambient conditions
 * 3. Read STCC4 (CO2)
 * 4. Read LPS22HH (pressure)
 * 5. Read battery voltage/percentage
 * 6. Read Qi charging status
 */

#ifndef SENSOR_MANAGER_H
#define SENSOR_MANAGER_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Slim fault codes do not overlap Screen's 0x01-0x0c codes. Wire byte 5
 * remains zero: Slim does not claim Screen's three-attempt recovery policy. */
void sensor_manager_get_initial_settling(bool *active, uint32_t *remaining_seconds);

enum sensor_fault {
  SENSOR_FAULT_STARTING = 0x40,
  SENSOR_FAULT_I2C = 0x41,
  SENSOR_FAULT_AMBIENT = 0x42,
  SENSOR_FAULT_PRESSURE = 0x43,
  SENSOR_FAULT_CO2 = 0x44,
  SENSOR_FAULT_COMPENSATION = 0x45,
};

/* Sensor data structure - all current readings */
typedef struct {
  /* Environmental */
  float temperature_c; /* °C from SHT40 */
  float humidity_rh;   /* %RH from SHT40 */
  uint16_t co2_ppm;    /* ppm from STCC4 */
  float pressure_hpa;  /* hPa from LPS22HH */

  /* Diagnostic evidence from this attempt; raw/status require flag bit 0. */
  int16_t raw_co2_ppm;
  uint16_t sensor_status;
  uint16_t sample_flags; /* 1=raw valid, 2=driver valid, 4=accepted,
                          8=conditioning complete, 16=continuous mode,
                          32=mode present, bits 6-8=selected mode (0..4),
                          512=native measurement mode labels */

  /* Power */
  uint16_t battery_mv;     /* mV */
  uint8_t battery_percent; /* 0-100% */
  bool is_charging;        /* Qi status */
  bool battery_low;        /* <15% */
  bool battery_critical;   /* <5% */

  /* Status */
  uint8_t error_code;    /* enum sensor_fault, zero for a valid sample */
  bool valid;            /* All sensors read successfully */
  uint32_t timestamp_ms; /* Uptime when measured */
} sensor_data_t;

/* Callback for new sensor data */
typedef void (*sensor_data_callback_t)(const sensor_data_t *data);

/**
 * @brief Initialize all sensors
 *
 * Initializes in correct order: I2C -> SHT40 -> STCC4 -> LPS22HH -> Battery ->
 * Qi
 *
 * @return 0 on success, negative errno on failure
 */
int sensor_manager_init(void);

/* Enabled after boot setup; stop() cancels recovery before sleeping sensors. */
void sensor_manager_start_recovery(void);
typedef struct {
  uint32_t total_attempts;
  uint16_t consecutive_attempts, retry_in_seconds;
  int16_t error;
  uint8_t stage; /* 0=no attempt, 1=running, 2=fresh sample verified, 3=failed */
  uint8_t fault; /* Fault which triggered the last attempt. */
} sensor_recovery_diagnostic_t;
void sensor_manager_get_recovery_diagnostic(sensor_recovery_diagnostic_t *out);

/* Last ASC-pause verification: stage 0=not attempted, 1=stop, 2=pause,
 * 3=single shot, 4=read/status, 5=verified, 6=ambient. Error is negative errno.
 * Status is the CRC-checked sensor word when available; otherwise zero. */
/* Snapshot of the last normal measurement attempt. Reading it never triggers
 * sensing or changes calibration. Raw/status are meaningful only with flag 1. */
typedef struct {
  uint32_t sequence, uptime_ms;
  int16_t error, raw_co2_ppm, temperature_centi;
  uint16_t status, humidity_centi, pressure_hpa;
  uint8_t flags; /* 1=CRC-checked raw, 2=driver policy valid, 4=accepted sample */
  uint8_t fault;
} sensor_co2_diagnostic_t;
typedef struct {
  uint8_t stage; int16_t error; uint16_t status;
  sensor_co2_diagnostic_t read;
} sensor_asc_diagnostic_t;
void sensor_manager_get_asc_diagnostic(sensor_asc_diagnostic_t *diagnostic);

/* Run the sensor's non-calibrating self-test, restoring its measurement mode.
 * Zero return means command and restoration succeeded; inspect result for faults.
 * 0x0000 and 0x0010 are passes (external SHT40 is on the host bus).
 * No settings or sensor calibration history are written. */
int sensor_manager_self_test(uint16_t *result);

/* Native ASC learning toggle; hardware verification + persistent transaction. */
int sensor_manager_change_native_asc(bool enabled);

/**
 * @brief Start periodic measurements
 *
 * @param interval_ms Measurement interval in milliseconds (min 1000)
 * @return 0 on success
 */
int sensor_manager_start(uint32_t interval_ms);

/**
 * @brief Stop work synchronously and sleep STCC4 for shutdown.
 * Call only from main; resume through normal reboot/initialization.
 */
void sensor_manager_stop(void);

/**
 * @brief Trigger single measurement
 *
 * @return 0 on success
 */
int sensor_manager_measure_now(void);

/**
 * @brief Get latest sensor data
 *
 * @param data Pointer to store data
 * @return 0 on success, -ENODATA if no measurement yet
 */
int sensor_manager_get_data(sensor_data_t *data);

/**
 * @brief Register callback for new data
 *
 * @param callback Function called when new data available
 */
void sensor_manager_register_callback(sensor_data_callback_t callback);

/**
 * @brief Set power mode (changes measurement interval)
 *
 * @param power_mode 0=on-demand (manual only), 1=low (3min), 2=mid (1min),
 * 3=high (5sec)
 */
int sensor_manager_set_power_mode(uint8_t power_mode);
/* Transactional user request: apply hardware, persist, restore on write error. */
int sensor_manager_change_power_mode(uint8_t power_mode);
void sensor_manager_factory_probe(uint8_t *sht_ok, uint8_t *lps_ok, uint8_t *stcc_ok);

/**
 * @brief Check if sensors are initialized
 */
bool sensor_manager_is_ready(void);

/**
 * @brief Reset CO2 sensor (STCC4)
 *
 * @return 0 on success, negative error code on failure
 */
int sensor_manager_reset_co2_sensor(void);

/** Explicit native FRC/ASC erase; retains selected ASC policy and power mode. */
int sensor_manager_reset_native_calibration(void);
/* Full factory reset: stop sampling and erase STCC4 without requiring LPS22HH. */
int sensor_manager_factory_erase_calibration(void);

/**
 * @brief Perform hardware forced recalibration (FRC) of the STCC4.
 *
 * Stops continuous measurement if active, issues FRC to @p target_ppm, then
 * restores continuous mode. The caller must have run the sensor long enough to
 * meet the datasheet FRC precondition (>=30 s continuous operation).
 *
 * @param target_ppm Reference CO2 in ppm (e.g. ~420 for fresh air).
 * @param applied_correction Optional out: correction applied by the sensor (ppm).
 * @return 0 on success; -EIO if the sensor rejected FRC; negative errno otherwise.
 */
int sensor_manager_perform_frc(uint16_t target_ppm, int16_t *applied_correction);


/* Last explicit native reset: stage 0 none, 1 pressure, 2 stop, 3 erase,
 * 4 ASC verification, 5 settings, 6 sampling, 7 complete.
 * Erase stage/result follow stcc4_reset_diagnostic_t. Errors are signed errno. */
typedef struct {
  uint8_t stage, erase_stage;
  uint16_t sensor_result;
  int16_t error, recovery_error;
} sensor_reset_diagnostic_t;
void sensor_manager_get_reset_diagnostic(sensor_reset_diagnostic_t *diagnostic);

#ifdef __cplusplus
}
#endif

#endif /* SENSOR_MANAGER_H */
