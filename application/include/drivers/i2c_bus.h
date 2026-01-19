/*
 * Copyright (c) 2025 AirSpot
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * I2C Bus Driver for STCC4 Slim
 * Manages I2C bus shared by: STCC4 (CO2), SHT40 (temp/humidity), LPS22HH
 * (pressure)
 */

#ifndef I2C_BUS_H
#define I2C_BUS_H

#include <stdbool.h>
#include <stdint.h>
#include <zephyr/device.h>

#ifdef __cplusplus
extern "C"
{
#endif

/* I2C device addresses */
#define I2C_ADDR_STCC4 0x64   /* Sensirion STCC4 CO2 sensor */
#define I2C_ADDR_SHT40 0x44   /* Sensirion SHT40 temp/humidity */
#define I2C_ADDR_LPS22HH 0x5D /* ST LPS22HH pressure sensor */

    /**
     * @brief Initialize the I2C bus
     *
     * Initializes I2C on P2.03 (SCL), P2.04 (SDA) at 400kHz
     *
     * @return 0 on success, negative errno on failure
     */
    int i2c_bus_init(void);

    /**
     * @brief Check if I2C bus is initialized
     *
     * @return true if initialized
     */
    bool i2c_bus_is_ready(void);

    /**
     * @brief Write data to I2C device
     *
     * @param addr 7-bit I2C address
     * @param data Data buffer to write
     * @param len Number of bytes to write
     * @return 0 on success, negative errno on failure
     */
    int i2c_bus_write(uint8_t addr, const uint8_t *data, size_t len);

    /**
     * @brief Read data from I2C device
     *
     * @param addr 7-bit I2C address
     * @param data Buffer to store read data
     * @param len Number of bytes to read
     * @return 0 on success, negative errno on failure
     */
    int i2c_bus_read(uint8_t addr, uint8_t *data, size_t len);

    /**
     * @brief Write then read from I2C device (common pattern)
     *
     * @param addr 7-bit I2C address
     * @param write_data Data to write
     * @param write_len Number of bytes to write
     * @param read_data Buffer for read data
     * @param read_len Number of bytes to read
     * @return 0 on success, negative errno on failure
     */
    int i2c_bus_write_read(uint8_t addr, const uint8_t *write_data,
                           size_t write_len, uint8_t *read_data, size_t read_len);

    /**
     * @brief Scan I2C bus for devices
     *
     * For debugging - logs all responding addresses
     */
    void i2c_bus_scan(void);

#ifdef __cplusplus
}
#endif

#endif /* I2C_BUS_H */
