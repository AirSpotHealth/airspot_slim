/*
 * Copyright (c) 2025 AirSpot
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * SPI Flash Driver (AT25XE161D)
 * Renesas AT25XE161D - 16 Mbit (2 MB) SPI NOR Flash
 *
 * Reference: https://www.renesas.com/us/en/document/dst/at25xe161d-datasheet
 */

#ifndef SPI_FLASH_H
#define SPI_FLASH_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/* Flash geometry */
#define SPI_FLASH_SIZE (2 * 1024 * 1024) /* 2 MB */
#define SPI_FLASH_PAGE_SIZE 256
#define SPI_FLASH_SECTOR_SIZE 4096 /* 4 KB */
#define SPI_FLASH_BLOCK_SIZE 65536 /* 64 KB */

    /**
     * @brief Initialize SPI flash driver
     *
     * @return 0 on success, negative errno on failure
     */
    int spi_flash_init(void);
#if defined(CONFIG_AIRSPOT_FORENSIC_HISTORY)
    void spi_flash_forensic_wake_probe(void);
#endif

    /**
     * @brief Read flash device ID (JEDEC ID)
     *
     * @param manufacturer_id Pointer to store manufacturer ID
     * @param device_id Pointer to store device ID (2 bytes)
     * @return 0 on success, negative errno on failure
     */
    int spi_flash_read_id(uint8_t *manufacturer_id, uint16_t *device_id);

    /**
     * @brief Read data from flash
     *
     * @param addr Start address
     * @param data Buffer to store data
     * @param len Number of bytes to read
     * @return 0 on success, negative errno on failure
     */
    int spi_flash_read(uint32_t addr, uint8_t *data, size_t len);

    /**
     * @brief Write data to flash (page program)
     *
     * Note: Address must be erased first. Max 256 bytes per call.
     *
     * @param addr Start address (must be page-aligned for best performance)
     * @param data Data to write
     * @param len Number of bytes to write (max 256)
     * @return 0 on success, negative errno on failure
     */
    int spi_flash_write(uint32_t addr, const uint8_t *data, size_t len);

    /**
     * @brief Erase 4KB sector
     *
     * @param addr Any address within the sector
     * @return 0 on success, negative errno on failure
     */
    int spi_flash_erase_sector(uint32_t addr);

    /**
     * @brief Erase 64KB block
     *
     * @param addr Any address within the block
     * @return 0 on success, negative errno on failure
     */
    int spi_flash_erase_block(uint32_t addr);

    /**
     * @brief Erase entire chip
     *
     * Warning: Takes several seconds!
     *
     * @return 0 on success, negative errno on failure
     */
    int spi_flash_erase_chip(void);

    /**
     * @brief Enter deep power-down mode
     *
     * @return 0 on success, negative errno on failure
     */
    int spi_flash_power_down(void);

    /**
     * @brief Wake from deep power-down
     *
     * @return 0 on success, negative errno on failure
     */
    int spi_flash_power_up(void);

    /**
     * @brief Check if flash is busy (write/erase in progress)
     *
     * @return true if busy
     */
    bool spi_flash_is_busy(void);

#ifdef __cplusplus
}
#endif

#endif /* SPI_FLASH_H */
