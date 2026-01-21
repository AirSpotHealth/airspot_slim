# Feature Comparison: Old Firmware vs New Firmware (AirSpot Slim)

Deep analysis comparing the original nRF52832 firmware with the new nRF54L05 firmware.

---

## Summary

| Category              | Old Firmware       | New Firmware    | Coverage                                 |
| --------------------- | ------------------ | --------------- | ---------------------------------------- |
| **Protocol Commands** | 44 commands        | 28 implemented  | 64%                                      |
| **User Settings**     | Full NVS           | Full NVS        | ✅ 100%                                  |
| **Sensors**           | SCD4x CO2          | STCC4 CO2       | ✅ Different sensor (Ref T/H/P required) |
| **Button**            | Full gestures      | Basic           | ⚠️ 60%                                   |
| **BLE Services**      | NUS, DFU, BAS, DIS | NUS, DFU, DIS   | ⚠️ 75%                                   |
| **Factory Test**      | Full suite         | ❌ Missing      | 0%                                       |
| **LCD/Display**       | Full UI            | ❌ Removed (HW) | N/A                                      |

---

## 1. Protocol Commands (0xFF 0xAA ...)

### ✅ IMPLEMENTED

| Cmd                         | Hex  | Description            | Notes              |
| --------------------------- | ---- | ---------------------- | ------------------ |
| `CMD_GET_CO2_VALUE`         | 0x01 | Get CO2 reading        | ✅ Working         |
| `CMD_SET_BUZZER_STATE`      | 0x02 | Set buzzer on/off      | ✅ Stub (no HW)    |
| `CMD_SET_VIBRATOR_STATE`    | 0x03 | Set vibrator on/off    | ✅ Stub (no HW)    |
| `CMD_SET_TIMEBASE`          | 0x04 | Set device time        | ✅ Working         |
| `CMD_SET_POWER_MODE`        | 0x05 | Set Low/Normal/Eco     | ✅ Working         |
| `CMD_GET_DEVICE_STATE`      | 0x08 | Get all settings       | ✅ Working         |
| `CMD_GET_DEVICE_NAME`       | 0x09 | Get BLE name           | ✅ Working         |
| `CMD_SET_DEVICE_NAME`       | 0x0A | Set BLE name           | ✅ Stub (ACK only) |
| `CMD_GET_CURRENT_HALF_PAGE` | 0x0B | Get history page count | ✅ Working         |
| `CMD_HISTORY_REQUEST`       | 0x0C | Get history page data  | ✅ Working         |
| `CMD_GET_HARDWARE_VERSION`  | 0x12 | Get HW version         | ✅ Working         |
| `CMD_GET_SOFTWARE_VERSION`  | 0x13 | Get SW version         | ✅ Working         |
| `CMD_ERASE_HISTORY`         | 0xFD | Erase all history      | ✅ Working         |
| `CMD_GET_SYSTEM_TIME`       | 0xFE | Get current time       | ✅ Working         |
| `CMD_GET_BATTERY_LEVEL`     | 0x20 | Get battery %          | ✅ Working         |
| `CMD_REFRESH_CO2_VALUE`     | 0x21 | Force measurement      | ✅ Working         |
| `CMD_RESET_DEVICE`          | 0x27 | Reboot device          | ✅ Working         |
| `CMD_GET_SENSOR_DETAILS`    | 0x30 | Get sensor info        | ✅ Working (STCC4) |
| `CMD_SET_FLIGHT_MODE`       | 0x32 | Toggle flight mode     | ✅ Working         |

### ⚠️ PARTIALLY IMPLEMENTED (Stubs/Defaults)

| Cmd                                  | Hex  | Description            | Status                               |
| ------------------------------------ | ---- | ---------------------- | ------------------------------------ |
| `CMD_SET_GRAPH_YELLOW_AND_RED_START` | 0x07 | Set CO2 thresholds     | Returns defaults (800/1200)          |
| `CMD_SET_DND`                        | 0x22 | Set DND schedule       | ⚠️ Not connected to actual DND logic |
| `CMD_SET_GRAPH_MODE`                 | 0x28 | Set graph display mode | N/A (no display)                     |

### ❌ NOT IMPLEMENTED

| Cmd                                  | Hex  | Description           | Priority                          |
| ------------------------------------ | ---- | --------------------- | --------------------------------- |
| `CMD_SET_BLE_DISCONNECT`             | 0x06 | Force disconnect      | 🟡 Low                            |
| `CMD_CALIB_START`                    | 0x0D | Start calibration     | 🟠 Medium (STCC4 doesn't support) |
| `CMD_SET_SCREEN_CONST`               | 0x0E | Keep screen on        | N/A (no display)                  |
| `CMD_FIND_MY_AIRSPOT`                | 0x10 | Blink/beep for locate | 🟠 Medium                         |
| `CMD_SET_SELF_CALIB`                 | 0x11 | Toggle ASC            | 🟡 Low (STCC4 no ASC)             |
| `CMD_SET_SENSOR_ERROR_DEBUG`         | 0xFF | Debug mode            | 🟡 Low                            |
| `CMD_POPULATE_FAKE_RECORDS`          | 0x23 | Test data             | 🟡 Low (dev only)                 |
| `CMD_RESET_SENSOR`                   | 0x24 | Reset CO2 sensor      | 🟠 Medium                         |
| `CMD_GET_ASC_COUNT`                  | 0x25 | Get ASC count         | 🟡 Low (STCC4 no ASC)             |
| `CMD_GET_DEVICE_STATE_DEBUG`         | 0x26 | Debug state dump      | 🟡 Low (dev only)                 |
| `CMD_SET_ASC_CHECK_DURATION`         | 0x29 | Set ASC interval      | 🟡 Low (STCC4 no ASC)             |
| `CMD_GET_ASC_DAY_COUNT`              | 0x2A | Get ASC days          | 🟡 Low (STCC4 no ASC)             |
| `CMD_GET_SENSOR_VARIANT`             | 0x2B | Get CO2 sensor type   | 🟡 Low                            |
| `CMD_SET_ADVANCED_ALARM`             | 0x2C | Set alarm threshold   | 🟠 Medium                         |
| `CMD_SET_ILLUMINATE_SCREEN_ON_ALARM` | 0x2D | Screen on alarm       | N/A (no display)                  |
| `CMD_SET_ALARM_CO2_FALLING`          | 0x2E | Alarm on CO2 drop     | 🟡 Low                            |
| `CMD_RESET_ADVANCED_ALARMS`          | 0x2F | Clear all alarms      | 🟠 Medium                         |
| `CMD_SET_CO2_SCALE_FACTOR`           | 0x31 | Adjust CO2 scaling    | 🟡 Low                            |

### ❌ FACTORY TEST COMMANDS (NOT IMPLEMENTED)

| Cmd                            | Hex  | Description         | Priority  |
| ------------------------------ | ---- | ------------------- | --------- |
| `CMD_ENTER_FACTORY_TEST_MODE`  | 0xD0 | Enter test mode     | 🔴 High   |
| `CMD_START_FACTORY_AUTO_TESTS` | 0xD1 | Run auto tests      | 🔴 High   |
| `CMD_START_MANUAL_TEST`        | 0xD2 | Start manual test   | 🔴 High   |
| `CMD_MANUAL_TEST_CONFIRM`      | 0xD3 | Confirm manual test | 🔴 High   |
| `CMD_FACTORY_TEST_RESULT`      | 0xDD | Send test result    | 🔴 High   |
| `CMD_FACTORY_TEST_ABORT`       | 0xDE | Abort testing       | 🔴 High   |
| `CMD_SET_DEVICE_SLEEP`         | 0xEE | Deep sleep          | 🟠 Medium |
| `CMD_FACTORY_RESET`            | 0xFA | Full factory reset  | 🟠 Medium |

---

## 2. Button Handling

### Old Firmware Events

| Event                | Description     | New Status     |
| -------------------- | --------------- | -------------- |
| `BTN_EVT_DOWN`       | Button pressed  | ✅ Implemented |
| `BTN_EVT_UP`         | Button released | ✅ Implemented |
| `BTN_EVT_PRESS`      | Short press     | ✅ Implemented |
| `BTN_EVT_PRESS_2S`   | 2-second hold   | ✅ Implemented |
| `BTN_EVT_PRESS_5S`   | 5-second hold   | ❌ Missing     |
| Double-tap detection | 400ms window    | ❌ Missing     |

### Button Actions Needed

- [ ] Add 5-second long press for factory reset trigger
- [ ] Add double-tap detection for quick actions

---

## 3. BLE Services

| Service               | Old | New        | Notes                            |
| --------------------- | --- | ---------- | -------------------------------- |
| **NUS** (UART)        | ✅  | ✅         | Working                          |
| **DIS** (Device Info) | ✅  | ✅         | Working                          |
| **BAS** (Battery)     | ✅  | ❌ Missing | Should add for app compatibility |
| **DFU** (Buttonless)  | ✅  | ✅         | MCUmgr SMP                       |

### BLE Features Needed

- [ ] Add BLE Battery Service (BAS) for standard battery reporting
- [ ] Verify advertising data matches old firmware format

---

## 4. User Settings (NVS)

| Setting               | Old | New          | Notes                  |
| --------------------- | --- | ------------ | ---------------------- |
| Buzzer enabled        | ✅  | ✅           | Stub on Slim           |
| Vibrator enabled      | ✅  | ✅           | Stub on Slim           |
| Power mode            | ✅  | ✅           | Working                |
| Device name           | ✅  | ❌ Read-only | Could add              |
| DND mode              | ✅  | ⚠️ Partial   | Not connected to logic |
| DND start/end time    | ✅  | ⚠️ Partial   | Not connected to logic |
| Flight mode           | ✅  | ✅           | Working                |
| Yellow/Red thresholds | ✅  | ⚠️ Hardcoded | Could add to NVS       |
| Graph mode            | ✅  | N/A          | No display             |
| Advanced alarms (10)  | ✅  | ❌ Missing   | Could add for app      |
| CO2 scale factor      | ✅  | ❌ Missing   | STCC4 pre-calibrated   |

---

## 5. Time Management

| Feature                        | Old | New        | Notes                        |
| ------------------------------ | --- | ---------- | ---------------------------- |
| Timebase set from app          | ✅  | ✅         | Working                      |
| Increment each second          | ✅  | ✅         | Working                      |
| Hour/minute functions          | ✅  | ✅         | Working                      |
| DND time check                 | ✅  | ⚠️ Stub    | Function exists but not used |
| Flight mode auto-disable (12h) | ✅  | ❌ Missing | Need to add                  |
| ASC daily check                | ✅  | N/A        | STCC4 no ASC                 |

---

## 6. Factory Test Mode

### Old Firmware Tests

| Test ID | Name                 | Type   | New Status        |
| ------- | -------------------- | ------ | ----------------- |
| 1       | SPI Flash            | Auto   | ❌                |
| 2       | Battery ADC          | Auto   | ❌                |
| 3       | Temperature/Humidity | Auto   | ❌                |
| 4       | CO2 Sensor           | Auto   | ❌                |
| 5       | Pressure Sensor      | Auto   | ❌                |
| 6       | Charge Detection     | Auto   | ❌                |
| 7       | Screen Edge          | Manual | N/A (no LCD)      |
| 8       | Screen Black         | Manual | N/A (no LCD)      |
| 9       | Screen White         | Manual | N/A (no LCD)      |
| 10      | Button Press         | Manual | ❌                |
| 11      | Buzzer               | Manual | N/A (no buzzer)   |
| 12      | Vibration            | Manual | N/A (no vibrator) |

**Recommended Slim Factory Tests:**

1. SPI Flash read/write
2. Battery ADC (voltage measurement)
3. I2C Sensors (SHT40, LPS22HH, STCC4)
4. Qi Charger detection
5. BLE advertising
6. LED RGB cycling
7. Button press

---

## 7. Power Management

| Feature                            | Old | New        |
| ---------------------------------- | --- | ---------- |
| System Off mode                    | ✅  | ❌         |
| Wake on button                     | ✅  | ❌         |
| Wake on USB/charger                | ✅  | ❌         |
| Variable sensor rate by power mode | ✅  | ✅         |
| Low battery warning                | ✅  | ⚠️ Partial |
| Low battery shutdown               | ✅  | ❌         |

---

## 8. History/Data Logging

| Feature                 | Old | New         |
| ----------------------- | --- | ----------- |
| Record format (8 bytes) | ✅  | ✅          |
| SPI flash storage       | ✅  | ✅          |
| Page-based retrieval    | ✅  | ✅          |
| Erase all               | ✅  | ✅          |
| Error/event logging     | ✅  | ⚠️ CO2 only |

---

## Priority Action Items

### 🔴 HIGH PRIORITY

1. **Factory Test Mode** - Required for production
2. **BLE Battery Service (BAS)** - App expects this
3. **Find My AirSpot** - LED blink for device location

### 🟠 MEDIUM PRIORITY

4. **Power off mode** - Battery life
5. **DND logic connection** - Silent hours
6. **Flight mode 12h auto-disable** - Safety feature
7. **Advanced alarms** - App feature

### 🟡 LOW PRIORITY

8. Double-tap button detection
9. Device name persistent storage
10. CO2 scale factor adjustment
11. ASC-related commands (not applicable to STCC4)

---

## Hardware Differences (Cannot Implement)

| Old Device   | Slim                         |
| ------------ | ---------------------------- |
| LCD Display  | ❌ Removed                   |
| Buzzer       | ❌ Removed                   |
| Vibrator     | ❌ Removed                   |
| USB Charging | Qi Wireless                  |
| SCD4x CO2    | STCC4 CO2 (Needs T/H/P Comp) |
| nRF52832     | nRF54L05                     |

---

## 9. Sensor Compensation (New Requirement)

Old firmware adjusted SCD4x output based on manual offsets or external pressure if available.
New firmware **MUST** performs active compensation for the STCC4 sensor using:

1. **Temperature/Humidity**: from on-board **SHT40**.
2. **Pressure**: from on-board **LPS22HH**.

**Current Status:**

- SHT40 integration: ✅
- LPS22HH integration: ✅
- STCC4 Pressure Compensation: ❌ Missing (Critical for accuracy)
- STCC4 Temp/Hum Compensation: ⚠️ Partially Implemented
