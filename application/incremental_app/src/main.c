/*
 * Incremental App - Starting with DFU
 * Build features incrementally: DFU -> LEDs -> Sensors -> BT -> ADC
 *
 * Copyright (c) 2026 AirSpot
 * SPDX-License-Identifier: Apache-2.0
 */

#include <math.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/printk.h>

/* Driver includes */
#include <drivers/button_driver.h>
#include <drivers/i2c_bus.h>
#include <drivers/led_driver.h>
#include <drivers/sht40.h>
#include <drivers/lps22hh.h>
#include <drivers/stcc4.h>
#include <drivers/battery.h>
#include <drivers/qi_charger.h>

LOG_MODULE_REGISTER(main, LOG_LEVEL_DBG);

/* Flash control pins - must be configured before SPI NOR driver init */
#define FLASH_WP_PIN \
	13					  /* P1.13 - Write Protect (active low, HIGH = not protected) */
#define FLASH_HOLD_PIN 11 /* P1.11 - Hold (active low, HIGH = not held) */

static void log_spi_config(const struct spi_dt_spec *spec)
{
	if (!spec)
	{
		return;
	}

	/* Bus ready check is handled by caller (spi_is_ready_dt) */
	LOG_INF("SPI frequency: %u Hz", spec->config.frequency);
	LOG_INF("SPI mode: CPOL=%d, CPHA=%d",
			(spec->config.operation & SPI_MODE_CPOL) ? 1 : 0,
			(spec->config.operation & SPI_MODE_CPHA) ? 1 : 0);

	if (spec->config.cs.gpio.port)
	{
		LOG_INF("SPI CS: %s pin %u (dt_flags=0x%X, delay=%u us)",
				spec->config.cs.gpio.port->name, spec->config.cs.gpio.pin,
				spec->config.cs.gpio.dt_flags, spec->config.cs.delay);
	}
	else
	{
		LOG_WRN("SPI CS: none (missing cs-gpios?)");
	}
}

static int flash_release_from_power_down(const struct spi_dt_spec *spec)
{
	uint8_t release_cmd = 0xAB;
	struct spi_buf release_tx_buf = {.buf = &release_cmd, .len = 1};
	struct spi_buf_set release_tx_set = {.buffers = &release_tx_buf, .count = 1};
	return spi_transceive_dt(spec, &release_tx_set, NULL);
}

static int flash_read_jedec_id(const struct spi_dt_spec *spec, uint8_t id[4])
{
	uint8_t cmd = 0x9F;
	struct spi_buf tx_buf = {.buf = &cmd, .len = 1};
	struct spi_buf_set tx_set = {.buffers = &tx_buf, .count = 1};
	struct spi_buf rx_buf = {.buf = id, .len = 4};
	struct spi_buf_set rx_set = {.buffers = &rx_buf, .count = 1};

	return spi_transceive_dt(spec, &tx_set, &rx_set);
}

/* Early initialization - runs before device drivers (PRE_KERNEL_1) */
static int flash_pins_early_init(void)
{
	const struct device *gpio1 = DEVICE_DT_GET(DT_NODELABEL(gpio1));
	if (!device_is_ready(gpio1))
		return 0;

	/* Configure WP and HOLD pins HIGH */
	gpio_pin_configure(gpio1, FLASH_WP_PIN, GPIO_OUTPUT_HIGH);
	gpio_pin_configure(gpio1, FLASH_HOLD_PIN, GPIO_OUTPUT_HIGH);

	return 0;
}

SYS_INIT(flash_pins_early_init, PRE_KERNEL_1, 0);

/* Flash release - runs early PRE_KERNEL_2 */
static int flash_power_up_init(void)
{
	/* No-op: Let MCUboot handle pins, or Main handle wake-up */
	return 0;
}

/* Register power-up init - runs at PRE_KERNEL_2 with high priority */
SYS_INIT(flash_power_up_init, PRE_KERNEL_2, 0);

/* Button event handler */
static void button_event_handler(button_event_t event)
{
	switch (event)
	{
	case BUTTON_EVENT_PRESSED:
		printk("\n>>> BUTTON PRESSED <<<\n");
		LOG_INF("Button pressed");
		/* Toggle LED 1 on button press */
		led_driver_toggle(1);
		break;

	case BUTTON_EVENT_RELEASED:
		printk("\n>>> BUTTON RELEASED <<<\n");
		LOG_INF("Button released");
		break;

	case BUTTON_EVENT_LONG_PRESS:
		printk("\n>>> BUTTON LONG PRESS (3+ seconds) <<<\n");
		LOG_INF("Button long press (3+ seconds)");
		/* Toggle LED 2 on long press */
		led_driver_toggle(2);
		break;
	}
}

int main(void)
{
	int ret;

	/* Early boot messages */
	printk("\n\n");
	printk("========================================\n");
	printk("AirSpot Incremental App\n");
	printk("Phase 1: DFU Enabled\n");
	printk("Phase 2: LEDs Enabled\n");
	printk("Phase 3: I2C Enabled\n");
	printk("Phase 4: External Flash (SPI) Enabled\n");
	printk("Phase 5: Button Enabled\n");
	printk("Phase 6: Sensors Enabled\n");
	printk("Phase 7: Battery & Qi Charging Enabled\n");
	printk("========================================\n");

	LOG_INF("========================================");
	LOG_INF("AirSpot Incremental App");
	LOG_INF("Phase 1: DFU Enabled");
	LOG_INF("Phase 2: LEDs Enabled");
	LOG_INF("Phase 3: I2C Enabled");
	LOG_INF("Phase 4: External Flash (SPI) Enabled");
	LOG_INF("Phase 5: Button Enabled");
	LOG_INF("Phase 6: Sensors Enabled");
	LOG_INF("Phase 7: Battery & Qi Charging Enabled");
	LOG_INF("========================================");

	/* Verify flash control pins are HIGH */
	const struct device *gpio1 = DEVICE_DT_GET(DT_NODELABEL(gpio1));
	if (device_is_ready(gpio1))
	{
		/* Re-configure to ensure they're HIGH (in case GPIO hog didn't work) */
		gpio_pin_configure(gpio1, FLASH_WP_PIN, GPIO_OUTPUT_HIGH);
		gpio_pin_configure(gpio1, FLASH_HOLD_PIN, GPIO_OUTPUT_HIGH);

		/* Verify pin states */
		int wp_state = gpio_pin_get(gpio1, FLASH_WP_PIN);
		int hold_state = gpio_pin_get(gpio1, FLASH_HOLD_PIN);
		LOG_INF("Flash control pins: WP=P1.13=%d (should be 1), HOLD=P1.11=%d "
				"(should be 1)",
				wp_state, hold_state);

		if (wp_state != 1 || hold_state != 1)
		{
			LOG_ERR("Flash control pins not HIGH! This will prevent flash "
					"communication.");
			/* Force them HIGH again */
			gpio_pin_set(gpio1, FLASH_WP_PIN, 1);
			gpio_pin_set(gpio1, FLASH_HOLD_PIN, 1);
			k_msleep(10); /* Wait for pins to stabilize */
		}

		/* External flash test (match flash_test behavior) */
#if DT_NODE_EXISTS(DT_NODELABEL(at25xe161d))
		const struct spi_dt_spec spi_spec =
			SPI_DT_SPEC_GET(DT_NODELABEL(at25xe161d), SPI_WORD_SET(8), 0);

		if (!spi_is_ready_dt(&spi_spec))
		{
			LOG_ERR("SPI device is not ready");
			LOG_ERR("SPI bus: %p", spi_spec.bus);
		}
		else
		{
			LOG_INF("SPI device is ready");
			log_spi_config(&spi_spec);

			/* Diagnostic: Check SPI bus device state */
			if (spi_spec.bus)
			{
				LOG_INF("SPI bus device: %s", spi_spec.bus->name);
				LOG_INF("SPI bus device ready: %s", device_is_ready(spi_spec.bus) ? "YES" : "NO");
			}

			/* After MCUboot chainloads, wait longer for SPI peripheral to stabilize.
			 * Don't manually configure CS - let SPI driver handle it completely.
			 */
			k_msleep(200); /* Longer delay for MCUboot chainload stabilization */

			/* CRITICAL: Re-assert WP/HOLD pins RIGHT before SPI test.
			 * LED driver initialization (when CONFIG_LED=y) happens early (POST_KERNEL)
			 * and might interfere with GPIO1 state. Force pins HIGH again.
			 */
			gpio_pin_set(gpio1, FLASH_WP_PIN, 1);
			gpio_pin_set(gpio1, FLASH_HOLD_PIN, 1);
			k_msleep(10); /* Wait for pins to stabilize */

			LOG_INF("Sending power-down release command (0xAB)...");
			ret = flash_release_from_power_down(&spi_spec);
			if (ret)
			{
				LOG_ERR("Power-down release failed: %d", ret);
			}
			else
			{
				LOG_INF("Power-down release sent, waiting 1ms...");
				k_msleep(1);
			}

			LOG_INF("========================================");
			LOG_INF("Test: Reading JEDEC ID (0x9F)");
			LOG_INF("========================================");

			for (int attempt = 0; attempt < 5; attempt++)
			{
				uint8_t id[4] = {0xFF, 0xFF, 0xFF, 0xFF};
				ret = flash_read_jedec_id(&spi_spec, id);
				if (ret == 0)
				{
					LOG_INF("Attempt %d: Response: %02X %02X %02X %02X", attempt + 1,
							id[0], id[1], id[2], id[3]);
					LOG_INF("Attempt %d: JEDEC ID: %02X %02X %02X (expected: 1F 46 0C)",
							attempt + 1, id[1], id[2], id[3]);

					if (id[1] == 0x1F && id[2] == 0x46 && id[3] == 0x0C)
					{
						LOG_INF("✓ Flash detected correctly!");
						break;
					}

					if (id[1] == 0x00 && id[2] == 0x00 && id[3] == 0x00)
					{
						LOG_WRN("Attempt %d: All zeros - flash not responding", attempt + 1);
					}
					else if (id[1] == 0xFF && id[2] == 0xFF && id[3] == 0xFF)
					{
						LOG_WRN("Attempt %d: All 0xFF - flash not responding", attempt + 1);
					}
					else
					{
						LOG_WRN("Attempt %d: Unexpected JEDEC ID", attempt + 1);
						break;
					}
				}
				else
				{
					LOG_ERR("Attempt %d: JEDEC read failed: %d", attempt + 1, ret);
				}

				k_msleep(100);
			}
		}
#else
		LOG_ERR("Flash device not found in device tree!");
#endif
	}
	else
	{
		LOG_ERR("GPIO1 not ready - cannot verify flash control pins");
	}

	/* Initialize LED driver */
	ret = led_driver_init();
	if (ret)
	{
		LOG_ERR("LED driver init failed: %d", ret);
	}
	else
	{
		LOG_INF("LED driver initialized");
		led_driver_startup_sequence();
	}

	/* Initialize button driver */
	ret = button_driver_init();
	if (ret)
	{
		LOG_ERR("Button driver init failed: %d", ret);
	}
	else
	{
		LOG_INF("Button driver initialized");
		button_driver_register_callback(button_event_handler);
	}

	/* Initialize I2C bus */
	ret = i2c_bus_init();
	if (ret)
	{
		LOG_ERR("I2C bus init failed: %d", ret);
	}
	else
	{
		LOG_INF("I2C bus initialized");
		/* Scan for devices on I2C bus */
		i2c_bus_scan();
	}

	/* Initialize sensors (order matters: SHT40 first for STCC4 compensation) */
	/* SHT40 - Temperature & Humidity */
	ret = sht40_init();
	if (ret)
	{
		LOG_ERR("SHT40 init failed: %d", ret);
	}
	else
	{
		LOG_INF("SHT40 initialized");
		/* Take initial measurement */
		sht40_data_t sht_data;
		ret = sht40_measure(&sht_data, SHT40_PRECISION_HIGH);
		if (ret == 0)
		{
			LOG_INF("SHT40: %.2f°C, %.1f%%RH", sht_data.temperature, sht_data.humidity);
		}
	}

	/* LPS22HH - Pressure */
	ret = lps22hh_init();
	if (ret)
	{
		LOG_ERR("LPS22HH init failed: %d", ret);
	}
	else
	{
		LOG_INF("LPS22HH initialized");
		/* Configure for 1Hz continuous mode */
		lps22hh_set_odr(LPS22HH_ODR_1HZ);
		/* Take initial reading */
		lps22hh_data_t lps_data;
		ret = lps22hh_read(&lps_data);
		if (ret == 0)
		{
			LOG_INF("LPS22HH: %.2f hPa, %.2f°C", lps_data.pressure, lps_data.temperature);
		}
	}

	/* STCC4 - CO2 (requires SHT40 for compensation) */
	ret = stcc4_init();
	if (ret)
	{
		LOG_ERR("STCC4 init failed: %d", ret);
	}
	else
	{
		LOG_INF("STCC4 initialized");
		/* Set ambient conditions from SHT40 */
		float temp = sht40_get_temperature();
		float hum = sht40_get_humidity();
		if (!isnan(temp) && !isnan(hum))
		{
			stcc4_set_ambient(temp, hum);
		}
		/* Trigger initial single-shot measurement */
		ret = stcc4_trigger_single_shot();
		if (ret == 0)
		{
			k_msleep(5000); /* Wait for measurement - single shot takes ~5 seconds */
			stcc4_data_t co2_data;
			ret = stcc4_read(&co2_data);
			if (ret == 0 && co2_data.valid)
			{
				LOG_INF("STCC4: %d ppm CO2", co2_data.co2_ppm);
			}
		}
	}

	/* Initialize Battery ADC driver */
	ret = battery_init();
	if (ret)
	{
		LOG_ERR("Battery driver init failed: %d", ret);
	}
	else
	{
		LOG_INF("Battery driver initialized");
		uint16_t batt_mv = battery_read_voltage_mv();
		uint8_t batt_pct = battery_get_percentage();
		LOG_INF("Battery: %u mV (%u%%)", batt_mv, batt_pct);
		LOG_INF("Battery raw ADC: %d, ADC mV: %d",
				battery_get_raw_adc(), battery_get_adc_mv());
	}

	/* Initialize Qi charger detection */
	ret = qi_charger_init();
	if (ret)
	{
		LOG_ERR("Qi charger init failed: %d", ret);
	}
	else
	{
		LOG_INF("Qi charger initialized");
		LOG_INF("Charging status: %s",
				qi_charger_is_charging() ? "CHARGING" : "Not charging");
	}

	LOG_INF("System initialized successfully");
	LOG_INF("DFU over BLE is available");
	LOG_INF("Button: Press to toggle LED 1, Long press (3s) to toggle LED 2");
	LOG_INF("Sensors & Battery: Reading every 6 seconds");

	/* Main loop - blink LED 0 every 2 seconds, read sensors every 5 seconds */
	while (1)
	{
		static uint32_t counter = 0;
		static uint32_t sensor_counter = 0;
		LOG_INF("Alive - count=%u", counter++);

		/* Toggle LED 0 every iteration */
		led_driver_toggle(0);

		/* Read sensors every 5 seconds (every 2.5 iterations) */
		if (counter % 3 == 0)
		{
			sensor_counter++;
			LOG_INF("=== Sensor Reading #%u ===", sensor_counter);

			/* Read SHT40 (temperature/humidity) */
			sht40_data_t sht_data;
			ret = sht40_measure(&sht_data, SHT40_PRECISION_HIGH);
			if (ret == 0)
			{
				LOG_INF("SHT40: %.2f°C, %.1f%%RH", sht_data.temperature, sht_data.humidity);
			}
			else
			{
				LOG_WRN("SHT40 read failed: %d", ret);
			}

			/* Read LPS22HH (pressure) */
			lps22hh_data_t lps_data;
			ret = lps22hh_read(&lps_data);
			if (ret == 0)
			{
				LOG_INF("LPS22HH: %.2f hPa, %.2f°C", lps_data.pressure, lps_data.temperature);
			}
			else
			{
				LOG_WRN("LPS22HH read failed: %d", ret);
			}

			/* Read STCC4 (CO2) - update ambient conditions first */
			float temp = sht40_get_temperature();
			float hum = sht40_get_humidity();
			if (!isnan(temp) && !isnan(hum))
			{
				stcc4_set_ambient(temp, hum);
			}
			ret = stcc4_trigger_single_shot();
			if (ret == 0)
			{
				k_msleep(5000); /* Wait for measurement - single shot takes ~5 seconds */
				stcc4_data_t co2_data;
				ret = stcc4_read(&co2_data);
				if (ret == 0 && co2_data.valid)
				{
					LOG_INF("STCC4: %d ppm CO2", co2_data.co2_ppm);
				}
				else
				{
					LOG_WRN("STCC4 read failed: %d", ret);
				}
			}
			else
			{
				LOG_WRN("STCC4 trigger failed: %d", ret);
			}

			/* Read Battery voltage and charging status */
			uint16_t batt_mv = battery_read_voltage_mv();
			uint8_t batt_pct = battery_get_percentage();
			bool is_charging = qi_charger_is_charging();
			LOG_INF("Battery: %u mV (%u%%) %s",
					batt_mv, batt_pct,
					is_charging ? "[CHARGING]" : "");

			if (battery_is_critical())
			{
				LOG_WRN("BATTERY CRITICAL!");
			}
			else if (battery_is_low())
			{
				LOG_WRN("Battery low");
			}
		}

		k_sleep(K_SECONDS(2));
	}

	return 0;
}
