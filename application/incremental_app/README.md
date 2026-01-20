# AirSpot Incremental App

Feature-by-feature development approach for the AirSpot Slim firmware.

## Current Status

| Phase | Feature       | Status     | Notes                                  |
| ----- | ------------- | ---------- | -------------------------------------- |
| 1     | DFU (MCUboot) | ✅ Ready    | Temporarily disabled for testing       |
| 2     | LEDs          | ✅ Working  | RGB LED on GPIO2 (P2.06, P2.08, P2.09) |
| 3     | I2C Bus       | ✅ Working  | 400kHz on P0.03/P0.04                  |
| 4     | SPI Flash     | ✅ Working  | AT25XE161D, JEDEC ID verified          |
| 5     | Button        | ✅ Working  | Polling mode (interrupt not supported) |
| 6     | Sensors       | ✅ Working  | SHT40, LPS22HH, STCC4 all functional   |
| 7     | Battery ADC   | ✅ Verified | ~98% accuracy across 3.0V-4.2V range   |
| 7b    | Qi Charging   | 🔶 Untested | Driver implemented, needs HW test      |
| 8     | BLE NUS       | ✅ Complete | NUS + Protocol + Sensor Manager        |
| 9     | BT Mesh       | 🔜 Planned  | For nRF54L10                           |

## Development Phases

### ✅ Phase 1: DFU (MCUboot)
- MCUboot bootloader support
- MCUmgr over BLE for firmware updates
- **Status**: Ready but temporarily disabled

### ✅ Phase 2: LEDs
- RGB LED control via GPIO
- 3 channels: LED0 (P2.09), LED1 (P2.08), LED2 (P2.06)
- Active HIGH configuration

### ✅ Phase 3: I2C Bus
- Shared I2C bus for all sensors
- 400kHz (I2C_BITRATE_FAST)
- Pins: SDA=P0.04, SCL=P0.03

### ✅ Phase 4: SPI Flash
- AT25XE161D 2MB flash
- JEDEC ID: 1F 46 0C
- WP/HOLD pins configured via gpio-hog

### ✅ Phase 5: Button
- Single button on P2.10
- Active HIGH
- Polling mode (50ms interval)
- Supports short press and long press (3s)

### ✅ Phase 6: Sensors
All three sensors working:

| Sensor  | Type          | I2C Address | Status                    |
| ------- | ------------- | ----------- | ------------------------- |
| SHT40   | Temp/Humidity | 0x44        | ✅ Working                 |
| LPS22HH | Pressure      | 0x5D        | ✅ Working (requires BOOT) |
| STCC4   | CO2           | 0x64        | ✅ Working                 |

See [docs/firmware/SENSORS.md](../docs/firmware/SENSORS.md) for detailed sensor documentation.

### ✅ Phase 7: Battery & Qi Charging
Battery voltage monitoring and Qi wireless charging detection:

| Feature     | Pin   | Status     | Description                          |
| ----------- | ----- | ---------- | ------------------------------------ |
| Battery ADC | P1.05 | ✅ Verified | AIN1, via 0.6x voltage divider       |
| ADC Enable  | P1.04 | ✅ Verified | Controls divider MOSFET (power save) |
| Qi Charge   | P0.00 | 🔶 Untested | Active LOW charging status           |

**ADC Configuration:**
- 12-bit resolution with 8x oversampling
- Internal 0.6V reference, 1/4 gain
- Device tree based setup (using `adc_dt_spec`)

**Verified Test Results (2026-01-20):**

| Input Voltage | Measured | Accuracy | Percentage |
| ------------- | -------- | -------- | ---------- |
| 3000 mV       | 2916 mV  | 97.2%    | 8%         |
| 3700 mV       | 3621 mV  | 97.9%    | 58%        |
| 4200 mV       | 4133 mV  | 98.4%    | 95%        |

**Voltage Scaling (from schematic):**
- Vbatt 4.2V → Vadc 2.52V (scaling factor 0.6)
- Vbatt 3.7V → Vadc 2.22V
- Vbatt 3.3V → Vadc 1.98V

**Battery Thresholds:**
- Full: 4.2V (100%)
- Low: 3.3V (~15%)
- Critical: 3.0V (~5%)
- Empty: 2.8V (0%)

**Qi Charging (Not Yet Tested):**
- Driver implemented using GPIO interrupt on P0.00
- Active LOW signal (0 = charging, 1 = not charging)
- Requires hardware test with Qi charging pad

### ✅ Phase 8: BLE NUS + App Protocol
Full BLE application with Nordic UART Service and app protocol:

| Service            | Description                                                   |
| ------------------ | ------------------------------------------------------------- |
| `ble_service`      | BLE stack initialization, advertising, connection management  |
| `airspot_nus`      | NUS (Nordic UART Service) for bidirectional app communication |
| `protocol`         | Binary protocol handler (all app commands)                    |
| `sensor_manager`   | Coordinated sensor readings with callbacks                    |
| `data_log`         | CO2 history logging to SPI flash                              |
| `user_settings`    | Persistent settings via NVS                                   |
| `ble_mode_service` | Peripheral/Mesh mode switching                                |

**Note**: Phase 8 development moved to main `application/` - incremental_app served as driver testing ground.

### 🔜 Phase 9: BT Mesh
- For nRF54L10 board
- Leave for later development

## Building

```bash
# Set up environment
export ZEPHYR_TOOLCHAIN_VARIANT=zephyr
export ZEPHYR_SDK_INSTALL_DIR=/opt/nordic/ncs/toolchains/322ac893fe/opt/zephyr-sdk

# Build (without sysbuild for simpler builds)
cd application/incremental_app
west build -b nrf54l15dk/nrf54l05/cpuapp --build-dir build --no-sysbuild

# Flash
nrfjprog --program build/zephyr/zephyr.hex --chiperase --verify --reset -f NRF54L
```

## Project Structure

```
incremental_app/
├── boards/
│   └── nrf54l15dk_nrf54l05_cpuapp.overlay  # Hardware configuration
├── include/
│   ├── airspot/
│   │   └── board.h                          # Board-level definitions
│   └── drivers/
│       ├── battery.h                        # Battery ADC driver
│       ├── button_driver.h
│       ├── i2c_bus.h
│       ├── led_driver.h
│       ├── lps22hh.h
│       ├── qi_charger.h                     # Qi charging detection
│       ├── sht40.h
│       └── stcc4.h
├── src/
│   ├── drivers/
│   │   ├── battery.c                        # Battery ADC implementation
│   │   ├── button_driver.c
│   │   ├── i2c_bus.c
│   │   ├── led_driver.c
│   │   ├── lps22hh.c
│   │   ├── qi_charger.c                     # Qi charging implementation
│   │   ├── sht40.c
│   │   └── stcc4.c
│   └── main.c
├── CMakeLists.txt
├── prj.conf                                 # Kconfig options
└── README.md
```

## Configuration

### Key Kconfig Options (prj.conf)

```conf
# Essential
CONFIG_GPIO=y
CONFIG_SERIAL=y
CONFIG_LOG=y
CONFIG_I2C=y
CONFIG_SPI=y
CONFIG_CBPRINTF_FP_SUPPORT=y  # Float printf support

# ADC (Battery monitoring)
CONFIG_ADC=y
CONFIG_ADC_NRFX_SAADC=y

# BLE
CONFIG_BT=y
CONFIG_BT_PERIPHERAL=y
CONFIG_BT_DEVICE_NAME="AirSpot Incremental"
```

## Sensor Reading Example

```c
/* SHT40 - Temperature/Humidity */
sht40_data_t sht_data;
ret = sht40_measure(&sht_data, SHT40_PRECISION_HIGH);
if (ret == 0) {
    float temp = sht_data.temperature;
    float hum = sht_data.humidity;
}

/* LPS22HH - Pressure */
lps22hh_data_t pressure_data;
ret = lps22hh_read(&pressure_data);
if (ret == 0) {
    float pressure = pressure_data.pressure;  // hPa
    float temp = pressure_data.temperature;   // °C
}

/* STCC4 - CO2 */
stcc4_data_t co2_data;
ret = stcc4_trigger_single_shot();
k_msleep(5000);  // Wait for measurement
ret = stcc4_read(&co2_data);
if (ret == 0 && co2_data.valid) {
    uint16_t co2 = co2_data.co2_ppm;
}

/* Battery - Voltage/Percentage */
uint16_t voltage_mv = battery_read_voltage_mv();
uint8_t percentage = battery_get_percentage();
bool is_low = battery_is_low();
bool is_critical = battery_is_critical();

/* Qi Charger - Charging Status */
bool is_charging = qi_charger_is_charging();
```

## Troubleshooting

### LPS22HH Returns 0.00 Values
The LPS22HH sensor requires a BOOT command to reload factory calibration data on init. This is handled automatically in `lps22hh_init()`.

### Button Interrupt Not Working
The nRF54L05 GPIO interrupt system has limitations. The button driver automatically falls back to polling mode (50ms interval).

### Float Values Show as `*float*`
Enable `CONFIG_CBPRINTF_FP_SUPPORT=y` in prj.conf for float printf support.

### Battery Reading Shows 0 or Wrong Values
1. Verify ADC enable pin (P1.04) is toggling correctly
2. Check voltage divider circuit connections
3. Use `battery_get_raw_adc()` and `battery_get_adc_mv()` for debugging
4. Expected ADC voltage range: ~1.98V to ~2.52V for battery 3.3V to 4.2V

### Qi Charging Not Detected
1. Verify P0.00 is connected to the Qi charger status output
2. Signal is active LOW (0 = charging)
3. Check if GPIO interrupt is working or use polling via `qi_charger_is_charging()`

---

**Version**: 0.8.0 (Phase 8 - BLE NUS Complete)  
**Last Updated**: 2026-01-20
