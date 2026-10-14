# Complete Pin Assignments - AirSpot Slim

> **Historical Rev1A reference.** For current Slim Rev3A wiring, use the
> [production board reference](PRODUCTION_BOARD.md). The pin assignments here
> must not be used to connect the current board.

**Source**: AH-013-0001-SCH CO2 Monitor (Standard Build) PWA Schematics Rev1A, Sheet 5 (nRF54L05)  
**Verified**: Based on working firmware configuration (application)  
**Last Updated**: 2026-01-19

## User Interface Pins

### LEDs (RGB LED Control)
| Signal         | Pin       | GPIO Port | Alternate Functions                             | Notes             |
| -------------- | --------- | --------- | ----------------------------------------------- | ----------------- |
| `NRF_UI_LED_1` | **P2.09** | GPIO2     | FLPR.9/TRACEDATA[2]/SPIM_SDO/SPIS_SDO/UARTE_TXD | RGB LED channel 1 |
| `NRF_UI_LED_2` | **P2.08** | GPIO2     | FLPR.8/TRACEDATA[1]/SPIM_SDO/SPIS_SDO/UARTE_TXD | RGB LED channel 2 |
| `NRF_UI_LED_3` | **P2.06** | GPIO2     | FLPR.6/SPIM_SCK/SPIS_SCK/TRACECLK               | RGB LED channel 3 |

**LED Details**:
- **Model**: CL-505S-X-SD-T (RGB LED)
- **Drive Current**: 5 mA (typical)
- **Pin Voltage**: 0.5V to VSYS
- **Forward Voltages**:
  - Red: 1.85V @ 1mA, 4.2V
  - Green: 2.70V @ 1mA, 4.2V
  - Blue: 2.75V @ 1mA, 4.2V

**Important Note**: LEDs are on **GPIO2** (P2.09, P2.08, P2.06), NOT GPIO1. This avoids conflicts with SPI flash control pins on GPIO1.

### Button
| Signal          | Pin       | GPIO Port | Alternate Functions                            | Notes                             |
| --------------- | --------- | --------- | ---------------------------------------------- | --------------------------------- |
| `NRF_UI_nBTN_1` | **P2.10** | GPIO2     | FLPR.10/TRACEDATA[3]/SPIM_CS/SPIS_CS/UARTE_RTS | Active high, debounce in firmware |

**Button Details**:
- Active high (pressed = HIGH)
- Debounce handled in firmware
- Supports press, release, and long-press (3+ seconds) events
- Uses GPIO interrupt for event detection

## I2C Bus (Shared by Sensors)

| Signal        | Pin       | GPIO Port | Alternate Functions | Notes             |
| ------------- | --------- | --------- | ------------------- | ----------------- |
| `NRF_I2C_SCL` | **P0.03** | GPIO0     | GRTC_PWM            | I2C Clock (i2c30) |
| `NRF_I2C_SDA` | **P0.04** | GPIO0     | GRTC_CLKOUT32K      | I2C Data (i2c30)  |

**I2C Configuration**:
- **Instance**: i2c30
- **Frequency**: 400 kHz (I2C_BITRATE_FAST)
- **Pull-ups**: Internal pull-ups enabled
- **Sleep mode**: Low-power enabled

**Sensors on I2C Bus**:
- **CO2 Sensor (SCD4x)**: Address `0x64` (7-bit)
- **Temp/Humidity (SHT40)**: Address `0x44` (7-bit)
- **Pressure (LPS22HH)**: Address `0x5D` (7-bit)

## SPI Flash Memory

| Signal               | Pin       | GPIO Port | Alternate Functions | Notes                                            |
| -------------------- | --------- | --------- | ------------------- | ------------------------------------------------ |
| `NRF_FLASH_SPI_CLK`  | **P2.01** | GPIO2     | FLPR.1/QSPI_SCK     | SPI Clock (spi00)                                |
| `NRF_FLASH_SPI_MOSI` | **P2.02** | GPIO2     | FLPR.2/QSPI_D1      | SPI MOSI (spi00)                                 |
| `NRF_FLASH_SPI_MISO` | **P2.04** | GPIO2     | FLPR.4/QSPI_D3      | SPI MISO (spi00)                                 |
| `NRF_FLASH_nCS`      | **P2.05** | GPIO2     | FLPR.5/QSPI_D2      | Chip Select (active low)                         |
| `NRF_FLASH_nWP`      | **P1.13** | GPIO1     | RADIO4/AIN6         | Write Protect (active low, HIGH = not protected) |
| `NRF_FLASH_nHLD`     | **P1.11** | GPIO1     | ASO3/RADIO2/AIN4    | Hold (active low, HIGH = not held)               |

**Flash Chip**: AT25XE161D (JEDEC ID: `1F 46 0C`, 2MB)

**SPI Configuration**:
- **Instance**: spi00
- **Frequency**: 2 MHz (spi-max-frequency = 2000000)
- **Mode**: CPOL=0, CPHA=0 (SPI_MODE_0)
- **Drive Mode**: S0S1 (standard drive)
- **CS**: GPIO-controlled, active low

**Flash Control Pins**:
- **WP (Write Protect)**: Must be HIGH for normal operation (configured via gpio-hog)
- **HOLD**: Must be HIGH for normal operation (configured via gpio-hog)
- Both pins are configured early at boot (PRE_KERNEL_1) to ensure flash is ready

**Important Note**: WP and HOLD pins are on GPIO1, which is why LEDs were moved to GPIO2 to avoid conflicts.

## UART (Debug Console)

| Signal         | Pin       | GPIO Port | Alternate Functions | Notes            |
| -------------- | --------- | --------- | ------------------- | ---------------- |
| `NRF_DEBUG_TX` | **P1.02** | GPIO1     | NFC1                | UART TX (uart20) |
| `NRF_DEBUG_RX` | **P1.03** | GPIO1     | NFC2                | UART RX (uart20) |

**UART Configuration**:
- **Instance**: uart20
- **Baud Rate**: 115200
- **NFC Pins**: Must disable NFC and enable GPIO mode via UICR (`nfct-pins-as-gpios`)

## Battery & Power

| Signal            | Pin       | GPIO Port | Alternate Functions | Status     | Notes                         |
| ----------------- | --------- | --------- | ------------------- | ---------- | ----------------------------- |
| `NRF_BATT_ADC`    | **P1.05** | GPIO1     | ASI0/RADIO6/AIN1    | ✅ Verified | Battery voltage ADC           |
| `NRF_BATT_ADC_EN` | **P1.04** | GPIO1     | ASO0/AIN0           | ✅ Verified | ADC enable control            |
| `NRF_QI_nCHG`     | **P0.00** | GPIO0     | -                   | 🔶 Untested | Qi charge status (active low) |
| `NRF_QI_EN1`      | **P1.00** | GPIO1     | XL1                 | -          | Qi enable 1                   |
| `NRF_QI_EN2`      | **P1.01** | GPIO1     | XL2                 | -          | Qi enable 2                   |

**Battery ADC Configuration (Verified 2026-01-20):**
- 12-bit resolution with 8x oversampling
- ADC gain: 1/4, internal 0.6V reference
- Voltage divider: 0.6x scaling factor
- Accuracy: ~97-98% across 3.0V-4.2V range
- ADC enable pin controls MOSFET for power saving

## Sensors

| Signal        | Pin       | GPIO Port | Alternate Functions | Notes                               |
| ------------- | --------- | --------- | ------------------- | ----------------------------------- |
| `NRF_LPS_INT` | **P0.03** | GPIO0     | GRTC_PWM            | Pressure sensor interrupt (LPS22HH) |

**Note**: LPS interrupt pin conflicts with I2C SCL. Verify if interrupt is actually used or if polling is preferred.

## Debug & Programming (SWD)

| Signal       | Pin       | GPIO Port | Alternate Functions                             | Notes        |
| ------------ | --------- | --------- | ----------------------------------------------- | ------------ |
| `NRF_SWDIO`  | **P2.06** | GPIO2     | FLPR.6/SPIM_SCK/SPIS_SCK/TRACECLK               | SWD Data I/O |
| `NRF_SWCLK`  | **P2.07** | GPIO2     | FLPR.7/TRACEDATA[0]/SWO/SPIM_DCX/UARTE_RXD      | SWD Clock    |
| `NRF_SWO`    | **P2.08** | GPIO2     | FLPR.8/TRACEDATA[1]/SPIM_SDO/SPIS_SDO/UARTE_TXD | SWO Trace    |
| `NRF_nRESET` | **P2.10** | GPIO2     | FLPR.10/TRACEDATA[3]/SPIM_CS/SPIS_CS/UARTE_RTS  | Reset        |

**Note**: SWD pins (P2.06, P2.07, P2.08) conflict with LED pins. This is expected - SWD is only used during development/debugging.

## Power Management (nPM)

| Signal           | Pin       | GPIO Port | Alternate Functions | Notes                |
| ---------------- | --------- | --------- | ------------------- | -------------------- |
| `NRF_NPM_Mode`   | **P0.01** | GPIO0     | -                   | nPM mode control     |
| `NRF_NPM_ERR`    | **P0.02** | GPIO0     | -                   | nPM error            |
| `NRF_NPM_CHG`    | **P0.03** | GPIO0     | GRTC_PWM            | nPM charge status    |
| `NRF_NPM_SHPACT` | **P0.04** | GPIO0     | GRTC_CLKOUT32K      | nPM ship/hold active |

**Note**: nPM pins (P0.03, P0.04) conflict with I2C pins. Verify actual usage.

## Clock & Crystal

| Signal      | Pin       | GPIO Port | Alternate Functions | Notes             |
| ----------- | --------- | --------- | ------------------- | ----------------- |
| `NRF_XAL_1` | **P1.00** | GPIO1     | XL1                 | 32.768kHz crystal |
| `NRF_XAL_2` | **P1.01** | GPIO1     | XL2                 | 32.768kHz crystal |

## GPIO Port Summary

### GPIO0 (Port 0)
- P0.00: Qi charge status
- P0.01: nPM mode
- P0.02: nPM error
- P0.03: I2C SCL / nPM CHG / LPS INT (conflicts)
- P0.04: I2C SDA / nPM SHPACT (conflicts)

### GPIO1 (Port 1)
- P1.00: Qi enable 1 / XL1 (crystal)
- P1.01: Qi enable 2 / XL2 (crystal)
- P1.02: UART TX / NFC1
- P1.03: UART RX / NFC2
- P1.04: ADC enable
- P1.05: Battery ADC
- P2.10: Button (active high) / nRESET (conflicts)
- P1.11: Flash HOLD (active low, HIGH = not held)
- P1.13: Flash WP (active low, HIGH = not protected)

**Important**: GPIO1 contains SPI flash control pins (WP, HOLD). LEDs were moved to GPIO2 to avoid conflicts.

### GPIO2 (Port 2)
- P2.00: SPI MOSI (flash)
- P2.01: SPI CLK (flash)
- P2.02: SPI MOSI (flash) - Note: overlay shows P2.02 for MOSI
- P2.04: SPI MISO (flash)
- P2.05: SPI CS (flash)
- P2.06: LED 3 / SWDIO (conflicts)
- P2.07: SWCLK
- P2.08: LED 2 / SWO (conflicts)
- P2.09: LED 1
- P2.10: Button (active high) / nRESET (conflicts)

## Zephyr Device Tree Usage

### LEDs
```dts
leds: leds {
    compatible = "gpio-leds";
    led0: led_0 {
        gpios = <&gpio2 9 GPIO_ACTIVE_HIGH>;  /* P2.09 */
        label = "LED 1";
    };
    led1: led_1 {
        gpios = <&gpio2 8 GPIO_ACTIVE_HIGH>;  /* P2.08 */
        label = "LED 2";
    };
    led2: led_2 {
        gpios = <&gpio2 6 GPIO_ACTIVE_HIGH>;  /* P2.06 */
        label = "LED 3";
    };
};
```

### Button
```dts
buttons {
    compatible = "gpio-keys";
    sw0: button_0 {
        gpios = <&gpio2 10 GPIO_ACTIVE_HIGH>;  /* P2.10 */
        label = "Button 0";
        zephyr,code = <INPUT_KEY_0>;
    };
};
```

### I2C
```dts
&i2c30 {
    status = "okay";
    pinctrl-0 = <&i2c30_default>;
    clock-frequency = <I2C_BITRATE_FAST>;
};

&pinctrl {
    i2c30_default: i2c30_default {
        group1 {
            psels = <NRF_PSEL(TWIM_SDA, 0, 4)>,  /* P0.04 SDA */
                   <NRF_PSEL(TWIM_SCL, 0, 3)>;   /* P0.03 SCL */
            bias-pull-up;
        };
    };
};
```

### SPI Flash
```dts
&spi00 {
    status = "okay";
    pinctrl-0 = <&spi00_custom>;
    cs-gpios = <&gpio2 5 GPIO_ACTIVE_LOW>;  /* P2.05 */
    
    at25xe161d: at25xe161d@0 {
        compatible = "jedec,spi-nor";
        reg = <0>;
        spi-max-frequency = <2000000>;
        jedec-id = [1f 46 0c];
        size = <DT_SIZE_M(2)>;
    };
};

&pinctrl {
    spi00_custom: spi00_custom {
        group1 {
            psels = <NRF_PSEL(SPIM_MISO, 2, 4)>;  /* P2.04 */
        };
        group2 {
            psels = <NRF_PSEL(SPIM_SCK, 2, 1)>;   /* P2.01 */
        };
        group3 {
            psels = <NRF_PSEL(SPIM_MOSI, 2, 2)>;  /* P2.02 */
        };
    };
};

&gpio1 {
    flash_wp: flash_wp {
        gpio-hog;
        gpios = <13 GPIO_ACTIVE_HIGH>;  /* P1.13 */
        output-high;
    };
    flash_hold: flash_hold {
        gpio-hog;
        gpios = <11 GPIO_ACTIVE_HIGH>;  /* P1.11 */
        output-high;
    };
};
```

### UART
```dts
&uart20 {
    status = "okay";
    current-speed = <115200>;
    pinctrl-0 = <&uart20_custom>;
};

&pinctrl {
    uart20_custom: uart20_custom {
        group1 {
            psels = <NRF_PSEL(UART_TX, 1, 2)>,  /* P1.02 */
                   <NRF_PSEL(UART_RX, 1, 3)>;   /* P1.03 */
        };
    };
};

&uicr {
    nfct-pins-as-gpios;  /* Required to use NFC pins as GPIOs */
};
```

## Important Notes

1. **LED Pin Conflict Resolution**: LEDs were moved from GPIO1 (P1.06-08) to GPIO2 (P2.09, P2.08, P2.06) to avoid conflicts with SPI flash control pins (WP, HOLD) on GPIO1.

2. **SPI Flash Control**: WP and HOLD pins must be HIGH for normal flash operation. They are configured via `gpio-hog` in device tree to ensure they're set early at boot.

3. **NFC Pins**: UART uses NFC pins (P1.02, P1.03). Must set `nfct-pins-as-gpios` in UICR to use them as GPIOs.

4. **Pin Conflicts**: Some pins have multiple functions (e.g., SWD pins vs LED pins). This is expected - SWD is only used during development.

5. **GPIO Numbering**: nRF54L05 uses port-based pin numbering. In Zephyr device tree:
   - `gpio0` = Port 0 (P0.x)
   - `gpio1` = Port 1 (P1.x)
   - `gpio2` = Port 2 (P2.x)

## References

- **Schematic**: AH-013-0001-SCH CO2 Monitor (Standard Build) PWA Schematics Rev1A
- **Working Configuration**: `application/boards/nrf54l15dk_nrf54l05_cpuapp.overlay`
- **Flash Test**: `application/flash_test/boards/nrf54l15dk_nrf54l05_cpuapp.overlay`

---

**Status**: Verified and up-to-date (Battery ADC verified 2026-01-20)  
**Last Updated**: 2026-01-20
