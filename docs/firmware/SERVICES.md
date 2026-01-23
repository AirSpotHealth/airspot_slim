# Services Documentation

Complete documentation for the AirSpot Slim firmware services layer.

## Overview

Services provide high-level functionality built on top of hardware drivers. They implement the application logic, state management, and inter-component communication.

## Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                      Application (main.c)                    │
├─────────────────────────────────────────────────────────────┤
│                         Services Layer                       │
│  ┌──────────────┐ ┌──────────────┐ ┌──────────────────────┐ │
│  │ Sensor       │ │ BLE          │ │ Power Manager        │ │
│  │ Manager      │ │ Service      │ │                      │ │
│  ├──────────────┤ ├──────────────┤ ├──────────────────────┤ │
│  │ Data Log     │ │ Protocol     │ │ LED Indicator        │ │
│  ├──────────────┤ ├──────────────┤ ├──────────────────────┤ │
│  │ User Settings│ │ AirSpot NUS  │ │ Factory Test         │ │
│  └──────────────┘ └──────────────┘ └──────────────────────┘ │
├─────────────────────────────────────────────────────────────┤
│                         Drivers Layer                        │
│   i2c_bus | battery | spi_flash | button | led | qi_charger │
│   sht40   | lps22hh | stcc4                                  │
└─────────────────────────────────────────────────────────────┘
```

---

## Service Reference

### 1. Sensor Manager (`sensor_manager.c`)

**Purpose**: Coordinates sensor readings and provides unified data access.

**Features**:

- Periodic measurement scheduling (default 5 seconds)
- Ambient compensation for CO2 sensor
- Callback mechanism for new data
- Thread-safe data access

**API**:

```c
/* Initialize sensor manager */
int sensor_manager_init(void);

/* Start periodic measurements */
int sensor_manager_start(void);

/* Stop measurements */
void sensor_manager_stop(void);

/* Force immediate measurement */
int sensor_manager_trigger_measurement(void);

/* Get latest sensor data */
int sensor_manager_get_data(sensor_data_t *data);

/* Register callback for new data */
void sensor_manager_set_callback(sensor_data_callback_t callback);
```

**Data Structure**:

```c
typedef struct {
    uint16_t co2_ppm;         /* CO2 concentration */
    float temperature;         /* From SHT40 (°C) */
    float humidity;            /* From SHT40 (%RH) */
    float pressure;            /* From LPS22HH (hPa) */
    bool co2_valid;            /* CO2 data validity */
    int64_t timestamp;         /* Measurement time */
} sensor_data_t;
```

---

### 2. BLE Service (`ble_service.c`)

**Purpose**: Core BLE stack management and connection handling.

**Features**:

- BLE advertising control
- Connection management
- Connection state callbacks
- MTU negotiation

**API**:

```c
/* Initialize BLE stack */
int ble_service_init(void);

/* Start advertising */
int ble_service_start_advertising(void);

/* Stop advertising */
void ble_service_stop_advertising(void);

/* Check connection status */
bool ble_service_is_connected(void);

/* Register connection callback */
void ble_service_set_connection_callback(ble_connection_cb_t callback);
```

**Configuration**:

- Device Name: "AirSpot Slim"
- Advertising Interval: 100ms (fast) / 1000ms (slow)
- Connection Interval: 7.5ms - 30ms
- MTU: Negotiated up to 247 bytes

---

### 3. AirSpot NUS (`airspot_nus.c`)

**Purpose**: Nordic UART Service implementation for app communication.

**Features**:

- RX data reception with callback
- TX data transmission
- Chunked data support
- Flow control

**API**:

```c
/* Initialize NUS service */
int airspot_nus_init(void);

/* Send data over NUS */
int airspot_nus_send(const uint8_t *data, uint16_t len);

/* Register RX callback */
void airspot_nus_set_rx_callback(nus_rx_callback_t callback);
```

**Protocol Frame Format**:

```
[0xFF][0xAA][CMD][LEN][DATA...][CHECKSUM]
```

---

### 4. Protocol Handler (`protocol.c`)

**Purpose**: Implements the binary protocol for mobile app communication.

**Features**:

- Frame parsing and validation
- Checksum verification
- Command routing
- Response generation

**Key Commands**:

| Command            | Code | Description         |
| ------------------ | ---- | ------------------- |
| Get Device State   | 0x11 | Full device status  |
| Get Realtime CO2   | 0x10 | Current CO2 reading |
| Get History        | 0x25 | Historical CO2 data |
| Set Power Mode     | 0x02 | Change power mode   |
| Set Device Name    | 0x06 | Change BLE name     |
| Start Factory Test | 0xDD | Run hardware tests  |
| Erase History      | 0x24 | Clear stored data   |

**API**:

```c
/* Initialize protocol handler */
int protocol_init(void);

/* Send realtime CO2 notification */
void protocol_send_realtime_co2(void);

/* Send battery level */
void protocol_send_battery_level(void);

/* Send device state */
void protocol_send_device_state(void);

/* Process pending history requests */
void protocol_process_history(void);
```

---

### 5. Data Log (`data_log.c`)

**Purpose**: Manages CO2 history storage to SPI flash.

**Features**:

- Half-page based storage (128 bytes per half-page)
- 4 records per half-page (32 bytes each)
- Circular buffer with wrap-around
- Efficient retrieval by half-page number

**Storage Format**:

```c
typedef struct {
    uint8_t  record_type;      /* Record type marker */
    uint8_t  reserved[3];      /* Alignment */
    uint16_t co2_ppm;          /* CO2 reading */
    uint16_t timestamp_offset; /* Time offset */
    /* ... additional fields ... */
} co2_record_t;  /* 32 bytes total */
```

**API**:

```c
/* Initialize data log */
int data_log_init(void);

/* Store current CO2 reading */
int data_log_store_co2(uint16_t co2_ppm);

/* Get current half-page number */
uint16_t data_log_get_current_half_page(void);

/* Read specific half-page */
int data_log_read_half_page(uint16_t half_page, uint8_t *buffer);

/* Erase all history */
int data_log_erase_all(void);

/* Get record count */
uint32_t data_log_get_record_count(void);
```

**Flash Layout**:

- Start: 0x000000
- Size: 1MB (configurable)
- Half-pages: 8192 total
- Records: 32768 total (~113 days at 5-min intervals)

---

### 6. User Settings (`user_settings.c`)

**Purpose**: Manages persistent device settings using NVS.

**Features**:

- NVS-backed persistence
- Default value support
- Reset to defaults
- Thread-safe access

**Settings Available**:

| Setting      | Type   | Default | Description            |
| ------------ | ------ | ------- | ---------------------- |
| Power Mode   | uint8  | 2 (Mid) | Power consumption mode |
| BLE Enabled  | bool   | true    | BLE advertising        |
| Flight Mode  | bool   | false   | All wireless off       |
| Alarm Enable | bool   | true    | CO2 alarms             |
| Alarm High   | uint16 | 1500    | High CO2 threshold     |
| Alarm Low    | uint16 | 800     | Low CO2 threshold      |
| DND Enable   | bool   | false   | Do Not Disturb         |
| DND Start    | uint16 | 2200    | DND start time         |
| DND End      | uint16 | 0700    | DND end time           |

**API**:

```c
/* Initialize settings */
int user_settings_init(void);

/* Get/Set power mode */
uint8_t user_settings_get_power_mode(void);
int user_settings_set_power_mode(uint8_t mode);

/* Get/Set BLE enabled */
bool user_settings_get_ble_enabled(void);
int user_settings_set_ble_enabled(bool enabled);

/* Get/Set flight mode */
bool user_settings_get_flight_mode(void);
int user_settings_set_flight_mode(bool enabled);

/* Reset all settings to defaults */
int user_settings_reset_defaults(void);
```

---

### 7. Power Manager (`power_manager.c`)

**Purpose**: Manages device power states and power gestures.

**Features**:

- System OFF (shipping mode) - ~1-3 µA
- Tap + tap-hold-3s power gesture
- Wake-up gesture validation
- LED feedback during gestures

**Power States**:

| State      | Description          | Current  |
| ---------- | -------------------- | -------- |
| Active     | Normal operation     | ~5-10 mA |
| Idle       | Sensors sleeping     | ~1-2 mA  |
| System OFF | All off, button wake | ~1-3 µA  |

**Power Gesture**:

1. Tap button (< 300ms press)
2. Within 1 second, press and hold
3. Hold for 3 seconds
4. Device powers on/off

**API**:

```c
/* Initialize power manager */
int power_manager_init(void);

/* Check if woke from System OFF */
bool power_manager_is_wakeup_from_off(void);

/* Validate wake-up gesture */
bool power_manager_validate_wakeup_gesture(void);

/* Enter System OFF */
void power_manager_enter_off_state(void);

/* Gesture detection callbacks */
void power_manager_on_tap_detected(void);
void power_manager_on_button_pressed(void);
void power_manager_on_button_released(void);
```

---

### 8. LED Indicator (`led_indicator.c`)

**Purpose**: High-level LED pattern control for status indication.

**Features**:

- Non-blocking pattern engine
- Fade, flash, solid patterns
- CO2 level color mapping
- Charging and pairing indicators

**Color Mapping**:

| Color | LEDs  | RGB Values      |
| ----- | ----- | --------------- |
| Red   | LED 0 | (255, 0, 0)     |
| Green | LED 1 | (0, 255, 0)     |
| Blue  | LED 2 | (0, 0, 255)     |
| White | All   | (255, 255, 255) |
| Amber | 0+1   | (255, 255, 0)   |

**CO2 Level Colors**:

- **Green**: < 800 ppm (Good)
- **Amber**: 800-1500 ppm (Moderate)
- **Red**: > 1500 ppm (Poor)

**API**:

```c
/* Initialize LED indicator */
int led_indicator_init(void);

/* Stop any active pattern */
void led_indicator_stop(void);

/* Timed patterns */
void led_indicator_show_color(led_color_t color, uint32_t duration_ms);
void led_indicator_flash(led_color_t color, uint32_t period_ms, uint32_t duration_ms);
void led_indicator_fade(led_color_t color, uint32_t duration_ms);

/* Continuous patterns */
void led_indicator_start_flash(led_color_t color, uint32_t period_ms);
void led_indicator_start_fade(led_color_t color, uint32_t period_ms);

/* Device state indicators */
void led_indicator_show_co2_level(uint16_t co2_ppm, uint32_t duration_ms);
void led_indicator_show_low_battery(void);
void led_indicator_show_charging_low_battery(void);
void led_indicator_show_charging_normal(bool device_on, uint16_t co2_ppm);
void led_indicator_show_pairing(void);
void led_indicator_show_connected(uint16_t co2_ppm);
```

---

### 9. Factory Test (`factory_test.c`)

**Purpose**: Manufacturing test sequence and diagnostics.

**Features**:

- Automated hardware tests
- LED pass/fail indication
- Test report generation
- BLE-retrievable results

**Tests Performed**:

| Test       | Pass Criteria               |
| ---------- | --------------------------- |
| SPI Flash  | Init successful             |
| Battery    | 2000-4500 mV                |
| SHT40      | Valid serial number         |
| LPS22HH    | WHO_AM_I = 0xB3             |
| STCC4      | Valid product ID            |
| Qi Charger | Detection works (info only) |

**API**:

```c
/* Initialize factory test */
int factory_test_init(void);

/* Start automated test sequence */
int factory_test_start_auto(void);

/* Stop/abort test */
void factory_test_stop(void);

/* Check if running */
bool factory_test_is_running(void);

/* Get test report */
int factory_test_get_report(factory_test_report_t *report);
```

**LED Indication**:

- **Pass**: 3 green blinks
- **Fail**: 5 fast red blinks

---

### 10. BLE Mode Service (`ble_mode_service.c`)

**Purpose**: Manages BLE mode switching between Peripheral and Mesh.

**Features**:

- Mode switching
- State persistence
- Clean transitions

**Modes**:

- **Peripheral**: Normal app connection mode
- **Mesh**: Bluetooth Mesh sensor node

**API**:

```c
/* Initialize BLE mode service */
int ble_mode_service_init(void);

/* Get current mode */
ble_mode_t ble_mode_service_get_mode(void);

/* Set mode (takes effect on restart) */
int ble_mode_service_set_mode(ble_mode_t mode);

/* Switch mode immediately */
int ble_mode_service_switch_mode(ble_mode_t mode);
```

---

## Initialization Order

Services must be initialized in this order (handled in `main.c`):

```c
1. led_driver_init()           /* LED hardware */
2. button_driver_init()        /* Button hardware */
3. power_manager_init()        /* Check wake reason */
4. i2c_bus_init()              /* I2C for sensors */
5. spi_flash_init()            /* Flash storage */
6. battery_init()              /* Battery ADC */
7. qi_charger_init()           /* Charge detection */
8. sht40_init()                /* Temp/humidity */
9. lps22hh_init()              /* Pressure */
10. stcc4_init()               /* CO2 */
11. user_settings_init()       /* Load settings */
12. data_log_init()            /* History storage */
13. ble_service_init()         /* BLE stack */
14. airspot_nus_init()         /* NUS service */
15. protocol_init()            /* Protocol handler */
16. sensor_manager_init()      /* Sensor coordination */
17. led_indicator_init()       /* LED patterns */
18. factory_test_init()        /* Factory test */
```

---

## Thread Model

Services run in the main thread or use Zephyr work queues:

| Service        | Execution Context          |
| -------------- | -------------------------- |
| Sensor Manager | Main thread (timer-driven) |
| Protocol       | BLE callback context       |
| Data Log       | Main thread                |
| LED Indicator  | Work queue                 |
| Power Manager  | Main thread + work queue   |
| Factory Test   | Main thread                |

---

## Error Handling

All services follow consistent error handling:

```c
/* Return codes */
0       = Success
-EINVAL = Invalid argument
-ENODEV = Device not ready
-EIO    = I/O error
-ENOMEM = Out of memory
-EBUSY  = Resource busy
```

---

**Last Updated**: 2026-01-24
