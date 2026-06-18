# AirSpot Protocol Specification

This document details the binary communication protocol used between the AirSpot Slim firmware and the mobile application via BLE Nordic UART Service (NUS).

## 1. Frame Format

All commands and responses follow this generic frame structure:

| Byte Index | Field    | Length | Description                            |
| :--------: | :------- | :----: | :------------------------------------- |
|     0      | Header 1 |   1    | Fixed `0xFF`                           |
|     1      | Header 2 |   1    | Fixed `0xAA`                           |
|     2      | Command  |   1    | Command ID (e.g., `0x01` for Get CO2)  |
|     3      | Length   |   1    | Length of the DATA payload only        |
|   4...N    | Data     |  Var   | Command parameters or Response data    |
|    N+1     | Checksum |   1    | Sum of bytes 0 to N (Low 8 updatebits) |

**Checksum Calculation:**
`Checksum = (Sum(Byte[0]...Byte[N])) & 0xFF`

> **Note:** All multi-byte integer values are **Big Endian** unless specified otherwise.

---

## 2. Command List

### **Realtime Data**

#### **Get Realtime CO2 Value (0x01)**

Requests the latest sensor reading.

- **Request:** `[FF][AA][01][00][CS]`
- **Response:**
  | Byte | Value | Description |
  |---|---|---|
  | 0-3 | Header+Len | `[FF][AA][01][0A]` (Len = 10) |
  | 4-7 | Time | Timestamp (Unix/Device time, uint32) |
  | 8-9 | CO2 | CO2 Concentration in PPM (uint16) |
  | 10-11 | Temp | Temperature × 100 (int16) (e.g., 2550 = 25.50°C) |
  | 12-13 | Hum | Humidity × 100 (uint16) (e.g., 5050 = 50.50%RH) |
  | 14 | Checksum | |

#### **Refresh CO2 Value (0x21)**

Triggers an immediate measurement and returns the result (Active polling).

- **Request:** `[FF][AA][21][00][CS]`
- **Response:** Same as `0x01` (Get Realtime CO2 Value).

#### **Get Battery Level (0x20)**

- **Request:** `[FF][AA][20][00][CS]`
- **Response:**
  | Byte | Value | Description |
  |---|---|---|
  | 0-3 | Header+Len | `[FF][AA][20][01]` |
  | 4 | Level | Battery Percentage (0-100) |
  | 5 | Charging | `1` = Charging, `0` = Not Charging |
  | 6 | Checksum | (Note: Payload is 2 bytes but Len is usually 1 due to legacy bug) |

#### **Get System Time (0xFE)**

- **Request:** `[FF][AA][FE][00][CS]`
- **Response:** `[FF][AA][FE][04][Time(4)][CS]`

---

## 3. Device Configuration

#### **Get Device State (0x08)**

Retrieves a full dump of device settings.

- **Request:** `[FF][AA][08][00][CS]`
- **Response:** `[FF][AA][08][Len][...Payload...][CS]`
  **Payload Structure:**
  - `[0]`: Buzzer State (1=On, 0=Off)
  - `[1]`: Vibrator State (1=On, 0=Off)
  - `[2]`: Power Mode (0=Normal, 1=Saver, etc.)
  - `[3-4]`: Yellow Threshold (e.g., 800)
  - `[5-6]`: Red Threshold (e.g., 1200)
  - `[7]`: Screen Const On (Legacy, unused)
  - `[8]`: Auto Calibrate Enabled (1/0)
  - `[9]`: DND Mode
  - `[10-13]`: DND Start/End Times (H:M, H:M)
  - `[14-15]`: Calibration Target (Legacy)
  - `...`: (Legacy/Unused fields for graph/alarms)
  - `[End-4 ... End]`: Flight Mode

#### **Set Device Name (Alias) (0x0A)**

Sets the user-friendly name displayed in the app.

- **Request:** `[FF][AA][0A][Len][NameString...][CS]`
- **Response:** `[FF][AA][0A][01][01][CS]` (Success)

#### **Get Device Name (0x09)**

- **Request:** `[FF][AA][09][00][CS]`
- **Response:** `[FF][AA][09][Len][NameString...][CS]`

#### **Set Timebase (0x04)**

Syncs device time with phone time.

- **Request:** `[FF][AA][04][04][Time(4)][CS]`
- **Response:** `[FF][AA][04][01][01][CS]` (Success)

#### **Set Buzzer State (0x02)**

- **Request:** `[FF][AA][02][01][State(1)][CS]`
- **Response:** Triggers `CMD_GET_DEVICE_STATE` response.

#### **Set Vibrator State (0x03)**

- **Request:** `[FF][AA][03][01][State(1)][CS]`
- **Response:** Triggers `CMD_GET_DEVICE_STATE` response.

#### **Set Power Mode (0x05)**

- **Request:** `[FF][AA][05][01][Mode(1)][CS]`
- **Response:** Triggers `CMD_GET_DEVICE_STATE` response.

#### **Set Do Not Disturb (DND) (0x22)**

- **Request:** `[FF][AA][22][05][Enabled][StartH][StartM][EndH][EndM][CS]`
- **Response:** Triggers `CMD_GET_DEVICE_STATE` response.

#### **Set Flight Mode (0x32)**

- **Request:** `[FF][AA][32][01][Mode(1)][CS]`
- **Response:** Triggers `CMD_GET_DEVICE_STATE` response.

#### **Set BLE Disconnect (0x06)**

Forces the device to disconnect BLE.

- **Request:** `[FF][AA][06][00][CS]`
- **Response:** None (Connection dropped).

#### **Set Device Sleep (Ship Mode) (0xEE)**

Turns the device off completely.

- **Request:** `[FF][AA][EE][00][CS]`
- **Response:** `[FF][AA][EE][01][01][CS]` (ACK before shutdown)

#### **Reset Device (0x27)**

Reboots the device.

- **Request:** `[FF][AA][27][00][CS]`
- **Response:** None (Reboot).

#### **Factory Reset (0xFA)**

Clears all settings and history.

- **Request:** `[FF][AA][FA][00][CS]`
- **Response:** `[FF][AA][FA][01][01][CS]` (ACK before reset)

---

## 4. History Data (Data Logging)

#### **Get Current Write Page (0x0B)**

Gets the current half-page index where data is being written.

- **Request:** `[FF][AA][0B][00][CS]`
- **Response:** `[FF][AA][0C][01][Page(2)][CS]`
  > **Note:** Response uses command ID `0x0C`.

#### **Request History Page (0x0C)**

Requests a specific half-page of history data.

- **Request:** `[FF][AA][0C][02][Page(2)][CS]`
- **Response:**
  | Byte | Value | Description |
  |---|---|---|
  | 0-3 | Header+Len | `[FF][AA][0C][80]` (Len=128) |
  | 4-131 | Data | 16 Records (8 bytes each) |
  | 132-133 | Page | Page Number (uint16) |
  | 134 | Checksum | |

  **Record Format (8 Bytes):**
  - `[0-3]`: Timestamp (uint32 Big Endian)
  - `[4-5]`: Value (uint16 **Little Endian**)
  - `[6]`: Type (uint8)
  - `[7]`: Reserved

#### **Erase History (0xFD)**

- **Request:** `[FF][AA][FD][00][CS]`
- **Response:** `[FF][AA][FD][01][01][CS]` (Sent when erase is complete)

---

## 5. Calibration & Sensors

#### **Set Self Calibration (0x11)**

Enables/Disables Automatic Self Calibration (ASC).

- **Request:** `[FF][AA][11][01][Enable(1)][CS]`
- **Response:** `[FF][AA][11][01][02][CS]`

#### **Set CO₂ LED thresholds (0x07)**

- **Request:** `[FF][AA][07][04][YellowStart(2)][RedStart(2)][CS]` (green/amber and amber/red boundaries in ppm)
- **Response:** Full device state (`0x08`)

#### **Start Manual Calibration (0x0D)**

- **Set target only:** `[FF][AA][0D][02][Target(2)][CS]` (does not start)
- **Start:** `[FF][AA][0D][00][CS]`
- **Response (Start):** `[FF][AA][0D][01][02][CS]`
- **Response (Done):** `[FF][AA][0D][01][Correction(2)][Sign(1)][CS]`
- **Countdown:** `[FF][AA][0F][01][Seconds(2)][CS]`

#### **Get ASC Count (Correction) (0x25)**

- **Request:** `[FF][AA][25][00][CS]`
- **Response:** `[FF][AA][25][01][Abs(2)][Sign(1)][CS]`

#### **Reset Sensor (0x24)**

Resets the CO2 sensor module.

- **Request:** `[FF][AA][24][00][CS]`
- **Response:** `[FF][AA][24][01][01][CS]`

#### **Get Sensor Variant (0x2B)**

- **Request:** `[FF][AA][2B][00][CS]`
- **Response:** `[FF][AA][2B][01][02][CS]` (02 = STCC4)

#### **Get Sensor Details (0x30)**

- **Request:** `[FF][AA][30][00][CS]`
- **Response:** `[FF][AA][30][10][...Details...][CS]`

---

## 6. Utilities

#### **Find My AirSpot (0x10)**

Blinks LEDs.

- **Request:** `[FF][AA][10][00][CS]`
- **Response:** `[FF][AA][10][01][01][CS]`

#### **Get Hardware Version (0x12)**

- **Request:** `[FF][AA][12][00][CS]`
- **Response:** `[FF][AA][12][Len][String...][CS]`

#### **Get Software Version (0x13)**

- **Request:** `[FF][AA][13][00][CS]`
- **Response:** `[FF][AA][13][Len][String...][CS]`

#### **Enter Factory Test (0xD0)**

- **Request:** `[FF][AA][D0][00][CS]`
- **Response:** `[FF][AA][D0][01][01][CS]`

#### **Get Factory Test Result (0xDD)**

- **Request:** `[FF][AA][DD][00][CS]`
- **Response:** `[FF][AA][DD][0A][Report...][CS]`
