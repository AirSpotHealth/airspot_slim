/*
 * Copyright (c) 2025 AirSpot
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * SPI Flash Driver Implementation (AT25XE161D)
 *
 * Reference: Renesas AT25XE161D Datasheet
 * https://www.renesas.com/us/en/document/dst/at25xe161d-datasheet
 */

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <drivers/spi_flash.h>

LOG_MODULE_REGISTER(spi_flash, CONFIG_AIRSPOT_LOG_LEVEL);

/* SPI configuration - using device tree spec (like working_minimal_uart) */
#define AT25_NODE DT_NODELABEL(at25xe161d)

#if !DT_NODE_EXISTS(AT25_NODE)
#error "No at25xe161d node found in device tree"
#endif

static const struct spi_dt_spec spi_spec =
    SPI_DT_SPEC_GET(AT25_NODE, SPI_WORD_SET(8), 0);

/* WP and HOLD GPIO - controlled via gpio-hog in device tree, no manual control
 * needed */

/* AT25XE161D Commands */
#define CMD_READ_ID 0x9F
#define CMD_READ_STATUS 0x05
#define CMD_WRITE_ENABLE 0x06
#define CMD_WRITE_DISABLE 0x04
#define CMD_READ_DATA 0x03
#define CMD_PAGE_PROGRAM 0x02
#define CMD_SECTOR_ERASE 0x20   /* 4KB */
#define CMD_BLOCK_ERASE_32 0x52 /* 32KB */
#define CMD_BLOCK_ERASE_64 0xD8 /* 64KB */
#define CMD_CHIP_ERASE 0xC7
#define CMD_POWER_DOWN 0xB9
#define CMD_RELEASE_PD 0xAB

/* Status register bits */
#define STATUS_WIP 0x01 /* Write in progress */
#define STATUS_WEL 0x02 /* Write enable latch */

/* Timing */
#define PAGE_PROGRAM_TIME_MS 3
#define SECTOR_ERASE_TIME_MS 300
#define BLOCK_ERASE_TIME_MS 1000
#define CHIP_ERASE_TIME_MS 60000

static bool initialized = false;

/**
 * Simple SPI transact using device tree spec (handles CS automatically)
 */
static int spi_flash_transact(const uint8_t *tx, size_t tx_len, uint8_t *rx,
                              size_t rx_len) {
  struct spi_buf tx_buf = {.buf = (void *)tx, .len = tx_len};
  struct spi_buf_set tx_set = {.buffers = &tx_buf, .count = 1};

  struct spi_buf rx_buf = {.buf = rx, .len = rx_len};
  struct spi_buf_set rx_set = {.buffers = &rx_buf, .count = rx ? 1 : 0};

  /* spi_transceive_dt handles CS automatically via device tree */
  return spi_transceive_dt(&spi_spec, &tx_set, rx ? &rx_set : NULL);
}

static int spi_flash_write_enable(void) {
  uint8_t cmd = CMD_WRITE_ENABLE;
  return spi_flash_transact(&cmd, 1, NULL, 0);
}

static int spi_flash_wait_ready(uint32_t timeout_ms) {
  uint8_t tx_buf[2] = {CMD_READ_STATUS, 0x00};
  uint8_t rx_buf[2] = {0};
  uint32_t start = k_uptime_get_32();

  struct spi_buf tx = {.buf = tx_buf, .len = 2};
  struct spi_buf_set tx_set = {.buffers = &tx, .count = 1};
  struct spi_buf rx = {.buf = rx_buf, .len = 2};
  struct spi_buf_set rx_set = {.buffers = &rx, .count = 1};

  while ((k_uptime_get_32() - start) < timeout_ms) {
    int ret = spi_transceive_dt(&spi_spec, &tx_set, &rx_set);
    if (ret) {
      return ret;
    }

    /* Status is in rx_buf[1] (received during second byte) */
    if (!(rx_buf[1] & STATUS_WIP)) {
      return 0;
    }
    k_msleep(1);
  }

  return -ETIMEDOUT;
}

int spi_flash_init(void) {
  if (initialized) {
    return 0;
  }

  /* Check if SPI device tree spec is ready */
  if (!spi_is_ready_dt(&spi_spec)) {
    LOG_ERR("SPI device not ready (check device tree)");
    return -ENODEV;
  }

  LOG_INF("SPI Flash using device tree config (CS handled automatically)");
  LOG_INF("SPI frequency: %d Hz", spi_spec.config.frequency);

  /* WP and HOLD are controlled via gpio-hog in device tree */

  /* Wake up flash if in power-down mode */
  /* Multiple wake-up attempts with delay for robust initialization */
  for (int i = 0; i < 3; i++) {
    spi_flash_power_up();
    k_msleep(5); /* Give flash time to wake up */
  }

  /* Read and verify ID */
  uint8_t mfg_id;
  uint16_t dev_id;
  int ret = spi_flash_read_id(&mfg_id, &dev_id);
  if (ret) {
    LOG_ERR("Failed to read flash ID: %d", ret);
    return ret;
  }

  /* Reject missing/wrong parts before data_log can issue writes or erases.
   * AT25XE161D JEDEC identity is 1F 46 0C (datasheet section 10.1).
   */
  static const uint8_t expected_id[] = DT_PROP(AT25_NODE, jedec_id);
  BUILD_ASSERT(sizeof(expected_id) == 3, "Expected three-byte JEDEC identity");
  LOG_INF("SPI Flash: Manufacturer 0x%02X, Device 0x%04X", mfg_id, dev_id);
  if (mfg_id != expected_id[0] ||
      dev_id != ((uint16_t)expected_id[1] << 8 | expected_id[2])) {
    LOG_ERR("Unexpected flash identity; refusing storage access");
    return -ENODEV;
  }

  initialized = true;
  LOG_INF("SPI Flash initialized (2 MB AT25XE161D)");
  return 0;
}

int spi_flash_read_id(uint8_t *manufacturer_id, uint16_t *device_id) {
  /*
   * JEDEC Read ID (0x9F) returns 3 bytes: Manufacturer ID, Device ID (2 bytes)
   * For full-duplex SPI: send cmd + 3 dummy bytes, receive during those bytes
   */
  uint8_t tx_buf[4] = {CMD_READ_ID, 0x00, 0x00, 0x00};
  uint8_t rx_buf[4] = {0};

  struct spi_buf tx = {.buf = tx_buf, .len = 4};
  struct spi_buf_set tx_set = {.buffers = &tx, .count = 1};
  struct spi_buf rx = {.buf = rx_buf, .len = 4};
  struct spi_buf_set rx_set = {.buffers = &rx, .count = 1};

  /* spi_transceive_dt handles CS automatically */
  int ret = spi_transceive_dt(&spi_spec, &tx_set, &rx_set);

  if (ret == 0) {
    /* rx_buf[0] is during cmd byte (dummy), actual ID starts at [1] */
    if (manufacturer_id)
      *manufacturer_id = rx_buf[1];
    if (device_id)
      *device_id = (rx_buf[2] << 8) | rx_buf[3];

    LOG_INF("Flash ID raw: %02X %02X %02X %02X", rx_buf[0], rx_buf[1],
            rx_buf[2], rx_buf[3]);
  }

  return ret;
}

int spi_flash_read(uint32_t addr, uint8_t *data, size_t len) {
  if (!initialized)
    return -ENODEV;
  if (!data || len == 0)
    return -EINVAL;
  if (addr + len > SPI_FLASH_SIZE)
    return -EINVAL;

  uint8_t cmd[4] = {CMD_READ_DATA, (addr >> 16) & 0xFF, (addr >> 8) & 0xFF,
                    addr & 0xFF};

  struct spi_buf tx_bufs[2] = {{.buf = cmd, .len = 4},
                               {.buf = NULL, .len = len}};
  struct spi_buf_set tx_set = {.buffers = tx_bufs, .count = 2};

  struct spi_buf rx_bufs[2] = {{.buf = NULL, .len = 4},
                               {.buf = data, .len = len}};
  struct spi_buf_set rx_set = {.buffers = rx_bufs, .count = 2};

  return spi_transceive_dt(&spi_spec, &tx_set, &rx_set);
}

int spi_flash_write(uint32_t addr, const uint8_t *data, size_t len) {
  if (!initialized)
    return -ENODEV;
  if (!data || len == 0 || len > SPI_FLASH_PAGE_SIZE)
    return -EINVAL;
  if (addr + len > SPI_FLASH_SIZE)
    return -EINVAL;

  int ret = spi_flash_write_enable();
  if (ret)
    return ret;

  uint8_t cmd[4] = {CMD_PAGE_PROGRAM, (addr >> 16) & 0xFF, (addr >> 8) & 0xFF,
                    addr & 0xFF};

  struct spi_buf tx_bufs[2] = {{.buf = cmd, .len = 4},
                               {.buf = (void *)data, .len = len}};
  struct spi_buf_set tx_set = {.buffers = tx_bufs, .count = 2};

  ret = spi_write_dt(&spi_spec, &tx_set);
  if (ret)
    return ret;

  return spi_flash_wait_ready(PAGE_PROGRAM_TIME_MS * 2);
}

int spi_flash_erase_sector(uint32_t addr) {
  if (!initialized)
    return -ENODEV;

  int ret = spi_flash_write_enable();
  if (ret)
    return ret;

  uint8_t cmd[4] = {CMD_SECTOR_ERASE, (addr >> 16) & 0xFF, (addr >> 8) & 0xFF,
                    addr & 0xFF};

  ret = spi_flash_transact(cmd, 4, NULL, 0);
  if (ret)
    return ret;

  return spi_flash_wait_ready(SECTOR_ERASE_TIME_MS);
}

int spi_flash_erase_block(uint32_t addr) {
  if (!initialized)
    return -ENODEV;

  int ret = spi_flash_write_enable();
  if (ret)
    return ret;

  uint8_t cmd[4] = {CMD_BLOCK_ERASE_64, (addr >> 16) & 0xFF, (addr >> 8) & 0xFF,
                    addr & 0xFF};

  ret = spi_flash_transact(cmd, 4, NULL, 0);
  if (ret)
    return ret;

  return spi_flash_wait_ready(BLOCK_ERASE_TIME_MS);
}

int spi_flash_erase_chip(void) {
  if (!initialized)
    return -ENODEV;

  int ret = spi_flash_write_enable();
  if (ret)
    return ret;

  uint8_t cmd = CMD_CHIP_ERASE;
  ret = spi_flash_transact(&cmd, 1, NULL, 0);
  if (ret)
    return ret;

  LOG_WRN("Chip erase started - this will take ~60 seconds");
  return spi_flash_wait_ready(CHIP_ERASE_TIME_MS);
}

int spi_flash_power_down(void) {
  uint8_t cmd = CMD_POWER_DOWN;
  return spi_flash_transact(&cmd, 1, NULL, 0);
}

int spi_flash_power_up(void) {
  uint8_t cmd = CMD_RELEASE_PD;
  return spi_flash_transact(&cmd, 1, NULL, 0);
}

bool spi_flash_is_busy(void) {
  uint8_t cmd = CMD_READ_STATUS;
  uint8_t status = 0xFF;

  spi_flash_transact(&cmd, 1, &status, 1);
  return (status & STATUS_WIP) != 0;
}
