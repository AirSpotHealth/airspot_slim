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

/* SPI configuration - using SPI00 per device tree overlay */
#define SPI_NODE DT_NODELABEL(spi00)

/* CS, WP, HOLD GPIO */
#define CS_NODE DT_NODELABEL(gpio1)
#define CS_PIN 14   /* P1.14 */
#define WP_PIN 13   /* P1.13 */
#define HOLD_PIN 11 /* P1.11 */

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

static const struct device *spi_dev;
static const struct device *gpio_dev;
static bool initialized = false;

static struct spi_config spi_cfg = {
    .frequency = 8000000, /* 8 MHz */
    .operation = SPI_OP_MODE_MASTER | SPI_TRANSFER_MSB | SPI_WORD_SET(8) |
                 SPI_LINES_SINGLE,
    .slave = 0,
    .cs = {.gpio = {.port = NULL, .pin = CS_PIN, .dt_flags = GPIO_ACTIVE_LOW}},
};

static int spi_flash_cs(bool select)
{
  if (!gpio_dev)
    return -ENODEV;
  return gpio_pin_set(gpio_dev, CS_PIN, select ? 0 : 1);
}

static int spi_flash_transact(const uint8_t *tx, size_t tx_len, uint8_t *rx,
                              size_t rx_len)
{
  struct spi_buf tx_buf = {.buf = (void *)tx, .len = tx_len};
  struct spi_buf_set tx_set = {.buffers = &tx_buf, .count = 1};

  struct spi_buf rx_buf = {.buf = rx, .len = rx_len};
  struct spi_buf_set rx_set = {.buffers = &rx_buf, .count = rx ? 1 : 0};

  spi_flash_cs(true);
  int ret = spi_transceive(spi_dev, &spi_cfg, &tx_set, rx ? &rx_set : NULL);
  spi_flash_cs(false);

  return ret;
}

static int spi_flash_write_enable(void)
{
  uint8_t cmd = CMD_WRITE_ENABLE;
  return spi_flash_transact(&cmd, 1, NULL, 0);
}

static int spi_flash_wait_ready(uint32_t timeout_ms)
{
  uint8_t cmd = CMD_READ_STATUS;
  uint8_t status;
  uint32_t start = k_uptime_get_32();

  while ((k_uptime_get_32() - start) < timeout_ms)
  {
    spi_flash_cs(true);

    struct spi_buf tx_buf = {.buf = &cmd, .len = 1};
    struct spi_buf_set tx_set = {.buffers = &tx_buf, .count = 1};
    struct spi_buf rx_buf = {.buf = &status, .len = 1};
    struct spi_buf_set rx_set = {.buffers = &rx_buf, .count = 1};

    spi_transceive(spi_dev, &spi_cfg, &tx_set, &rx_set);
    spi_flash_cs(false);

    if (!(status & STATUS_WIP))
    {
      return 0;
    }
    k_msleep(1);
  }

  return -ETIMEDOUT;
}

int spi_flash_init(void)
{
  if (initialized)
  {
    return 0;
  }

  /* Initialize SPI */
  spi_dev = DEVICE_DT_GET(SPI_NODE);
  if (!device_is_ready(spi_dev))
  {
    LOG_ERR("SPI device not ready");
    return -ENODEV;
  }

  /* Initialize GPIO for CS, WP, HOLD */
  gpio_dev = DEVICE_DT_GET(CS_NODE);
  if (!device_is_ready(gpio_dev))
  {
    LOG_ERR("GPIO device not ready");
    return -ENODEV;
  }

  gpio_pin_configure(gpio_dev, CS_PIN, GPIO_OUTPUT_HIGH);
  gpio_pin_configure(gpio_dev, WP_PIN, GPIO_OUTPUT_HIGH);   /* Not protected */
  gpio_pin_configure(gpio_dev, HOLD_PIN, GPIO_OUTPUT_HIGH); /* Not held */

  /* Wake up flash if in power-down */
  spi_flash_power_up();
  k_msleep(1);

  /* Read and verify ID */
  uint8_t mfg_id;
  uint16_t dev_id;
  int ret = spi_flash_read_id(&mfg_id, &dev_id);
  if (ret)
  {
    LOG_ERR("Failed to read flash ID: %d", ret);
    return ret;
  }

  /* AT25XE161D: Manufacturer = 0x1F (Adesto/Renesas), Device = 0x4502 */
  LOG_INF("SPI Flash: Manufacturer 0x%02X, Device 0x%04X", mfg_id, dev_id);

  initialized = true;
  LOG_INF("SPI Flash initialized (2 MB AT25XE161D)");
  return 0;
}

int spi_flash_read_id(uint8_t *manufacturer_id, uint16_t *device_id)
{
  uint8_t cmd = CMD_READ_ID;
  uint8_t id[4]; /* Cmd + 3 bytes ID */

  spi_flash_cs(true);

  struct spi_buf tx_buf = {.buf = &cmd, .len = 1};
  struct spi_buf_set tx_set = {.buffers = &tx_buf, .count = 1};
  struct spi_buf rx_buf = {.buf = id, .len = 4};
  struct spi_buf_set rx_set = {.buffers = &rx_buf, .count = 1};

  int ret = spi_transceive(spi_dev, &spi_cfg, &tx_set, &rx_set);
  spi_flash_cs(false);

  if (ret == 0)
  {
    if (manufacturer_id)
      *manufacturer_id = id[1];
    if (device_id)
      *device_id = (id[2] << 8) | id[3];
  }

  return ret;
}

int spi_flash_read(uint32_t addr, uint8_t *data, size_t len)
{
  if (!initialized)
    return -ENODEV;
  if (!data || len == 0)
    return -EINVAL;
  if (addr + len > SPI_FLASH_SIZE)
    return -EINVAL;

  uint8_t cmd[4] = {CMD_READ_DATA, (addr >> 16) & 0xFF, (addr >> 8) & 0xFF,
                    addr & 0xFF};

  spi_flash_cs(true);

  struct spi_buf tx_bufs[2] = {{.buf = cmd, .len = 4},
                               {.buf = NULL, .len = len}};
  struct spi_buf_set tx_set = {.buffers = tx_bufs, .count = 2};

  struct spi_buf rx_bufs[2] = {{.buf = NULL, .len = 4},
                               {.buf = data, .len = len}};
  struct spi_buf_set rx_set = {.buffers = rx_bufs, .count = 2};

  int ret = spi_transceive(spi_dev, &spi_cfg, &tx_set, &rx_set);
  spi_flash_cs(false);

  return ret;
}

int spi_flash_write(uint32_t addr, const uint8_t *data, size_t len)
{
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

  spi_flash_cs(true);

  struct spi_buf tx_bufs[2] = {{.buf = cmd, .len = 4},
                               {.buf = (void *)data, .len = len}};
  struct spi_buf_set tx_set = {.buffers = tx_bufs, .count = 2};

  ret = spi_write(spi_dev, &spi_cfg, &tx_set);
  spi_flash_cs(false);

  if (ret)
    return ret;

  return spi_flash_wait_ready(PAGE_PROGRAM_TIME_MS * 2);
}

int spi_flash_erase_sector(uint32_t addr)
{
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

int spi_flash_erase_block(uint32_t addr)
{
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

int spi_flash_erase_chip(void)
{
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

int spi_flash_power_down(void)
{
  uint8_t cmd = CMD_POWER_DOWN;
  return spi_flash_transact(&cmd, 1, NULL, 0);
}

int spi_flash_power_up(void)
{
  uint8_t cmd = CMD_RELEASE_PD;
  return spi_flash_transact(&cmd, 1, NULL, 0);
}

bool spi_flash_is_busy(void)
{
  uint8_t cmd = CMD_READ_STATUS;
  uint8_t status = 0xFF;

  spi_flash_transact(&cmd, 1, &status, 1);
  return (status & STATUS_WIP) != 0;
}
