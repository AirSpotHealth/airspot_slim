# Schematic Analysis - AirSpot Slim

**Schematic File**: AH-013-0001-SCH CO2 Monitor (Standard Build) PWA Schematics Rev1A.PDF  
**Revision**: 1A  
**Date**: 29-Oct-2025  
**Designer**: Genesys Electronics Design

## Schematic Overview

The schematic consists of 9 sheets:

1. **Top** - Main board overview
2. **Battery Protection** - Battery protection circuit
3. **Power** - Power management
4. **Wireless Power** - Qi charging circuit
5. **nRF54L05** - Main MCU connections
6. **nRF54L05 Peripherals** - Peripheral connections
7. **Sensors** - Sensor connections (CO2 sensor)
8. **User Interface** - Button, buzzer, vibrator
9. **Revision History** - Change log

## Key Components Identified

### Main MCU
- **nRF54L05** - Nordic Semiconductor MCU
  - ARM Cortex-M33 dual-core
  - Bluetooth 5.4
  - 1 MB Flash, 256 KB RAM

### Power Management
- **nPM (Nordic Power Management)** IC
  - Multiple interface pins: `NRF_NPM_54H` (pins 02, 11, 12, 13)
  - Low power mode control
  - Active mode indication

### Qi Wireless Charging
- **Qi Charging IC** (model to be determined)
  - Charge status: `NRF_QC_4CHG` (Pin 01)
  - Enable control: `NRF_QC_EN1` (Pin 05)
  - Overheat detection: `NRF_QC_4OH1` (if present)

### CO2 Sensor
- **Sensirion SCD4x** series
  - I2C interface
  - Signals: `NRF_CC_*` (multiple pins)
  - Address selection pin

### Flash Memory
- **SPI Flash** (likely ZB25D16)
  - MISO: `NRF_PLAIM_4M1U` (Pin 06)
  - Clock: `NRF_PLAIM_4PL_CLK` (Pin 07)
  - Additional signals on Pins 08, 09, 10

### Battery Monitoring
- **ADC** for battery voltage
  - Enable: `NRF_RAST1_ADC_EN`
  - Input: `NRF_RAST1_ADC`

### Low Power Sensor
- **Low Power Sensor** with interrupt
  - Interrupt pin: `NRF_LPS_INT` (Pin 03)

### Debug Interface
- **SWD** (Serial Wire Debug)
  - Multiple SWD clock pins: `NRF_5W_5WOLL`
  - Debug pin: `NRF_5WDRG`
  - Reset: `NRF_4RESET`

## Pin Assignments (Partial)

From the schematic pin table on Sheet 5:

| Pin | Signal | Function |
|-----|--------|-----------|
| 01 | `NRF_QC_4CHG` | Qi Charge Status |
| 02 | `NRF_NPM_54H` | nPM Host Interface |
| 03 | `NRF_LPS_INT` | Low Power Sensor Interrupt |
| 04 | `NRF_5W_ACL` | SWD/ACL |
| 05 | `NRF_QC_5OA` | Qi (to be determined) |
| 06 | `NRF_PLAIM_4M1U` | Flash MISO |
| 07 | `NRF_PLAIM_4PL_CLK` | Flash Clock |
| 08 | `NRF_PLAIM_4PL_MOG` | Flash Signal |
| 09 | `NRF_PLAIM_4PL_MOG` | Flash Signal |
| 10 | `NRF_PLAIM_4PL_MOG` | Flash Signal |
| 11 | `NRF_NPM_54H` | nPM Host Interface |
| 12 | `NRF_NPM_54H` | nPM Host Interface |
| 13 | `NRF_NPM_54H` | nPM Host Interface |
| 14-18 | `NRF_5W_5WOLL` | SWD Clock (multiple) |

## Power Management Notes

From the schematic notes:

### nRF54 Low Power Recommendations:
- MCS1 pins to be actively set to analog mode when not in use
- Receptors to be disabled when not in use
- DCDC regulator to be used
- External crystal to be used
- TX power of -6dBm recommended
- Port 1 should be disabled most of the time
- S2 to power GRTC
- SAADC on as little as possible

## Hardware Features

### Confirmed:
- ✅ nRF54L05 MCU
- ✅ Qi wireless charging
- ✅ CO2 sensor (SCD4x)
- ✅ SPI flash memory
- ✅ Battery monitoring (ADC)
- ✅ nPM power management IC
- ✅ Low power sensor
- ✅ SWD debug interface
- ✅ BLE antenna (chip antenna)

### To Be Determined:
- ❓ Button pin assignment
- ❓ Buzzer pin assignment (PWM?)
- ❓ Vibrator pin assignment
- ❓ I2C pins for CO2 sensor (SDA/SCL)
- ❓ Complete SPI flash pin mapping
- ❓ ADC channel for battery
- ❓ Qi charging IC model
- ❓ Status LEDs (if any)

## Schematic Sheets Reference

1. **Top** - Overall board layout
2. **Battery Protection** - Battery protection and charging circuit
3. **Power** - Main power distribution
4. **Wireless Power** - Qi charging receiver circuit
5. **nRF54L05** - MCU pin connections and power
6. **nRF54L05 Peripherals** - Peripheral device connections
7. **Sensors** - CO2 sensor and other sensors
8. **User Interface** - Button, buzzer, vibrator, LEDs (if any)
9. **Revision History** - Document revision tracking

## Next Steps for Firmware Development

1. **Extract Complete Pin Map**
   - Review all 9 sheets in detail
   - Create complete pin assignment table
   - Document all GPIO functions

2. **Identify Qi Charging IC**
   - Determine IC model from Sheet 4 (Wireless Power)
   - Understand charge detection method
   - Document register interface (if I2C)

3. **Document I2C Interface**
   - Identify SDA/SCL pins for CO2 sensor
   - Document I2C address
   - Document power control pin

4. **Document SPI Interface**
   - Complete SPI flash pin mapping
   - Identify CS, MOSI, MISO, CLK pins
   - Document flash chip model

5. **Document User Interface**
   - Button GPIO pin and configuration
   - Buzzer pin (PWM or GPIO)
   - Vibrator GPIO pin
   - LED pins (if present)

6. **Document Power Management**
   - nPM IC interface (I2C or GPIO)
   - Battery ADC channel
   - Power sequencing requirements

## Questions for Hardware Team

1. What is the Qi charging IC model? (from Sheet 4)
2. What are the complete I2C pin assignments? (SDA/SCL)
3. What are the complete SPI pin assignments? (CS, MOSI, MISO, CLK)
4. What is the ADC channel for battery voltage?
5. What are the button, buzzer, and vibrator pin assignments?
6. Are there status LEDs? If so, what are the pin assignments?
7. What is the low power sensor? (LPS_INT pin 03)
8. What is the nPM IC interface? (I2C address, register map)

---

**Status**: Initial Analysis - Need Detailed Review  
**Last Updated**: 2025-01-XX

