# Code Organization - Best Practices

This document describes the code organization and best practices used in AirSpot Slim firmware.

## Folder Structure

```
slim/
├── CMakeLists.txt              # Build configuration
├── Kconfig                      # Kconfig options
├── prj.conf                     # Zephyr configuration
├── boards/                      # Board-specific files
│   └── *.overlay               # Device tree overlays
├── include/                     # Public headers
│   ├── airspot/                # AirSpot-specific headers
│   │   ├── version.h          # Version information
│   │   └── board.h            # Board definitions
│   ├── drivers/                # Driver interfaces
│   │   ├── led_driver.h
│   │   └── button_driver.h
│   └── services/               # Service interfaces
│       └── ble_service.h
└── src/                         # Source files
    ├── main.c                  # Application entry
    ├── drivers/                # Driver implementations
    │   ├── led_driver.c
    │   └── button_driver.c
    └── services/               # Service implementations
        └── ble_service.c
```

## Design Principles

### 1. Modular Architecture
- **Drivers**: Hardware abstraction layer (LED, Button, etc.)
- **Services**: Higher-level functionality (BLE, Power Management, etc.)
- **Clear interfaces**: Headers in `include/`, implementations in `src/`

### 2. Memory Optimization

#### Use Appropriate Data Types
```c
// Good: Use smallest appropriate type
uint8_t led_id;        // 0-255 is enough
uint16_t voltage_mv;   // Millivolts, 0-65535

// Bad: Using int for everything
int led_id;            // Wastes memory
```

#### Const for Read-Only Data
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

#### Static Allocation
```c
// Good: Static allocation, known at compile time
static struct k_work_delayable long_press_work;

// Bad: Dynamic allocation (unless necessary)
struct k_work_delayable *work = k_malloc(sizeof(*work));
```

### 3. CPU/Power Optimization

#### Minimize Polling
```c
// Good: Use interrupts
gpio_pin_interrupt_configure_dt(&button, GPIO_INT_EDGE_BOTH);

// Bad: Polling in main loop
while (1) {
    if (gpio_pin_get_dt(&button) == 0) {
        // Button pressed
    }
    k_sleep(K_MSEC(10));
}
```

#### Efficient Sleep
```c
// Good: Long sleep allows CPU to enter low-power mode
k_sleep(K_SECONDS(1));

// Bad: Short sleeps waste power
k_sleep(K_MSEC(10));
```

#### Use Work Queues for Deferred Work
```c
// Good: Defer non-critical work
k_work_schedule(&long_press_work, K_MSEC(3000));

// Bad: Blocking in interrupt context
k_sleep(K_MSEC(3000));  // In ISR - BAD!
```

### 4. Error Handling

#### Always Check Return Values
```c
// Good: Check and handle errors
int ret = led_driver_init();
if (ret) {
    LOG_ERR("LED init failed: %d", ret);
    return ret;
}

// Bad: Ignore errors
led_driver_init();  // What if it fails?
```

#### Use Appropriate Error Codes
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

### 5. Logging Best Practices

#### Use Appropriate Log Levels
```c
LOG_ERR("Critical error");      // Always log
LOG_WRN("Warning condition");   // Important but not critical
LOG_INF("Normal operation");    // Informational
LOG_DBG("Debug information");   // Only in debug builds
```

#### Use Module-Specific Loggers
```c
// Good: Module-specific logger
LOG_MODULE_REGISTER(led_driver, CONFIG_AIRSPOT_LOG_LEVEL);

// Bad: Using main logger for everything
LOG_MODULE_REGISTER(main, LOG_LEVEL_DBG);
```

### 6. Code Organization

#### Header Guards
```c
#ifndef LED_DRIVER_H
#define LED_DRIVER_H
// ... code ...
#endif /* LED_DRIVER_H */
```

#### Forward Declarations
```c
// In header: Forward declaration
typedef void (*button_callback_t)(button_event_t event);

// In source: Full definition
typedef enum {
    BUTTON_EVENT_PRESSED,
    // ...
} button_event_t;
```

#### Const Correctness
```c
// Good: Mark parameters as const if not modified
int led_driver_get_state(uint8_t led_id, bool *state);

// Good: Mark functions that don't modify state
bool ble_service_is_connected(void);
```

### 7. Documentation

#### Function Documentation
```c
/**
 * @brief Initialize LED driver
 *
 * @return 0 on success, negative error code on failure
 */
int led_driver_init(void);
```

#### File Headers
```c
/*
 * Copyright (c) 2025 AirSpot
 *
 * SPDX-License-Identifier: Apache-2.0
 */
```

## Zephyr-Specific Best Practices

### 1. Device Tree Usage
- Use device tree for hardware configuration
- Access devices via `DEVICE_DT_GET()`
- Use `GPIO_DT_SPEC_GET()` for GPIO pins

### 2. Kconfig Integration
- Use Kconfig for configurable options
- Provide sensible defaults
- Document options in Kconfig files

### 3. Power Management
- Use `CONFIG_PM=y` for power management
- Implement PM hooks for device power states
- Minimize active time

### 4. Thread Safety
- Use kernel primitives (mutexes, semaphores) for shared data
- Be careful with ISR context
- Use work queues for deferred work

## Memory Usage Guidelines

### Stack Usage
- Keep stack usage minimal
- Use `CONFIG_MAIN_STACK_SIZE` appropriately
- Monitor stack usage with `CONFIG_THREAD_STACK_INFO`

### Heap Usage
- Avoid dynamic allocation when possible
- Use static allocation for known sizes
- Use memory pools for frequent allocations

### Flash Usage
- Use `const` for read-only data
- Store strings in flash with `CONFIG_LOG_BACKEND_SHOW_COLOR=n`
- Minimize code size with compiler optimizations

## Performance Guidelines

### Interrupts
- Keep ISRs short
- Defer work to threads/work queues
- Use `k_work` for deferred work

### Threads
- Minimize thread count
- Use appropriate priorities
- Use sleep to allow other threads

### BLE
- Use appropriate advertising intervals
- Minimize connection parameters
- Use notifications efficiently

## Testing Guidelines

### Unit Testing
- Test drivers independently
- Mock hardware dependencies
- Use Zephyr's ztest framework

### Integration Testing
- Test module interactions
- Verify error handling
- Test power management

## Future Scalability

### Adding New Drivers
1. Create header in `include/drivers/`
2. Create implementation in `src/drivers/`
3. Add to CMakeLists.txt
4. Document in appropriate files

### Adding New Services
1. Create header in `include/services/`
2. Create implementation in `src/services/`
3. Add initialization to main.c
4. Document API

### Adding New Boards
1. Create overlay in `boards/`
2. Update board.h if needed
3. Test with new hardware

---

**Last Updated**: 2025-01-XX

