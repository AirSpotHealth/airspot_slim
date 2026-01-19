# Sensor Drivers Documentation

Complete documentation for the AirSpot Slim sensor drivers.

## Overview

The AirSpot Slim uses three I2C sensors for environmental monitoring:

| Sensor  | Manufacturer       | Function               | I2C Address | Datasheet                                                    |
| ------- | ------------------ | ---------------------- | ----------- | ------------------------------------------------------------ |
| SHT40   | Sensirion          | Temperature & Humidity | 0x44        | [Link](https://sensirion.com/products/catalog/SHT40)         |
| LPS22HH | STMicroelectronics | Pressure               | 0x5D        | [Link](https://www.st.com/resource/en/datasheet/lps22hh.pdf) |
| STCC4   | Sensirion          | CO2                    | 0x64        | [Link](https://sensirion.com/products/catalog/STC41)         |

## I2C Bus Configuration

All sensors share a common I2C bus:

- **Instance**: i2c30
- **Frequency**: 400 kHz (I2C_BITRATE_FAST)
- **SDA Pin**: P0.04
- **SCL Pin**: P0.03
- **Pull-ups**: Internal (enabled in device tree)

### I2C Scan Results

```
Found device at 0x44  (SHT40)
Found device at 0x5D  (LPS22HH)
Found device at 0x64  (STCC4)
I2C scan complete: 3 devices found
```

---

## SHT40 - Temperature & Humidity Sensor

### Specifications

| Parameter            | Value                  |
| -------------------- | ---------------------- |
| Temperature Range    | -40°C to +125°C        |
| Temperature Accuracy | ±0.2°C (typical)       |
| Humidity Range       | 0% to 100% RH          |
| Humidity Accuracy    | ±1.8% RH (typical)     |
| I2C Address          | 0x44                   |
| Measurement Time     | ~10ms (high precision) |

### Commands

| Command                  | Code | Description                  |
| ------------------------ | ---- | ---------------------------- |
| Measure High Precision   | 0xFD | High precision measurement   |
| Measure Medium Precision | 0xF6 | Medium precision measurement |
| Measure Low Precision    | 0xE0 | Low precision measurement    |
| Read Serial              | 0x89 | Read unique serial number    |
| Soft Reset               | 0x94 | Reset sensor                 |

### API Reference

```c
/* Initialize sensor */
int sht40_init(void);

/* Trigger measurement and read data */
int sht40_measure(void);

/* Get last measured values */
float sht40_get_temperature(void);  // Returns °C
float sht40_get_humidity(void);     // Returns %RH

/* Read serial number */
int sht40_read_serial(uint32_t *serial);
```

### Usage Example

```c
#include <drivers/sht40.h>

int ret = sht40_init();
if (ret == 0) {
    ret = sht40_measure();
    if (ret == 0) {
        float temp = sht40_get_temperature();
        float hum = sht40_get_humidity();
        printk("Temp: %.2f°C, Humidity: %.1f%%RH\n", temp, hum);
    }
}
```

### Data Conversion

```c
/* Raw to temperature (16-bit value) */
temperature = -45.0f + 175.0f * (raw_temp / 65535.0f);

/* Raw to humidity (16-bit value) */
humidity = -6.0f + 125.0f * (raw_hum / 65535.0f);
/* Clamp to 0-100% */
if (humidity < 0) humidity = 0;
if (humidity > 100) humidity = 100;
```

### Typical Values

```
SHT40: 24.73°C, 64.1%RH
SHT40 Serial: 0x1521FA46
```

---

## LPS22HH - Pressure Sensor

### Specifications

| Parameter            | Value           |
| -------------------- | --------------- |
| Pressure Range       | 260 to 1260 hPa |
| Pressure Accuracy    | ±0.5 hPa        |
| Temperature Range    | -40°C to +85°C  |
| Temperature Accuracy | ±1.5°C          |
| I2C Address          | 0x5D            |
| WHO_AM_I             | 0xB3            |

### Registers

| Register     | Address | Description               |
| ------------ | ------- | ------------------------- |
| WHO_AM_I     | 0x0F    | Device ID (returns 0xB3)  |
| CTRL_REG1    | 0x10    | ODR, BDU configuration    |
| CTRL_REG2    | 0x11    | BOOT, ONE_SHOT, LOW_NOISE |
| STATUS       | 0x27    | Data ready flags          |
| PRESS_OUT_XL | 0x28    | Pressure LSB              |
| PRESS_OUT_L  | 0x29    | Pressure middle byte      |
| PRESS_OUT_H  | 0x2A    | Pressure MSB              |
| TEMP_OUT_L   | 0x2B    | Temperature LSB           |
| TEMP_OUT_H   | 0x2C    | Temperature MSB           |

### CTRL_REG1 Bits (0x10)

| Bit | Name     | Description         |
| --- | -------- | ------------------- |
| 6:4 | ODR[2:0] | Output Data Rate    |
| 1   | BDU      | Block Data Update   |
| 0   | SIM      | SPI mode (0=4-wire) |

**ODR Settings**:
- 000 = One-shot / Power-down
- 001 = 1 Hz
- 010 = 10 Hz
- 011 = 25 Hz
- 100 = 50 Hz
- 101 = 75 Hz
- 110 = 100 Hz
- 111 = 200 Hz

### CTRL_REG2 Bits (0x11)

| Bit | Name         | Description                |
| --- | ------------ | -------------------------- |
| 7   | BOOT         | Reload calibration data    |
| 4   | IF_ADD_INC   | Auto-increment address     |
| 2   | SWRESET      | Software reset             |
| 1   | LOW_NOISE_EN | Low noise mode             |
| 0   | ONE_SHOT     | Trigger single measurement |

### Critical: BOOT Command

**The LPS22HH requires a BOOT command on initialization to reload factory calibration data.** Without this, the sensor may return all zeros.

```c
/* Trigger BOOT to reload calibration */
lps22hh_write_reg(0x11, 0x80);  // Set BOOT bit

/* Wait for BOOT to complete (bit self-clears) */
do {
    lps22hh_read_reg(0x11, &val);
    k_msleep(10);
} while (val & 0x80);
```

### API Reference

```c
/* Initialize sensor (includes BOOT) */
int lps22hh_init(void);

/* Read WHO_AM_I register */
int lps22hh_read_id(uint8_t *id);

/* Trigger one-shot measurement */
int lps22hh_trigger_one_shot(void);

/* Read pressure and temperature */
int lps22hh_read(lps22hh_data_t *data);

/* Get last measured values */
float lps22hh_get_pressure(void);     // Returns hPa
float lps22hh_get_temperature(void);  // Returns °C
```

### Usage Example

```c
#include <drivers/lps22hh.h>

int ret = lps22hh_init();
if (ret == 0) {
    lps22hh_data_t data;
    ret = lps22hh_read(&data);
    if (ret == 0) {
        printk("Pressure: %.2f hPa, Temp: %.2f°C\n", 
               data.pressure, data.temperature);
    }
}
```

### Data Conversion

```c
/* Pressure: 24-bit signed, LSB = 1/4096 hPa */
int32_t press_raw = (raw[2] << 16) | (raw[1] << 8) | raw[0];
if (press_raw & 0x800000) press_raw |= 0xFF000000;  // Sign extend
float pressure = press_raw / 4096.0f;

/* Temperature: 16-bit signed, LSB = 0.01°C */
int16_t temp_raw = (raw[4] << 8) | raw[3];
float temperature = temp_raw / 100.0f;
```

### Typical Values

```
LPS22HH ID: 0xB3 (expected: 0xB3)
LPS22HH: 1007.47 hPa, 25.03°C
```

### Troubleshooting

| Issue                 | Cause                 | Solution                    |
| --------------------- | --------------------- | --------------------------- |
| Returns 0.00 hPa      | Missing BOOT          | Ensure BOOT command in init |
| STATUS always 0x00    | ONE_SHOT not clearing | Check BOOT completed        |
| CTRL_REG1 writes fail | Sensor in boot mode   | Wait for BOOT_ON to clear   |

---

## STCC4 - CO2 Sensor

### Specifications

| Parameter        | Value                    |
| ---------------- | ------------------------ |
| CO2 Range        | 0 to 40,000 ppm          |
| CO2 Accuracy     | ±(50 ppm + 5%)           |
| I2C Address      | 0x64                     |
| Measurement Time | ~5 seconds (single-shot) |

### Commands

| Command             | Code (MSB, LSB) | Description                    |
| ------------------- | --------------- | ------------------------------ |
| Measure Single Shot | 0x21, 0x9D      | Start single measurement       |
| Start Continuous    | 0x21, 0x8B      | Start continuous mode          |
| Stop Continuous     | 0x3F, 0x86      | Stop continuous mode           |
| Get Product ID      | 0x36, 0x5B      | Read product identifier        |
| Set Ambient         | 0x36, 0x15      | Set temp/humidity compensation |

### API Reference

```c
/* Initialize sensor */
int stcc4_init(void);

/* Single-shot measurement */
int stcc4_trigger_single_shot(void);

/* Continuous mode */
int stcc4_start_continuous(void);
int stcc4_stop_continuous(void);

/* Read CO2 data */
int stcc4_read(stcc4_data_t *data);

/* Set ambient conditions for compensation */
int stcc4_set_ambient(float temperature, float humidity);

/* Get last CO2 value */
uint16_t stcc4_get_co2(void);
```

### Data Structure

```c
typedef struct {
    uint16_t co2_ppm;  // CO2 concentration in ppm
    bool valid;        // Data validity flag
} stcc4_data_t;
```

### Usage Example - Single Shot

```c
#include <drivers/stcc4.h>

int ret = stcc4_init();
if (ret == 0) {
    /* Set ambient conditions from SHT40 */
    stcc4_set_ambient(sht40_get_temperature(), sht40_get_humidity());
    
    /* Trigger measurement */
    ret = stcc4_trigger_single_shot();
    if (ret == 0) {
        k_msleep(5000);  // Wait 5 seconds for measurement
        
        stcc4_data_t data;
        ret = stcc4_read(&data);
        if (ret == 0 && data.valid) {
            printk("CO2: %u ppm\n", data.co2_ppm);
        }
    }
}
```

### Usage Example - Continuous Mode

```c
/* Start continuous measurement */
stcc4_start_continuous();
k_msleep(1100);  // Wait for first measurement

/* Read periodically */
while (1) {
    stcc4_data_t data;
    ret = stcc4_read(&data);
    if (ret == 0 && data.valid) {
        printk("CO2: %u ppm\n", data.co2_ppm);
    }
    
    /* Update ambient conditions periodically */
    stcc4_set_ambient(temp, humidity);
    
    k_msleep(5000);
}
```

### Response Format

| Byte | Content |
| ---- | ------- |
| 0    | CO2 MSB |
| 1    | CO2 LSB |
| 2    | CRC-8   |

### CRC-8 Calculation

```c
#define CRC8_POLYNOMIAL 0x31
#define CRC8_INIT 0xFF

uint8_t crc8(const uint8_t *data, size_t len) {
    uint8_t crc = CRC8_INIT;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            if (crc & 0x80) {
                crc = (crc << 1) ^ CRC8_POLYNOMIAL;
            } else {
                crc <<= 1;
            }
        }
    }
    return crc;
}
```

### Typical Values

```
STCC4 raw: 01 86 [CRC:A9] -> CO2=390 ppm
STCC4: 390 ppm CO2
```

### Ambient Compensation

For best accuracy, provide the STCC4 with temperature and humidity data from the SHT40:

```c
float temp = sht40_get_temperature();
float hum = sht40_get_humidity();
if (!isnan(temp) && !isnan(hum)) {
    stcc4_set_ambient(temp, hum);
}
```

---

## Integration Example

Complete sensor reading loop:

```c
#include <drivers/i2c_bus.h>
#include <drivers/sht40.h>
#include <drivers/lps22hh.h>
#include <drivers/stcc4.h>

void sensor_init(void)
{
    /* Initialize I2C bus */
    i2c_bus_init();
    i2c_bus_scan();  // Optional: verify devices present
    
    /* Initialize sensors */
    sht40_init();
    lps22hh_init();
    stcc4_init();
}

void sensor_read_all(void)
{
    /* Read SHT40 (temperature/humidity) */
    if (sht40_measure() == 0) {
        float temp = sht40_get_temperature();
        float hum = sht40_get_humidity();
        LOG_INF("SHT40: %.2f°C, %.1f%%RH", temp, hum);
        
        /* Update STCC4 ambient conditions */
        stcc4_set_ambient(temp, hum);
    }
    
    /* Read LPS22HH (pressure) */
    lps22hh_data_t pressure;
    if (lps22hh_read(&pressure) == 0) {
        LOG_INF("LPS22HH: %.2f hPa, %.2f°C", 
                pressure.pressure, pressure.temperature);
    }
    
    /* Read STCC4 (CO2) - assumes continuous mode */
    stcc4_data_t co2;
    if (stcc4_read(&co2) == 0 && co2.valid) {
        LOG_INF("STCC4: %u ppm", co2.co2_ppm);
    }
}
```

---

## Kconfig Options

Required configuration in `prj.conf`:

```conf
# I2C support
CONFIG_I2C=y

# Logging
CONFIG_LOG=y

# Float printf support (for LOG_INF with floats)
CONFIG_CBPRINTF_FP_SUPPORT=y

# Clock control (required for I2C)
CONFIG_CLOCK_CONTROL_NRF=y
```

---

## Files

### Driver Source Files

| File                    | Description        |
| ----------------------- | ------------------ |
| `src/drivers/i2c_bus.c` | I2C bus management |
| `src/drivers/sht40.c`   | SHT40 driver       |
| `src/drivers/lps22hh.c` | LPS22HH driver     |
| `src/drivers/stcc4.c`   | STCC4 driver       |

### Header Files

| File                        | Description |
| --------------------------- | ----------- |
| `include/drivers/i2c_bus.h` | I2C bus API |
| `include/drivers/sht40.h`   | SHT40 API   |
| `include/drivers/lps22hh.h` | LPS22HH API |
| `include/drivers/stcc4.h`   | STCC4 API   |

---

## References

- [Sensirion SHT40 Datasheet](https://sensirion.com/media/documents/33FD6951/624C4357/Datasheet_SHT4x.pdf)
- [ST LPS22HH Datasheet](https://www.st.com/resource/en/datasheet/lps22hh.pdf)
- [Sensirion STC41 Datasheet](https://sensirion.com/media/documents/F1BC3A5C/6426DD70/Sensirion_Datasheet_CO2_Sensor_STC4x.pdf)

---

**Last Updated**: 2026-01-19
