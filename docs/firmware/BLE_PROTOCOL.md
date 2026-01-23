# BLE Protocol Documentation

Complete reference for the AirSpot Slim BLE communication protocol.

## Overview

The AirSpot Slim uses a binary protocol over the Nordic UART Service (NUS) for communication with the mobile app. This protocol is fully compatible with the original AirSpot device.

## NUS Service

**Service UUID**: `6E400001-B5A3-F393-E0A9-E50E24DCCA9E`

| Characteristic | UUID                                   | Properties                    |
| -------------- | -------------------------------------- | ----------------------------- |
| TX             | `6E400003-B5A3-F393-E0A9-E50E24DCCA9E` | Notify                        |
| RX             | `6E400002-B5A3-F393-E0A9-E50E24DCCA9E` | Write, Write Without Response |

---

## Frame Format

All protocol messages use the following frame format:

```
┌──────┬──────┬─────┬─────┬──────────┬──────────┐
│ 0xFF │ 0xAA │ CMD │ LEN │ DATA...  │ CHECKSUM │
└──────┴──────┴─────┴─────┴──────────┴──────────┘
   1B     1B    1B    1B    0-251B       1B
```

| Field    | Size        | Description                       |
| -------- | ----------- | --------------------------------- |
| Header   | 2 bytes     | Always `0xFF 0xAA`                |
| CMD      | 1 byte      | Command code                      |
| LEN      | 1 byte      | Data length (0-251)               |
| DATA     | 0-251 bytes | Command-specific payload          |
| CHECKSUM | 1 byte      | XOR of all bytes including header |

### Checksum Calculation

```c
uint8_t calculate_checksum(const uint8_t *frame, uint16_t len) {
    uint8_t checksum = 0;
    for (uint16_t i = 0; i < len - 1; i++) {
        checksum ^= frame[i];
    }
    return checksum;
}
```

---

## Command Reference

### Device Information Commands

#### Get Device State (0x08)

**Direction**: App → Device → App (Request/Response)

**Request**: No data payload

```
FF AA 08 00 [CHK]
```

**Response**: Full device state

```
FF AA 08 [LEN] [DATA...] [CHK]
```

| Offset | Size | Field                          |
| ------ | ---- | ------------------------------ |
| 0      | 2    | CO2 value (ppm, big-endian)    |
| 2      | 2    | Temperature (°C × 100, signed) |
| 4      | 2    | Humidity (%RH × 100)           |
| 6      | 1    | Battery level (0-100%)         |
| 7      | 1    | Charging status (0=no, 1=yes)  |
| 8      | 1    | Power mode (0-3)               |
| 9      | 1    | BLE enabled (0/1)              |
| 10     | 1    | Flight mode (0/1)              |
| 11     | 1    | Alarm enabled (0/1)            |
| 12     | 2    | Alarm high threshold (ppm)     |
| 14     | 2    | Alarm low threshold (ppm)      |
| 16     | 1    | DND enabled (0/1)              |
| 17     | 2    | DND start (HHMM)               |
| 19     | 2    | DND end (HHMM)                 |
| 21     | 1    | Device type (0x02 = Slim)      |
| 22     | 2    | Pressure (hPa, optional)       |

---

#### Get Hardware Version (0x12)

**Direction**: App → Device → App

**Request**:

```
FF AA 12 00 [CHK]
```

**Response**:

```
FF AA 12 04 [MAJOR] [MINOR] [PATCH] [BUILD] [CHK]
```

---

#### Get Software Version (0x13)

**Direction**: App → Device → App

**Request**:

```
FF AA 13 00 [CHK]
```

**Response**:

```
FF AA 13 04 [MAJOR] [MINOR] [PATCH] [BUILD] [CHK]
```

---

#### Get Device Name (0x09)

**Direction**: App → Device → App

**Request**:

```
FF AA 09 00 [CHK]
```

**Response**:

```
FF AA 09 [LEN] [NAME...] [CHK]
```

---

#### Set Device Name (0x0A)

**Direction**: App → Device

**Request**:

```
FF AA 0A [LEN] [NAME...] [CHK]
```

Name is ASCII string, max 20 characters.

---

### Sensor Commands

#### Get Realtime CO2 (0x01)

**Direction**: App → Device → App

**Request**:

```
FF AA 01 00 [CHK]
```

**Response**:

```
FF AA 01 08 [CO2_H][CO2_L][TEMP_H][TEMP_L][HUM_H][HUM_L][BAT][CHG] [CHK]
```

| Offset | Size | Field                  |
| ------ | ---- | ---------------------- |
| 0      | 2    | CO2 (ppm, big-endian)  |
| 2      | 2    | Temperature (°C × 100) |
| 4      | 2    | Humidity (%RH × 100)   |
| 6      | 1    | Battery %              |
| 7      | 1    | Charging (0/1)         |

---

#### Refresh CO2 Value (0x21)

**Direction**: App → Device

Forces an immediate sensor measurement.

**Request**:

```
FF AA 21 00 [CHK]
```

Response is sent as Realtime CO2 (0x01) notification.

---

#### Get Sensor Variant (0x2B)

**Direction**: App → Device → App

**Request**:

```
FF AA 2B 00 [CHK]
```

**Response**:

```
FF AA 2B 01 [VARIANT] [CHK]
```

Variant: 0x00 = SCD40, 0x01 = SCD41, 0x02 = STCC4/STC41

---

#### Reset Sensor (0x24)

**Direction**: App → Device

Reinitializes the CO2 sensor.

**Request**:

```
FF AA 24 00 [CHK]
```

---

### Battery Commands

#### Get Battery Level (0x20)

**Direction**: App → Device → App

**Request**:

```
FF AA 20 00 [CHK]
```

**Response**:

```
FF AA 20 03 [LEVEL] [VOLTAGE_H] [VOLTAGE_L] [CHK]
```

| Offset | Size | Field                    |
| ------ | ---- | ------------------------ |
| 0      | 1    | Battery level (0-100%)   |
| 1      | 2    | Voltage (mV, big-endian) |

---

### History Commands

#### Get Current Half-Page (0x0B)

**Direction**: App → Device → App

Gets the current write position in history storage.

**Request**:

```
FF AA 0B 00 [CHK]
```

**Response**:

```
FF AA 0B 02 [PAGE_H] [PAGE_L] [CHK]
```

---

#### Request History Data (0x0C)

**Direction**: App → Device → App

Request data for a specific half-page.

**Request**:

```
FF AA 0C 02 [PAGE_H] [PAGE_L] [CHK]
```

**Response**: Multiple frames containing history records

```
FF AA 0C [LEN] [PAGE_H][PAGE_L][RECORDS...] [CHK]
```

Each record is 32 bytes containing:

- CO2 value (2 bytes)
- Timestamp offset (2 bytes)
- Additional sensor data

---

#### Erase History (0xFD)

**Direction**: App → Device → App

Erases all stored history data.

**Request**:

```
FF AA FD 00 [CHK]
```

**Response** (when complete):

```
FF AA FD 00 [CHK]
```

---

### Settings Commands

#### Set Power Mode (0x05)

**Direction**: App → Device

**Request**:

```
FF AA 05 01 [MODE] [CHK]
```

| Mode | Description | Measurement Interval |
| ---- | ----------- | -------------------- |
| 0x00 | On-demand   | Manual only          |
| 0x01 | Low         | 30 seconds           |
| 0x02 | Medium      | 15 seconds           |
| 0x03 | High        | 5 seconds            |

---

#### Set Flight Mode (0x32)

**Direction**: App → Device

**Request**:

```
FF AA 32 01 [ENABLED] [CHK]
```

Enabled: 0x00 = off, 0x01 = on

---

#### Set Do Not Disturb (0x22)

**Direction**: App → Device

**Request**:

```
FF AA 22 05 [ENABLED][START_H][START_L][END_H][END_L] [CHK]
```

| Offset | Size | Field             |
| ------ | ---- | ----------------- |
| 0      | 1    | Enabled (0/1)     |
| 1      | 2    | Start time (HHMM) |
| 3      | 2    | End time (HHMM)   |

---

### Device Control Commands

#### Find My AirSpot (0x10)

**Direction**: App → Device

Blinks LEDs for identification.

**Request**:

```
FF AA 10 00 [CHK]
```

---

#### Reset Device (0x27)

**Direction**: App → Device

Performs a soft reset.

**Request**:

```
FF AA 27 00 [CHK]
```

---

#### Device Sleep/Power Off (0xEE)

**Direction**: App → Device

Enters System OFF (shipping mode).

**Request**:

```
FF AA EE 00 [CHK]
```

Device will power off after this command. Requires power-on gesture to wake.

---

#### Factory Reset (0xFA)

**Direction**: App → Device

Resets all settings to defaults and erases history.

**Request**:

```
FF AA FA 00 [CHK]
```

---

### Factory Test Commands

#### Enter Factory Test (0xD0)

**Direction**: App → Device

Starts automated factory test sequence.

**Request**:

```
FF AA D0 00 [CHK]
```

---

#### Get Factory Test Result (0xDD)

**Direction**: App → Device → App

**Request**:

```
FF AA DD 00 [CHK]
```

**Response**:

```
FF AA DD 07 [FLASH][BAT][SHT40][STCC4][LPS22][QI][BAT_MV_H][BAT_MV_L] [CHK]
```

| Offset | Size | Field                           |
| ------ | ---- | ------------------------------- |
| 0      | 1    | Flash status (0=fail, 1=pass)   |
| 1      | 1    | Battery status (0=fail, 1=pass) |
| 2      | 1    | SHT40 status                    |
| 3      | 1    | STCC4 status                    |
| 4      | 1    | LPS22HH status                  |
| 5      | 1    | Qi charger present (0/1)        |
| 6      | 2    | Battery voltage (mV)            |

---

### Calibration Commands

#### Start Calibration (0x0D)

**Direction**: App → Device

Starts CO2 sensor conditioning/calibration.

**Request**:

```
FF AA 0D 00 [CHK]
```

---

#### Set CO2 Scale Factor (0x31)

**Direction**: App → Device

Sets CO2 reading scale factor (stub - not implemented).

**Request**:

```
FF AA 31 02 [FACTOR_H][FACTOR_L] [CHK]
```

---

### System Commands

#### Get System Time (0xFE)

**Direction**: App → Device → App

**Request**:

```
FF AA FE 00 [CHK]
```

**Response**:

```
FF AA FE 04 [TIME_3][TIME_2][TIME_1][TIME_0] [CHK]
```

Returns system uptime in seconds (32-bit, big-endian).

---

#### Set Timebase (0x04)

**Direction**: App → Device

Sets the device's time reference.

**Request**:

```
FF AA 04 04 [TIME_3][TIME_2][TIME_1][TIME_0] [CHK]
```

---

#### BLE Disconnect (0x06)

**Direction**: App → Device

Requests the device to disconnect BLE.

**Request**:

```
FF AA 06 00 [CHK]
```

---

## Notifications

The device sends unsolicited notifications:

### Realtime CO2 Notification

Sent periodically when connected (every measurement cycle).

```
FF AA 01 08 [CO2_H][CO2_L][TEMP_H][TEMP_L][HUM_H][HUM_L][BAT][CHG] [CHK]
```

---

## Error Handling

### Invalid Frame

If a frame has invalid checksum or format, it is silently discarded.

### Unknown Command

Unknown command codes are logged but not responded to.

### Buffer Overflow

The receive buffer holds up to 256 bytes. Excess data is discarded.

---

## Implementation Notes

### Thread Safety

Protocol processing occurs in the BLE callback context. Long operations (like flash reads) are deferred to the main loop.

### MTU Considerations

Responses may be chunked if larger than the negotiated MTU. The protocol layer handles this automatically.

### History Transfer

History data is sent in chunks to avoid BLE congestion. The `protocol_process_history()` function should be called periodically from the main loop.

---

## Example Communication

### Getting Device State

**App sends**:

```
FF AA 08 00 A2
```

**Device responds**:

```
FF AA 08 18
  01 86       // CO2 = 390 ppm
  09 74       // Temp = 24.20°C (2420)
  19 00       // Humidity = 64.0% (6400)
  55          // Battery = 85%
  00          // Not charging
  02          // Power mode = Medium
  01          // BLE enabled
  00          // Flight mode off
  01          // Alarm enabled
  05 DC       // Alarm high = 1500
  03 20       // Alarm low = 800
  00          // DND disabled
  00 00       // DND start
  00 00       // DND end
  02          // Device type = Slim
  03 ED       // Pressure = 1005 hPa
  [CHK]
```

---

**Last Updated**: 2026-01-24
