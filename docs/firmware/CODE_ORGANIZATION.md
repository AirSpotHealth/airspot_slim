# Code Organization - Best Practices

This document describes the code organization and best practices used in AirSpot Slim firmware.

## Folder Structure

```
airspot_slim/
├── application/
│   ├── CMakeLists.txt              # Build configuration
│   ├── Kconfig                     # Kconfig options
│   ├── prj.conf                    # Zephyr configuration
│   ├── pm_static.yml               # Partition manager
│   ├── build.sh                    # Build script
│   ├── setup_env.sh                # Environment setup
│   │
│   ├── boards/                     # Board-specific files
│   │   └── arm/airspot_slim_nrf54l05_cpuapp/
│   │       ├── airspot_slim_nrf54l05_cpuapp.dts
│   │       ├── airspot_slim_nrf54l05_cpuapp_defconfig
│   │       └── board.yml
│   │
│   ├── include/                    # Public headers
│   │   ├── airspot/               # Device definitions
│   │   │   ├── version.h          # Version information
│   │   │   └── board.h            # Board definitions
│   │   ├── drivers/               # Driver interfaces
│   │   │   ├── battery.h          # Battery ADC
│   │   │   ├── button_driver.h    # Button input
│   │   │   ├── i2c_bus.h          # I2C bus
│   │   │   ├── led_driver.h       # LED control
│   │   │   ├── lps22hh.h          # Pressure sensor
│   │   │   ├── qi_charger.h       # Qi detection
│   │   │   ├── sht40.h            # Temp/humidity
│   │   │   ├── spi_flash.h        # Flash storage
│   │   │   └── stcc4.h            # CO2 sensor
│   │   └── services/              # Service interfaces
│   │       ├── airspot_nus.h      # NUS service
│   │       ├── ble_mode_service.h # Mode switching
│   │       ├── ble_service.h      # BLE core
│   │       ├── data_log.h         # History storage
│   │       ├── factory_test.h     # Factory test
│   │       ├── led_indicator.h    # LED patterns
│   │       ├── mesh_sensor.h      # BLE Mesh
│   │       ├── power_manager.h    # Power states
│   │       ├── protocol.h         # App protocol
│   │       ├── sensor_manager.h   # Sensor coordination
│   │       └── user_settings.h    # Settings
│   │
│   └── src/                        # Source files
│       ├── main.c                  # Application entry
│       ├── drivers/               # Driver implementations
│       │   ├── battery.c
│       │   ├── button_driver.c
│       │   ├── i2c_bus.c
│       │   ├── led_driver.c
│       │   ├── lps22hh.c
│       │   ├── qi_charger.c
│       │   ├── sht40.c
│       │   ├── spi_flash.c
│       │   └── stcc4.c
│       └── services/              # Service implementations
│           ├── airspot_nus.c
│           ├── ble_mode_service.c
│           ├── ble_service.c
│           ├── data_log.c
│           ├── factory_test.c
│           ├── led_indicator.c
│           ├── mesh_sensor.c
│           ├── power_manager.c
│           ├── protocol.c
│           ├── sensor_manager.c
│           └── user_settings.c
│
├── docs/                           # Documentation
│   ├── board/                     # Hardware documentation
│   │   ├── PIN_ASSIGNMENTS.md
│   │   ├── HARDWARE_REFERENCE.md
│   │   ├── SCHEMATIC_ANALYSIS.md
│   │   └── SCHEMATIC_FINDINGS.md
│   ├── firmware/                  # Firmware documentation
│   │   ├── BLE_PROTOCOL.md       # Protocol reference
│   │   ├── BUILD_INSTRUCTIONS.md
│   │   ├── CODE_ORGANIZATION.md  # This file
│   │   ├── IMPLEMENTATION_PLAN.md
│   │   ├── QUICK_START.md
│   │   ├── SENSORS.md
│   │   └── SERVICES.md           # Service documentation
│   └── datasheets/               # Component datasheets
│
└── external/                      # Zephyr, nRF SDK modules
```

---

## Design Principles

### 1. Modular Architecture

The firmware follows a layered architecture:

```
┌─────────────────────────────────────────┐
│            Application (main.c)          │
├─────────────────────────────────────────┤
│              Services Layer              │
│   (protocol, sensor_manager, data_log)   │
├─────────────────────────────────────────┤
│              Drivers Layer               │
│   (stcc4, sht40, battery, spi_flash)    │
├─────────────────────────────────────────┤
│         Zephyr Device Drivers            │
└─────────────────────────────────────────┘
```

- **Drivers**: Hardware abstraction layer
- **Services**: Application logic and state management
- **Clear interfaces**: Headers in `include/`, implementations in `src/`

### 2. File Header Template

Every source file should have a standard header:

```c
/*
 * Copyright (c) 2026 AirSpot
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * [Module Name]
 *
 * [Brief description of what this module does]
 */
```

### 3. Function Documentation Style

Use Doxygen-compatible comments for public APIs:

```c
/**
 * @brief Initialize the sensor manager
 *
 * Sets up the sensor coordination thread and registers callbacks.
 *
 * @return 0 on success, negative errno on failure
 */
int sensor_manager_init(void);
```

---

## Memory Optimization

### Use Appropriate Data Types

```c
// Good: Use smallest appropriate type
uint8_t led_id;        // 0-255 is enough
uint16_t voltage_mv;   // Millivolts, 0-65535
uint16_t co2_ppm;      // CO2 range 0-40000

// Bad: Using int for everything
int led_id;            // Wastes memory
```

### Const for Read-Only Data

```c
// Good: Stored in flash, not RAM
static const struct bt_data ad[] = {
    BT_DATA_BYTES(...),
};

// Bad: Wastes RAM
static struct bt_data ad[] = {
    BT_DATA_BYTES(...),
};
```

### Static Allocation

```c
// Good: Static allocation, known at compile time
static struct k_work_delayable long_press_work;

// Bad: Dynamic allocation (unless necessary)
struct k_work_delayable *work = k_malloc(sizeof(*work));
```

---

## CPU/Power Optimization

### Use Timers and Work Queues

```c
// Good: Timer-driven, CPU can sleep
k_timer_start(&measurement_timer, K_SECONDS(5), K_SECONDS(5));

// Bad: Polling in main loop
while (1) {
    do_measurement();
    k_sleep(K_SECONDS(5));
}
```

### Efficient Sleep

```c
// Good: Long sleep allows CPU to enter low-power mode
k_sleep(K_SECONDS(1));

// Bad: Short sleeps waste power
k_sleep(K_MSEC(10));
```

### Use Work Queues for Deferred Work

```c
// Good: Defer non-critical work
k_work_schedule(&long_press_work, K_MSEC(3000));

// Bad: Blocking in interrupt context
k_sleep(K_MSEC(3000));  // In ISR - BAD!
```

---

## Error Handling

### Always Check Return Values

```c
// Good: Check and handle errors
int ret = sensor_manager_init();
if (ret) {
    LOG_ERR("Sensor manager init failed: %d", ret);
    return ret;
}

// Bad: Ignore errors
sensor_manager_init();  // What if it fails?
```

### Use Standard Error Codes

```c
// Good: Return standard error codes
if (led_id >= LED_COUNT) {
    return -EINVAL;  // Invalid argument
}
if (!initialized) {
    return -ENODEV;  // Device not ready
}

// Bad: Magic numbers
if (error) {
    return -1;  // What does -1 mean?
}
```

---

## Logging Best Practices

### Use Appropriate Log Levels

```c
LOG_ERR("Critical error");      // Always log
LOG_WRN("Warning condition");   // Important but not critical
LOG_INF("Normal operation");    // Informational
LOG_DBG("Debug information");   // Only in debug builds
```

### Use Module-Specific Loggers

```c
// Good: Module-specific logger
LOG_MODULE_REGISTER(sensor_manager, CONFIG_AIRSPOT_LOG_LEVEL);

// Bad: Using main logger for everything
LOG_MODULE_REGISTER(main, LOG_LEVEL_DBG);
```

---

## Zephyr-Specific Best Practices

### Device Tree Usage

```c
// Get device from device tree
#define I2C_NODE DT_NODELABEL(i2c30)
static const struct device *i2c_dev = DEVICE_DT_GET(I2C_NODE);

// Get GPIO spec from device tree
static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET(DT_ALIAS(sw0), gpios);
```

### Kconfig Integration

```
# Kconfig file
config AIRSPOT_SENSOR_INTERVAL_MS
    int "Sensor measurement interval in milliseconds"
    default 5000
    help
      Interval between sensor measurements.
```

### Thread Safety

```c
// Use kernel primitives for shared data
static K_MUTEX_DEFINE(data_mutex);

void update_data(int value) {
    k_mutex_lock(&data_mutex, K_FOREVER);
    shared_data = value;
    k_mutex_unlock(&data_mutex);
}
```

---

## Testing Guidelines

### Unit Testing

- Test drivers independently
- Mock hardware dependencies
- Use Zephyr's ztest framework

### Integration Testing

- Test module interactions
- Verify error handling
- Test power management

---

## Adding New Components

### Adding New Drivers

1. Create header in `include/drivers/`
2. Create implementation in `src/drivers/`
3. Add to CMakeLists.txt
4. Update SENSORS.md or create new doc

### Adding New Services

1. Create header in `include/services/`
2. Create implementation in `src/services/`
3. Add initialization to main.c
4. Update SERVICES.md

### Adding New Protocol Commands

1. Add command code to `protocol.h`
2. Add handler case in `protocol.c`
3. Update BLE_PROTOCOL.md

---

## Code Review Checklist

- [ ] Follows standard file header format
- [ ] Public APIs have Doxygen comments
- [ ] Error codes are checked and handled
- [ ] Uses appropriate data types
- [ ] No blocking calls in ISR context
- [ ] Thread-safe if accessing shared data
- [ ] Logging uses appropriate levels
- [ ] No hardcoded magic numbers

---

**Last Updated**: 2026-01-24
