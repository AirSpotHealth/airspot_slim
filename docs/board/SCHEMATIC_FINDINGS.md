# Key Findings from Schematic Analysis

**Schematic**: AH-013-0001-SCH CO2 Monitor (Standard Build) PWA Schematics Rev1A  
**Date**: 2025-01-XX

## 🎯 Critical Discoveries

### 1. **NEW SENSORS ADDED** ⚠️

The new board has **TWO ADDITIONAL SENSORS** not present in the original firmware:

#### ✅ **SHT40 Temperature & Humidity Sensor**
- **I2C Address**: 0x44 (7-bit)
- **Purpose**: Temperature and humidity measurement
- **Specifications**:
  - Operating Current: 2.2 uA (avg)
  - Idle Current: 1 uA
  - Temp Range: -40°C to 125°C
  - Voltage: 1.08V to 3.6V
- **Status**: **NEW** - Driver needs to be implemented
- **Impact**: Additional sensor data available for monitoring

#### ✅ **LPS22HH Pressure Sensor**
- **I2C Address**: 0x5D (7-bit)
- **Purpose**: Barometric pressure measurement
- **Interrupt Pin**: `NRF_LPS_INT` (Pin 03)
- **Specifications**:
  - Operating Current: 4 uA (avg)
  - Sleep Current: 0.9 uA
  - Temp Range: -40°C to 85°C
  - Voltage: 2.7V to 5.5V
- **Status**: **NEW** - Driver needs to be implemented
- **Impact**: 
  - Additional environmental data
  - Interrupt capability for wake-up
  - Can be used for altitude/weather compensation

### 2. **Flash Memory Changed** ⚠️

- **Original**: ZB25D16 (assumed)
- **New**: **STCC4-D-R3**
- **Impact**: 
  - Different SPI flash chip
  - May have different command set
  - Driver needs to be verified/updated

### 3. **I2C Bus Shared by 3 Sensors**

All sensors share the same I2C bus:
- **CO2 Sensor (SCD4x)**: Address 0x64
- **Temp/Humidity (SHT40)**: Address 0x44
- **Pressure (LPS22HH)**: Address 0x5D

**Implementation Note**: Need to ensure proper I2C bus management for multiple devices.

### 4. **User Interface Changes**

#### ✅ **RGB LED Present**
- **Model**: CL-505S-X-SD-T
- **Channels**: 3 (Red, Green, Blue)
- **Control Pins**: `NRF_UI_LED_1`, `NRF_UI_LED_2`, `NRF_UI_LED_3`
- **Status**: **NEW** - Not in original firmware (original had LCD display)

#### ❓ **Buzzer/Vibrator Status**
- **Not visible** on User Interface sheet (Sheet 8)
- **Need to verify**: 
  - Are they present on another sheet?
  - Were they removed from this design?
  - Original firmware had both buzzer and vibrator

### 5. **Button Configuration**

- **Signal**: `NRF_UI_nBTN_1` (active low)
- **Debounce**: To be done in firmware
- **Status**: Same as original (single button)

### 6. **Qi Charging**

- **Charge Status Pin**: `NRF_QC_4CHG` (Pin 01)
- **Enable Pin**: `NRF_QC_EN1` (Pin 05)
- **Status**: Confirmed present (as expected)

## 📊 Comparison: Original vs New Hardware

| Feature           | Original (nRF52832) | New (nRF54L05)   | Status      |
| ----------------- | ------------------- | ---------------- | ----------- |
| **MCU**           | nRF52832            | nRF54L05         | ✅ Changed   |
| **CO2 Sensor**    | SCD4x (0x64)        | SCD4x (0x64)     | ✅ Same      |
| **Temp/Humidity** | ❌ None              | ✅ SHT40 (0x44)   | 🆕 **NEW**   |
| **Pressure**      | ❌ None              | ✅ LPS22HH (0x5D) | 🆕 **NEW**   |
| **Flash**         | ZB25D16 (assumed)   | STCC4-D-R3       | ⚠️ Changed   |
| **Display**       | ✅ ST7301 LCD        | ❌ None           | ❌ Removed   |
| **LEDs**          | ❌ None              | ✅ RGB LED        | 🆕 **NEW**   |
| **Button**        | ✅ Single            | ✅ Single         | ✅ Same      |
| **Buzzer**        | ✅ PWM               | ❓ Unknown        | ❓ To verify |
| **Vibrator**      | ✅ GPIO              | ❓ Unknown        | ❓ To verify |
| **Charging**      | USB                 | Qi Wireless      | ✅ Changed   |

## 🔧 Implementation Impact

### New Drivers Required:
1. **SHT40 Driver** - Temperature & Humidity sensor
2. **LPS22HH Driver** - Pressure sensor
3. **RGB LED Driver** - Status indication

### Modified Drivers:
1. **Flash Driver** - Update for STCC4-D-R3
2. **Battery/Charging** - Update for Qi charging

### Removed Code:
1. **LCD Display** - All ST7301 and UI code
2. **Display-related protocol commands**

### Protocol Changes Needed:
- Add commands for new sensor data (temperature, humidity, pressure)
- Add LED control commands
- Remove display-related commands

## 📝 Updated Implementation Plan

### Phase 2 (Hardware Drivers) - ADDITIONS:
- [ ] Port SHT40 sensor driver
- [ ] Port LPS22HH sensor driver  
- [ ] Implement RGB LED control
- [ ] Verify buzzer/vibrator presence

### Phase 3 (Sensor & Storage) - ADDITIONS:
- [ ] Integrate SHT40 readings
- [ ] Integrate LPS22HH readings
- [ ] Update data logging for new sensors
- [ ] Update protocol for new sensor data

### Phase 4 (BLE Protocol) - ADDITIONS:
- [ ] Add temperature/humidity data commands
- [ ] Add pressure data commands
- [ ] Add LED control commands
- [ ] Remove display-related commands

## ⚠️ Action Items

### Immediate:
1. **Verify Buzzer/Vibrator**: Check if present on other schematic sheets
2. **Get STCC4-D-R3 Datasheet**: For flash driver implementation
3. **Get SHT40 Datasheet**: For temperature/humidity sensor driver
4. **Get LPS22HH Datasheet**: For pressure sensor driver
5. **Complete Pin Mapping**: Extract exact GPIO pin numbers from schematic

### Short Term:
1. Implement new sensor drivers
2. Update data structures for additional sensor data
3. Update BLE protocol for new data types
4. Implement RGB LED control

### Long Term:
1. Integrate new sensor data into mobile app
2. Update factory tests for new sensors
3. Optimize power consumption with new sensors

## 📚 References

- **SHT40**: Sensirion SHT40 datasheet needed
- **LPS22HH**: STMicroelectronics LPS22HH datasheet needed
- **STCC4-D-R3**: Flash memory datasheet needed
- **CL-505S-X-SD-T**: RGB LED datasheet (if available)

---

**Status**: Initial Analysis Complete  
**Next Steps**: Verify missing information, obtain datasheets, begin driver implementation  
**Last Updated**: 2025-01-XX

