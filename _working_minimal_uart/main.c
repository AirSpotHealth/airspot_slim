/*
 * Copyright (c) 2012-2014 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/kernel.h>
#include <zephyr/pm/device.h>

#include <zephyr/sys/printk.h>

#if defined(CONFIG_SHELL)
#include <zephyr/shell/shell.h>
#endif
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/sys/util.h>


// Get the GPIO devices from devicetree
#define MY_GPIO_NODE DT_NODELABEL(my_gpio_pin)
static const struct gpio_dt_spec my_gpio = GPIO_DT_SPEC_GET(MY_GPIO_NODE, gpios);

#define LED1_NODE DT_NODELABEL(ah01_led1_pin)
static const struct gpio_dt_spec led1 = GPIO_DT_SPEC_GET(LED1_NODE, gpios);

#define LED2_NODE DT_NODELABEL(ah01_led2_pin)
static const struct gpio_dt_spec led2 = GPIO_DT_SPEC_GET(LED2_NODE, gpios);

#define ADC_EN_NODE DT_NODELABEL(ah01_adc_en_pin)
static const struct gpio_dt_spec adc_en = GPIO_DT_SPEC_GET(ADC_EN_NODE, gpios);

#define BUTTON_NODE DT_NODELABEL(ah01_button_pin)
static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET(BUTTON_NODE, gpios);

static struct gpio_callback button_cb_data;
static bool leds_latched_on = false;
static bool button_prev_state = false;

// Get the I2C device from devicetree
#define I2C_TEST_DEVICE DT_NODELABEL(i2c_test_device)
static const struct device *i2c_dev = DEVICE_DT_GET(DT_BUS(I2C_TEST_DEVICE));
static const struct i2c_dt_spec i2c_spec = I2C_DT_SPEC_GET(I2C_TEST_DEVICE);


// Get the SPI device from devicetree (AT25XE161D flash)
#if DT_NODE_EXISTS(DT_NODELABEL(at25xe161d))
#define AT25_DEVICE DT_NODELABEL(at25xe161d)
#define HAS_SPI_FLASH 1
static const struct device *spi_dev = DEVICE_DT_GET(DT_BUS(AT25_DEVICE));
static const struct spi_dt_spec spi_spec = SPI_DT_SPEC_GET(AT25_DEVICE, SPI_WORD_SET(8), 0);
#else
#define HAS_SPI_FLASH 0
#warning "No SPI flash device (at25xe161d) found in device tree - SPI flash features disabled"
#endif

// BLE variables
static struct bt_conn *current_conn;
static bool ble_connected = false;

// Custom service UUID (16-bit for simplicity)
#define BT_UUID_CUSTOM_SERVICE_VAL \
	BT_UUID_128_ENCODE(0x12345678, 0x1234, 0x5678, 0x1234, 0x56789abcdef0)

#define BT_UUID_CUSTOM_SERVICE   BT_UUID_DECLARE_128(BT_UUID_CUSTOM_SERVICE_VAL)

// Custom characteristic UUID
#define BT_UUID_CUSTOM_CHAR_VAL \
	BT_UUID_128_ENCODE(0x12345678, 0x1234, 0x5678, 0x1234, 0x56789abcdef1)

#define BT_UUID_CUSTOM_CHAR      BT_UUID_DECLARE_128(BT_UUID_CUSTOM_CHAR_VAL)

// Data buffer for sending/receiving
#define DATA_BUFFER_SIZE 244
static uint8_t data_buffer[DATA_BUFFER_SIZE];
static uint16_t data_length = 0;

// BLE advertising data
static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME, sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};

// BLE advertising parameters (2 second interval)
// BLE advertising interval is in units of 0.625ms
// 2 seconds = 2000ms = 2000/0.625 = 3200 units
static const struct bt_le_adv_param adv_param = {
	.options = BT_LE_ADV_OPT_CONN,
	.interval_min = 3200,  // 2 seconds
	.interval_max = 3200,  // 2 seconds
};

// ADC configuration check
#if !DT_NODE_EXISTS(DT_PATH(zephyr_user)) || \
	!DT_NODE_HAS_PROP(DT_PATH(zephyr_user), io_channels)
#error "No suitable devicetree overlay specified"
#endif

#define DT_SPEC_AND_COMMA(node_id, prop, idx) \
	ADC_DT_SPEC_GET_BY_IDX(node_id, idx),

/* Data of ADC io-channels specified in devicetree. */
static const struct adc_dt_spec adc_channels[] = {
	DT_FOREACH_PROP_ELEM(DT_PATH(zephyr_user), io_channels,
			     DT_SPEC_AND_COMMA)
};

// ADC enable pin control functions
static void adc_en_enable(void)
{
	gpio_pin_set_dt(&adc_en, 1);
	printk("ADC enable pin (P1.04) enabled\n");
}

static void adc_en_disable(void)
{
	gpio_pin_set_dt(&adc_en, 0);
	printk("ADC enable pin (P1.04) disabled\n");
}

// Shell command functions (only compiled if shell is enabled)
#if defined(CONFIG_SHELL)

// Button callback to latch LEDs on/off
static void button_pressed(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(cb);
	ARG_UNUSED(pins);

	leds_latched_on = !leds_latched_on;

	gpio_pin_set_dt(&my_gpio, leds_latched_on);
	gpio_pin_set_dt(&led1, leds_latched_on);
	gpio_pin_set_dt(&led2, leds_latched_on);
}

static int cmd_gpio_on(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	gpio_pin_set_dt(&my_gpio, 1);
	shell_print(sh, "GPIO P0.02 turned ON");
	return 0;
}

static int cmd_gpio_off(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	gpio_pin_set_dt(&my_gpio, 0);
	shell_print(sh, "GPIO P0.02 turned OFF");
	return 0;
}

static int cmd_gpio_toggle(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	static bool gpio_state = false;
	gpio_state = !gpio_state;
	gpio_pin_set_dt(&my_gpio, gpio_state);
	shell_print(sh, "GPIO P0.02 toggled to %s", gpio_state ? "ON" : "OFF");
	return 0;
}

static int cmd_leds_toggle(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	leds_latched_on = !leds_latched_on;

	gpio_pin_set_dt(&my_gpio, leds_latched_on);
	gpio_pin_set_dt(&led1, leds_latched_on);
	gpio_pin_set_dt(&led2, leds_latched_on);

	shell_print(sh, "All LEDs toggled %s", leds_latched_on ? "ON" : "OFF");
	return 0;
}

static int cmd_adc_en_enable(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	adc_en_enable();
	shell_print(sh, "ADC enable pin turned ON");
	return 0;
}

static int cmd_adc_en_disable(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	adc_en_disable();
	shell_print(sh, "ADC enable pin turned OFF");
	return 0;
}

static int cmd_button_read(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	int val = gpio_pin_get_dt(&button);

	if (val < 0) {
		shell_print(sh, "Button read failed (err=%d)", val);
	} else {
		shell_print(sh, "Button GPIO value: %d (%s)", val,
			    val ? "HIGH" : "LOW");
	}

	return 0;
}

static int cmd_i2c_write(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	uint8_t test_data[] = {0x01, 0x02, 0x03, 0x04};
	int err = i2c_write_dt(&i2c_spec, test_data, sizeof(test_data));

	if (err == 0) {
		shell_print(sh, "I2C write successful");
	} else {
		shell_print(sh, "I2C write failed: %d", err);
	}
	return 0;
}

static int cmd_i2c_read(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	uint8_t read_data[4];
	int err = i2c_read_dt(&i2c_spec, read_data, sizeof(read_data));

	if (err == 0) {
		shell_print(sh, "I2C read successful: %02X %02X %02X %02X",
			   read_data[0], read_data[1], read_data[2], read_data[3]);
	} else {
		shell_print(sh, "I2C read failed: %d", err);
	}
	return 0;
}

static int cmd_i2c_scan(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	if (!device_is_ready(i2c_dev)) {
		shell_print(sh, "I2C controller not ready");
		return 0;
	}

	shell_print(sh, "Scanning I2C bus for devices...");

	uint8_t dummy = 0x00;
	int found = 0;

	for (uint8_t addr = 0x03; addr <= 0x77; addr++) {
		/* Try a write of 1 dummy byte to probe the address */
		int err = i2c_write(i2c_dev, &dummy, 1, addr);

		if (err == 0) {
			shell_print(sh, "  Found device at 0x%02X", addr);
			found++;
		}
	}

	if (found == 0) {
		shell_print(sh, "No I2C devices found");
	} else {
		shell_print(sh, "Total I2C devices found: %d", found);
	}

	return 0;
}

static int cmd_sht40_test(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	if (!device_is_ready(i2c_dev)) {
		shell_print(sh, "I2C controller not ready");
		return 0;
	}

	// Send measure T & RH with high precision command (0xFD)
	uint8_t cmd = 0xFD;
	int err = i2c_write(i2c_dev, &cmd, 1, 0x44);

	if (err != 0) {
		shell_print(sh, "SHT40 test: write command FAILED (err=%d)", err);
		return 0;
	}

	// Wait for measurement (typ 8.3ms for high precision)
	k_msleep(10);

	// Read 6 bytes (temp + CRC + humidity + CRC)
	uint8_t data[6];
	err = i2c_read(i2c_dev, data, 6, 0x44);

	if (err == 0) {
		shell_print(sh, "SHT40 test: read OK - data: %02X %02X %02X %02X %02X %02X",
			data[0], data[1], data[2], data[3], data[4], data[5]);
	} else {
		shell_print(sh, "SHT40 test: read FAILED (err=%d)", err);
	}

	return 0;
}

static int cmd_stcc4_test(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	if (!device_is_ready(i2c_dev)) {
		shell_print(sh, "I2C controller not ready");
		return 0;
	}

	// Send get_product_ID command (0x365B)
	uint8_t cmd[2] = {0x36, 0x5B};
	int err = i2c_write(i2c_dev, cmd, 2, 0x64);

	if (err != 0) {
		shell_print(sh, "STCC4 test: write command FAILED (err=%d)", err);
		return 0;
	}

	// Wait a bit for response
	k_msleep(1);

	// Read response (product ID + CRC, typically 6 or 9 bytes)
	uint8_t data[9];
	err = i2c_read(i2c_dev, data, 9, 0x64);

	if (err == 0) {
		shell_print(sh, "STCC4 test: read OK - data: %02X %02X %02X %02X %02X %02X %02X %02X %02X",
			data[0], data[1], data[2], data[3], data[4], data[5], data[6], data[7], data[8]);
	} else {
		shell_print(sh, "STCC4 test: read FAILED (err=%d)", err);
	}

	return 0;
}

static int cmd_lps_test(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	if (!device_is_ready(i2c_dev)) {
		shell_print(sh, "I2C controller not ready");
		return 0;
	}

	// Read WHO_AM_I register at 0x0F (should return 0xB3)
	uint8_t reg = 0x0F;
	uint8_t data;
	int err = i2c_write_read(i2c_dev, 0x5D, &reg, 1, &data, 1);

	if (err == 0) {
		shell_print(sh, "LPS test: read OK - WHO_AM_I = 0x%02X (expected 0xB3)", data);
	} else {
		shell_print(sh, "LPS test: read FAILED (err=%d)", err);
	}

	return 0;
}

static int cmd_sensor_sample(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	if (!device_is_ready(i2c_dev)) {
		shell_print(sh, "I2C controller not ready");
		return 0;
	}

	shell_print(sh, "=== Sensor Sampling ===\n");

	// ========== SHT40 Temperature & Humidity ==========
	shell_print(sh, "SHT40 (0x44): Temperature & Humidity");
	uint8_t sht40_cmd = 0xFD;  // High precision measurement
	int err = i2c_write(i2c_dev, &sht40_cmd, 1, 0x44);

	if (err == 0) {
		k_msleep(10);  // Wait for measurement

		uint8_t sht40_data[6];
		err = i2c_read(i2c_dev, sht40_data, 6, 0x44);

		if (err == 0) {
			// Convert temperature: T = -45 + 175 * (raw / 65535)
			uint16_t temp_raw = (sht40_data[0] << 8) | sht40_data[1];
			float temperature = -45.0f + 175.0f * (temp_raw / 65535.0f);

			// Convert humidity: RH = -6 + 125 * (raw / 65535)
			uint16_t rh_raw = (sht40_data[3] << 8) | sht40_data[4];
			float humidity = -6.0f + 125.0f * (rh_raw / 65535.0f);

			shell_print(sh, "  Temperature: %.2f °C", temperature);
			shell_print(sh, "  Humidity: %.2f %%RH", humidity);
			shell_print(sh, "  Raw: T=0x%04X RH=0x%04X", temp_raw, rh_raw);
		} else {
			shell_print(sh, "  Read FAILED (err=%d)", err);
		}
	} else {
		shell_print(sh, "  Write command FAILED (err=%d)", err);
	}

	shell_print(sh, "");

	// ========== STCC4 (SCD4x) CO2 Sensor ==========
	shell_print(sh, "STCC4/SCD4x (0x64): CO2 Sensor");

	// Send measure_single_shot command (0x219D)
	uint8_t stcc4_cmd[2] = {0x21, 0x9D};
	err = i2c_write(i2c_dev, stcc4_cmd, 2, 0x64);

	if (err == 0) {
		shell_print(sh, "  Measurement started, waiting 5 seconds...");
		k_msleep(5000);  // Single shot takes ~5 seconds

		// Send read_measurement command (0xEC05)
		uint8_t read_cmd[2] = {0xEC, 0x05};
		err = i2c_write(i2c_dev, read_cmd, 2, 0x64);

		if (err == 0) {
			k_msleep(1);

			uint8_t stcc4_data[9];  // CO2(2) + CRC + Temp(2) + CRC + RH(2) + CRC
			err = i2c_read(i2c_dev, stcc4_data, 9, 0x64);

			if (err == 0) {
				// Extract CO2 (ppm)
				uint16_t co2_raw = (stcc4_data[0] << 8) | stcc4_data[1];

				// Extract temperature: T = -45 + 175 * (raw / 65536)
				uint16_t temp_raw = (stcc4_data[3] << 8) | stcc4_data[4];
				float temperature = -45.0f + 175.0f * (temp_raw / 65536.0f);

				// Extract humidity: RH = 100 * (raw / 65536)
				uint16_t rh_raw = (stcc4_data[6] << 8) | stcc4_data[7];
				float humidity = 100.0f * (rh_raw / 65536.0f);

				shell_print(sh, "  CO2: %u ppm", co2_raw);
				shell_print(sh, "  Temperature: %.2f °C", temperature);
				shell_print(sh, "  Humidity: %.2f %%RH", humidity);
			} else {
				shell_print(sh, "  Read measurement FAILED (err=%d)", err);
			}
		} else {
			shell_print(sh, "  Read command FAILED (err=%d)", err);
		}
	} else {
		shell_print(sh, "  Single shot command FAILED (err=%d)", err);
	}

	shell_print(sh, "\n=== Sampling Complete ===");
	return 0;
}

static int cmd_adc_read(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	uint16_t buf;
	struct adc_sequence sequence = {
		.buffer = &buf,
		.buffer_size = sizeof(buf),
	};

	shell_print(sh, "ADC reading:");
	for (size_t i = 0U; i < ARRAY_SIZE(adc_channels); i++) {
		int32_t val_mv;

		shell_print(sh, "- %s, channel %d: ",
			   adc_channels[i].dev->name,
			   adc_channels[i].channel_id);

		(void)adc_sequence_init_dt(&adc_channels[i], &sequence);

		int err = adc_read_dt(&adc_channels[i], &sequence);
		if (err < 0) {
			shell_print(sh, "Could not read (%d)", err);
			continue;
		}

		if (adc_channels[i].channel_cfg.differential) {
			val_mv = (int32_t)((int16_t)buf);
		} else {
			val_mv = (int32_t)buf;
		}
		shell_print(sh, "  Raw: %"PRId32, val_mv);

		err = adc_raw_to_millivolts_dt(&adc_channels[i], &val_mv);
		if (err < 0) {
			shell_print(sh, "  (voltage conversion not available)");
		} else {
			shell_print(sh, "  Voltage: %"PRId32" mV", val_mv);
		}
	}
	return 0;
}

#if HAS_SPI_FLASH
// AT25XE161D: Read JEDEC ID (should return 1F 44 15)
static int cmd_at25_jedec_id(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	uint8_t cmd = 0x9F; // JEDEC ID read command
	uint8_t rx_data[4] = {0};

	struct spi_buf tx_buf = {
		.buf = &cmd,
		.len = 1
	};

	struct spi_buf rx_buf = {
		.buf = rx_data,
		.len = sizeof(rx_data)
	};

	struct spi_buf_set tx_set = {
		.buffers = &tx_buf,
		.count = 1
	};

	struct spi_buf_set rx_set = {
		.buffers = &rx_buf,
		.count = 1
	};

	int err = spi_transceive_dt(&spi_spec, &tx_set, &rx_set);

	if (err == 0) {
		shell_print(sh, "AT25XE161D JEDEC ID: %02X %02X %02X", rx_data[1], rx_data[2], rx_data[3]);
		if (rx_data[1] == 0x1F && rx_data[2] == 0x46 && rx_data[3] == 0x01) {
			shell_print(sh, "✓ AT25XE161D detected correctly!");
		} else {
			shell_print(sh, "⚠ Unexpected JEDEC ID (expected: 1F 44 15)");
		}
	} else {
		shell_print(sh, "✗ JEDEC ID read failed: %d", err);
	}
	return 0;
}

// AT25XE161D: Read Status Register
static int cmd_at25_status(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	uint8_t cmd = 0x05; // Read Status Register command
	uint8_t status[2] = {0};

	struct spi_buf tx_buf = {
		.buf = &cmd,
		.len = 1
	};

	struct spi_buf rx_buf = {
		.buf = status,
		.len = sizeof(status)
	};

	struct spi_buf_set tx_set = {
		.buffers = &tx_buf,
		.count = 1
	};

	struct spi_buf_set rx_set = {
		.buffers = &rx_buf,
		.count = 1
	};

	int err = spi_transceive_dt(&spi_spec, &tx_set, &rx_set);

	if (err == 0) {
		shell_print(sh, "Status Register: 0x%02X", status[1]);
		shell_print(sh, "  Busy (WIP): %s", (status[1] & 0x01) ? "Yes" : "No");
		shell_print(sh, "  Write Enable (WEL): %s", (status[1] & 0x02) ? "Yes" : "No");
		shell_print(sh, "  Block Protect bits: 0x%02X", (status[1] >> 2) & 0x0F);
	} else {
		shell_print(sh, "✗ Status register read failed: %d", err);
	}
	return 0;
}

// AT25XE161D: Read 16 bytes from address 0x000000
static int cmd_at25_read_data(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	// Read command: 0x03 (Read Data Bytes) + 3 address bytes
	uint8_t cmd[4] = {0x03, 0x00, 0x00, 0x00};
	uint8_t rx_data[20] = {0}; // 4 dummy + 16 data

	struct spi_buf tx_buf = {
		.buf = cmd,
		.len = sizeof(cmd)
	};

	struct spi_buf rx_buf = {
		.buf = rx_data,
		.len = sizeof(rx_data)
	};

	struct spi_buf_set tx_set = {
		.buffers = &tx_buf,
		.count = 1
	};

	struct spi_buf_set rx_set = {
		.buffers = &rx_buf,
		.count = 1
	};

	int err = spi_transceive_dt(&spi_spec, &tx_set, &rx_set);

	if (err == 0) {
		shell_print(sh, "Read 16 bytes from address 0x000000:");
		shell_print(sh, "Hex: ");
		for (int i = 4; i < 20; i++) {
			shell_print(sh, "%02X ", rx_data[i]);
		}
		shell_print(sh, "");
	} else {
		shell_print(sh, "✗ Data read failed: %d", err);
	}
	return 0;
}

// AT25XE161D: Comprehensive test
static int cmd_at25_test(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	shell_print(sh, "=== AT25XE161D Flash Test ===");

	// Test 1: JEDEC ID Read
	shell_print(sh, "Test 1: Reading JEDEC ID...");
	uint8_t jedec_cmd = 0x9F;
	uint8_t jedec_response[4] = {0};

	struct spi_buf tx_buf = {
		.buf = &jedec_cmd,
		.len = 1
	};

	struct spi_buf rx_buf = {
		.buf = jedec_response,
		.len = sizeof(jedec_response)
	};

	struct spi_buf_set tx_set = {
		.buffers = &tx_buf,
		.count = 1
	};

	struct spi_buf_set rx_set = {
		.buffers = &rx_buf,
		.count = 1
	};

	int err = spi_transceive_dt(&spi_spec, &tx_set, &rx_set);

	if (err == 0) {
		shell_print(sh, "JEDEC ID: %02X %02X %02X",
			   jedec_response[1], jedec_response[2], jedec_response[3]);

		// Check if it matches AT25XE161D expected ID
		if (jedec_response[1] == 0x1F && jedec_response[2] == 0x44 && jedec_response[3] == 0x15) {
			shell_print(sh, "✓ AT25XE161D flash detected correctly!");
		} else {
			shell_print(sh, "⚠ Unexpected JEDEC ID (expected: 1F 44 15)");
		}
	} else {
		shell_print(sh, "✗ JEDEC ID read failed: %d", err);
		shell_print(sh, "=== Test Aborted ===");
		return 0;
	}

	// Test 2: Status Register Read
	shell_print(sh, "Test 2: Reading Status Register...");
	uint8_t status_cmd = 0x05;
	uint8_t status_response[2] = {0};

	tx_buf.buf = &status_cmd;
	tx_buf.len = 1;
	rx_buf.buf = status_response;
	rx_buf.len = sizeof(status_response);

	err = spi_transceive_dt(&spi_spec, &tx_set, &rx_set);

	if (err == 0) {
		shell_print(sh, "Status Register: 0x%02X", status_response[1]);
		shell_print(sh, "  Busy (WIP): %s", (status_response[1] & 0x01) ? "Yes" : "No");
		shell_print(sh, "  Write Enable: %s", (status_response[1] & 0x02) ? "Yes" : "No");
	} else {
		shell_print(sh, "✗ Status register read failed: %d", err);
	}

	// Test 3: Read first 16 bytes
	shell_print(sh, "Test 3: Reading first 16 bytes...");
	uint8_t read_cmd[4] = {0x03, 0x00, 0x00, 0x00};
	uint8_t read_data[20] = {0};

	tx_buf.buf = read_cmd;
	tx_buf.len = sizeof(read_cmd);
	rx_buf.buf = read_data;
	rx_buf.len = sizeof(read_data);

	err = spi_transceive_dt(&spi_spec, &tx_set, &rx_set);

	if (err == 0) {
		shell_print(sh, "Data from 0x000000:");
		for (int i = 4; i < 20; i++) {
			shell_print(sh, "%02X ", read_data[i]);
		}
		shell_print(sh, "");
	} else {
		shell_print(sh, "✗ Data read failed: %d", err);
	}

	shell_print(sh, "=== AT25XE161D Test Complete ===");
	return 0;
}
#endif // HAS_SPI_FLASH

// BLE connection callbacks
static void connected(struct bt_conn *conn, uint8_t err)
{
	char addr[BT_ADDR_LE_STR_LEN];

	if (err) {
		printk("Connection failed, err 0x%02x %s\n", err, bt_hci_err_to_str(err));
		return;
	}

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
	printk("Connected %s\n", addr);

	current_conn = bt_conn_ref(conn);
	ble_connected = true;
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
	printk("Disconnected: %s, reason 0x%02x %s\n", addr, reason, bt_hci_err_to_str(reason));

	if (current_conn) {
		bt_conn_unref(current_conn);
		current_conn = NULL;
		ble_connected = false;
	}
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected = connected,
	.disconnected = disconnected,
};

// GATT service callbacks
static ssize_t custom_char_read(struct bt_conn *conn, const struct bt_gatt_attr *attr,
				void *buf, uint16_t len, uint16_t offset)
{
	printk("Characteristic read: offset=%d, len=%d\n", offset, len);

	if (offset >= data_length) {
		return 0;
	}

	uint16_t to_copy = MIN(len, data_length - offset);
	memcpy(buf, &data_buffer[offset], to_copy);

	return to_copy;
}

static ssize_t custom_char_write(struct bt_conn *conn, const struct bt_gatt_attr *attr,
				 const void *buf, uint16_t len, uint16_t offset, uint8_t flags)
{
	printk("Characteristic write: offset=%d, len=%d\n", offset, len);

	if (offset + len > DATA_BUFFER_SIZE) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
	}

	memcpy(&data_buffer[offset], buf, len);

	if (offset + len > data_length) {
		data_length = offset + len;
	}

	printk("Received data: ");
	for (uint16_t i = 0; i < len; i++) {
		printk("%02X ", ((uint8_t*)buf)[i]);
	}
	printk("\n");

	return len;
}

// GATT service definition
BT_GATT_SERVICE_DEFINE(custom_service,
	BT_GATT_PRIMARY_SERVICE(BT_UUID_CUSTOM_SERVICE),
	BT_GATT_CHARACTERISTIC(BT_UUID_CUSTOM_CHAR,
			       BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE,
			       BT_GATT_PERM_READ | BT_GATT_PERM_WRITE,
			       custom_char_read, custom_char_write, NULL),
);

// BLE shell commands
static int cmd_ble_start_adv(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

   int adv_err = bt_le_adv_start(&adv_param, ad, ARRAY_SIZE(ad), NULL, 0);
   if (adv_err) {
      printf("Failed to start BLE advertising: %d\n", adv_err);
   } else {
      printf("BLE advertising started automatically (2s interval)\n");
   }
	return 0;
}

static int cmd_ble_stop_adv(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	int err = bt_le_adv_stop();

	if (err == 0) {
		shell_print(sh, "BLE advertising stopped");
	} else {
		shell_print(sh, "Failed to stop BLE advertising: %d", err);
	}
	return 0;
}

static int cmd_ble_status(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	shell_print(sh, "=== BLE Status ===");
	shell_print(sh, "Connected: %s", ble_connected ? "Yes" : "No");
	shell_print(sh, "Device Name: %s", CONFIG_BT_DEVICE_NAME);

	if (ble_connected && current_conn) {
		char addr[BT_ADDR_LE_STR_LEN];
		bt_addr_le_to_str(bt_conn_get_dst(current_conn), addr, sizeof(addr));
		shell_print(sh, "Connected to: %s", addr);
	}

	return 0;
}

static int cmd_ble_debug(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	shell_print(sh, "=== BLE Debug Info ===");
	shell_print(sh, "BT enabled: %s", bt_is_ready() ? "Yes" : "No");
	shell_print(sh, "Device name: %s", CONFIG_BT_DEVICE_NAME);
	shell_print(sh, "Device name length: %d", sizeof(CONFIG_BT_DEVICE_NAME) - 1);
	shell_print(sh, "Advertising data entries: %d", ARRAY_SIZE(ad));

	return 0;
}

static int cmd_ble_send_data(const struct shell *sh, size_t argc, char **argv)
{
	if (argc < 2) {
		shell_print(sh, "Usage: test ble_send_data <hex_data>");
		shell_print(sh, "Example: test ble_send_data 48656C6C6F");
		return 0;
	}

	if (!ble_connected) {
		shell_print(sh, "Error: No BLE connection");
		return 0;
	}

	// Parse hex string
	const char *hex_str = argv[1];
	size_t hex_len = strlen(hex_str);

	if (hex_len % 2 != 0) {
		shell_print(sh, "Error: Hex string must have even length");
		return 0;
	}

	size_t data_len = hex_len / 2;
	if (data_len > DATA_BUFFER_SIZE) {
		shell_print(sh, "Error: Data too long (max %d bytes)", DATA_BUFFER_SIZE);
		return 0;
	}

	// Convert hex to bytes
	uint8_t send_data[DATA_BUFFER_SIZE];
	for (size_t i = 0; i < data_len; i++) {
		char hex_byte[3] = {hex_str[i*2], hex_str[i*2+1], '\0'};
		send_data[i] = (uint8_t)strtol(hex_byte, NULL, 16);
	}

	// Store in buffer for reading
	memcpy(data_buffer, send_data, data_len);
	data_length = data_len;

	shell_print(sh, "Data stored in buffer (%d bytes):", data_len);
	for (size_t i = 0; i < data_len; i++) {
		shell_print(sh, "%02X ", send_data[i]);
	}
	shell_print(sh, "");
	shell_print(sh, "Client can now read this data from the characteristic");

	return 0;
}

static int cmd_ble_show_received(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	shell_print(sh, "=== Received Data ===");
	shell_print(sh, "Buffer length: %d bytes", data_length);

	if (data_length > 0) {
		shell_print(sh, "Data (hex): ");
		for (uint16_t i = 0; i < data_length; i++) {
			shell_print(sh, "%02X ", data_buffer[i]);
		}
		shell_print(sh, "");

		shell_print(sh, "Data (ASCII): ");
		for (uint16_t i = 0; i < data_length; i++) {
			if (data_buffer[i] >= 32 && data_buffer[i] <= 126) {
				shell_print(sh, "%c", data_buffer[i]);
			} else {
				shell_print(sh, ".");
			}
		}
		shell_print(sh, "");
	} else {
		shell_print(sh, "No data received yet");
	}

	return 0;
}

static int cmd_ble_clear_buffer(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	memset(data_buffer, 0, DATA_BUFFER_SIZE);
	data_length = 0;

	shell_print(sh, "Data buffer cleared");
	return 0;
}

static int cmd_pm_suspend_adc(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	int err = pm_device_action_run(adc_channels[0].dev, PM_DEVICE_ACTION_SUSPEND);

	if (err == 0) {
		shell_print(sh, "ADC suspended successfully");
	} else {
		shell_print(sh, "Failed to suspend ADC: %d", err);
	}
	return 0;
}

static int cmd_pm_resume_adc(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	int err = pm_device_action_run(adc_channels[0].dev, PM_DEVICE_ACTION_RESUME);

	if (err == 0) {
		shell_print(sh, "ADC resumed successfully");
	} else {
		shell_print(sh, "Failed to resume ADC: %d", err);
	}
	return 0;
}

#if HAS_SPI_FLASH
static int cmd_pm_suspend_spi(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	int err = pm_device_action_run(spi_dev, PM_DEVICE_ACTION_SUSPEND);

	if (err == 0) {
		shell_print(sh, "SPI suspended successfully");
	} else {
		shell_print(sh, "Failed to suspend SPI: %d", err);
	}
	return 0;
}

static int cmd_pm_resume_spi(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	int err = pm_device_action_run(spi_dev, PM_DEVICE_ACTION_RESUME);

	if (err == 0) {
		shell_print(sh, "SPI resumed successfully");
	} else {
		shell_print(sh, "Failed to resume SPI: %d", err);
	}
	return 0;
}
#endif // HAS_SPI_FLASH

static int cmd_pm_suspend_i2c(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	int err = pm_device_action_run(i2c_dev, PM_DEVICE_ACTION_SUSPEND);

	if (err == 0) {
		shell_print(sh, "I2C suspended successfully");
	} else {
		shell_print(sh, "Failed to suspend I2C: %d", err);
	}
	return 0;
}

static int cmd_pm_resume_i2c(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	int err = pm_device_action_run(i2c_dev, PM_DEVICE_ACTION_RESUME);

	if (err == 0) {
		shell_print(sh, "I2C resumed successfully");
	} else {
		shell_print(sh, "Failed to resume I2C: %d", err);
	}
	return 0;
}

static int cmd_pm_info(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	shell_print(sh, "=== Power Management Info ===");
	shell_print(sh, "Use 'device list' to see all devices and their PM states");
	shell_print(sh, "Use 'pm suspend <device>' to suspend a device");
	shell_print(sh, "Use 'pm resume <device>' to resume a device");
	shell_print(sh, "");
	shell_print(sh, "Available devices for PM testing:");
	shell_print(sh, "- adc (ADC peripheral)");
	shell_print(sh, "- spi (SPI peripheral)");
	shell_print(sh, "- i2c (I2C peripheral)");
	shell_print(sh, "");
	shell_print(sh, "Example commands:");
	shell_print(sh, "  device list");
	shell_print(sh, "  pm suspend adc");
	shell_print(sh, "  pm resume adc");
	return 0;
}

static int cmd_status(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	shell_print(sh, "=== Device Status ===");
	shell_print(sh, "GPIO P0.02: %s", gpio_is_ready_dt(&my_gpio) ? "Ready" : "Not Ready");
	shell_print(sh, "I2C: %s", device_is_ready(i2c_dev) ? "Ready" : "Not Ready");
#if HAS_SPI_FLASH
	shell_print(sh, "SPI: %s", spi_is_ready_dt(&spi_spec) ? "Ready" : "Not Ready");
#endif
	shell_print(sh, "ADC: %d channels configured", ARRAY_SIZE(adc_channels));

	for (size_t i = 0U; i < ARRAY_SIZE(adc_channels); i++) {
		shell_print(sh, "  - %s, channel %d: %s",
			   adc_channels[i].dev->name,
			   adc_channels[i].channel_id,
			   adc_is_ready_dt(&adc_channels[i]) ? "Ready" : "Not Ready");
	}
	return 0;
}

// Shell command definitions
SHELL_STATIC_SUBCMD_SET_CREATE(sub_test,
	SHELL_CMD(gpio_on, NULL, "Turn GPIO P0.02 ON", cmd_gpio_on),
	SHELL_CMD(gpio_off, NULL, "Turn GPIO P0.02 OFF", cmd_gpio_off),
	SHELL_CMD(gpio_toggle, NULL, "Toggle GPIO P0.02", cmd_gpio_toggle),
	SHELL_CMD(leds_toggle, NULL, "Toggle all LEDs", cmd_leds_toggle),
	SHELL_CMD(adc_en_enable, NULL, "Enable ADC enable pin (P1.04)", cmd_adc_en_enable),
	SHELL_CMD(adc_en_disable, NULL, "Disable ADC enable pin (P1.04)", cmd_adc_en_disable),
	SHELL_CMD(button_read, NULL, "Read button GPIO level", cmd_button_read),
	SHELL_CMD(i2c_write, NULL, "Test I2C write", cmd_i2c_write),
	SHELL_CMD(i2c_read, NULL, "Test I2C read", cmd_i2c_read),
	SHELL_CMD(i2c_scan, NULL, "Scan I2C bus for devices", cmd_i2c_scan),
	SHELL_CMD(sht40_test, NULL, "Return SHT40 I2C address", cmd_sht40_test),
	SHELL_CMD(stcc4_test, NULL, "Return STCC4 I2C address", cmd_stcc4_test),
	SHELL_CMD(lps_test, NULL, "Return LPS I2C address", cmd_lps_test),
	SHELL_CMD(sensor_sample, NULL, "Sample SHT40 and STCC4 sensor data", cmd_sensor_sample),
#if HAS_SPI_FLASH
	SHELL_CMD(at25_jedec_id, NULL, "Read AT25XE161D JEDEC ID", cmd_at25_jedec_id),
	SHELL_CMD(at25_status, NULL, "Read AT25XE161D status register", cmd_at25_status),
	SHELL_CMD(at25_read_data, NULL, "Read 16 bytes from address 0x000000", cmd_at25_read_data),
	SHELL_CMD(at25_test, NULL, "Comprehensive AT25XE161D test", cmd_at25_test),
#endif
	SHELL_CMD(adc_read, NULL, "Read ADC values", cmd_adc_read),
	SHELL_CMD(pm_suspend_adc, NULL, "Suspend ADC for power saving", cmd_pm_suspend_adc),
	SHELL_CMD(pm_resume_adc, NULL, "Resume ADC from suspend", cmd_pm_resume_adc),
#if HAS_SPI_FLASH
	SHELL_CMD(pm_suspend_spi, NULL, "Suspend SPI for power saving", cmd_pm_suspend_spi),
	SHELL_CMD(pm_resume_spi, NULL, "Resume SPI from suspend", cmd_pm_resume_spi),
#endif
	SHELL_CMD(pm_suspend_i2c, NULL, "Suspend I2C for power saving", cmd_pm_suspend_i2c),
	SHELL_CMD(pm_resume_i2c, NULL, "Resume I2C from suspend", cmd_pm_resume_i2c),
	SHELL_CMD(pm_info, NULL, "Show power management info", cmd_pm_info),
	SHELL_CMD(ble_start_adv, NULL, "Start BLE advertising", cmd_ble_start_adv),
	SHELL_CMD(ble_stop_adv, NULL, "Stop BLE advertising", cmd_ble_stop_adv),
	SHELL_CMD(ble_status, NULL, "Show BLE connection status", cmd_ble_status),
	SHELL_CMD(ble_debug, NULL, "Show BLE debug information", cmd_ble_debug),
	SHELL_CMD(ble_send_data, NULL, "Send data via BLE (hex format)", cmd_ble_send_data),
	SHELL_CMD(ble_show_received, NULL, "Show received BLE data", cmd_ble_show_received),
	SHELL_CMD(ble_clear_buffer, NULL, "Clear BLE data buffer", cmd_ble_clear_buffer),
	SHELL_CMD(status, NULL, "Show device status", cmd_status),
	SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(test, &sub_test, "Test commands for GPIO, I2C, SPI, ADC, and BLE", NULL);

#endif /* CONFIG_SHELL */

int main(void)
{
	int err;

	printf("Hello World! %s\n", CONFIG_BOARD_TARGET);
	printf("GPIO + I2C + ADC + BLE Application\n");
	printf("GPIO Pin: P0.02, I2C SCL: P0.03, I2C SDA: P0.04, ADC: P1.06 (AIN2)\n");
	printf("UART: TX=P1.02, RX=P1.03\n");
#if HAS_SPI_FLASH
	printf("SPI: CS=P2.05, MOSI, MISO, SCK (MX25R64 Flash)\n");
#endif
	printf("BLE: Device Name: %s\n", CONFIG_BT_DEVICE_NAME);

#if defined(CONFIG_SHELL)
	printf("Shell enabled - Use 'test' commands to control devices. Type 'help' for available commands.\n");
#else
	printf("Shell disabled - Application running in production mode\n");
#endif

	// Check if GPIO device is ready
	if (!gpio_is_ready_dt(&my_gpio)) {
		printf("Error: GPIO device is not ready\n");
		return 0;
	}

	// Check if additional LED GPIO devices are ready
	if (!gpio_is_ready_dt(&led1)) {
		printf("Error: LED1 device is not ready\n");
		return 0;
	}

	if (!gpio_is_ready_dt(&led2)) {
		printf("Error: LED2 device is not ready\n");
		return 0;
	}

	// Check if ADC enable GPIO device is ready
	if (!gpio_is_ready_dt(&adc_en)) {
		printf("Error: ADC enable device is not ready\n");
		return 0;
	}

	// Check if button GPIO device is ready
	if (!gpio_is_ready_dt(&button)) {
		printf("Error: button device is not ready\n");
		return 0;
	}

	// Configure GPIO pin as output
	err = gpio_pin_configure_dt(&my_gpio, GPIO_OUTPUT_ACTIVE);
	if (err < 0) {
		printf("Error: Failed to configure GPIO pin\n");
		return 0;
	}

	// Configure additional LEDs as outputs, initially off
	err = gpio_pin_configure_dt(&led1, GPIO_OUTPUT_INACTIVE);
	if (err < 0) {
		printf("Error: Failed to configure LED1 pin\n");
		return 0;
	}

	err = gpio_pin_configure_dt(&led2, GPIO_OUTPUT_INACTIVE);
	if (err < 0) {
		printf("Error: Failed to configure LED2 pin\n");
		return 0;
	}

	// Configure ADC enable pin as output, initially off
	err = gpio_pin_configure_dt(&adc_en, GPIO_OUTPUT_INACTIVE);
	if (err < 0) {
		printf("Error: Failed to configure ADC enable pin\n");
		return 0;
	}

	// Configure button as input (flags from devicetree handle pull-up/active-low)
	err = gpio_pin_configure_dt(&button, GPIO_INPUT);
	if (err < 0) {
		printf("Error: Failed to configure button pin\n");
		return 0;
	}

	// Check if I2C device is ready
	if (!device_is_ready(i2c_dev)) {
		printf("Error: I2C device is not ready\n");
		return 0;
	}

#if HAS_SPI_FLASH
	// Check if SPI device is ready
	if (!spi_is_ready_dt(&spi_spec)) {
		printf("Error: SPI device is not ready\n");
		return 0;
	}
#endif

	// Initialize Bluetooth
	err = bt_enable(NULL);
	if (err) {
		printf("Error: Bluetooth init failed: %d\n", err);
		return 0;
	}
	printf("Bluetooth initialized successfully\n");

	// Start BLE advertising automatically (2 second interval)
printf("Before bt_le_adv_start\n");
printf("NOTE: bt_le_adv_start() temporarily disabled on custom PCB build (causes reset)\n");
/* TODO: Re-enable advertising once BLE is verified on the custom PCB
int adv_err = bt_le_adv_start(&adv_param, ad, ARRAY_SIZE(ad), NULL, 0);
if (adv_err) {
	printf("Failed to start BLE advertising: %d\n", adv_err);
} else {
	printf("BLE advertising started automatically (2s interval)\n");
}
*/

	// Configure ADC channels individually prior to sampling
	printf("Before ADC setup loop, channels=%d\n", (int)ARRAY_SIZE(adc_channels));
	for (size_t i = 0U; i < ARRAY_SIZE(adc_channels); i++) {
		printf("ADC setup loop: i=%zu, dev=%s\n", i, adc_channels[i].dev->name);
		if (!adc_is_ready_dt(&adc_channels[i])) {
			printf("ADC controller device %s not ready\n", adc_channels[i].dev->name);
			return 0;
		}

		err = adc_channel_setup_dt(&adc_channels[i]);
		if (err < 0) {
			printf("Could not setup channel #%d (%d)\n", i, err);
			return 0;
		}
	}
	printf("After ADC setup loop\n");

	printf("I2C device ready at address 0x%02X\n", i2c_spec.addr);
#if HAS_SPI_FLASH
	printf("SPI device ready at %d MHz\n", spi_spec.config.frequency / 1000000);
#endif
	printf("ADC device ready with %d channels\n", ARRAY_SIZE(adc_channels));
	printf("All devices initialized successfully!\n");

#if defined(CONFIG_SHELL)
	printf("Shell commands available under 'test' subheading\n");
#endif

	// Main application loop
	printf("Entering main loop\n");
	while (1) {
		// Toggle all LEDs
		leds_latched_on = !leds_latched_on;
		gpio_pin_set_dt(&my_gpio, leds_latched_on);
		gpio_pin_set_dt(&led1, leds_latched_on);
		gpio_pin_set_dt(&led2, leds_latched_on);

		printf("LEDs toggled %s\n", leds_latched_on ? "ON" : "OFF");

		// Sleep for 1 second between toggles
		k_sleep(K_MSEC(1000));
	}

	return 0;
}
