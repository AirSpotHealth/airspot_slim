# STCC4 Slim Hardware Reference

Complete hardware component reference for the AirSpot STCC4 Slim firmware.

## Sensors

### Sensirion STCC4 - CO2 Sensor

| Property                      | Value            |
| ----------------------------- | ---------------- |
| **Part Number**               | STCC4            |
| **Interface**                 | I2C              |
| **Address**                   | 0x64             |
| **Measurement Range**         | 400 - 5000 ppm   |
| **Accuracy**                  | ±(100 ppm + 10%) |
| **Supply Voltage**            | 2.7V - 5.5V      |
| **Current (continuous)**      | 950 µA           |
| **Current (single-shot 10s)** | 100 µA           |

**Important**: Requires continuous temperature/humidity input from SHT40 for compensation.

**Datasheet**: https://sensirion.com/products/catalog/STCC4

---

### Sensirion SHT40 - Temperature & Humidity

| Property                 | Value                |
| ------------------------ | -------------------- |
| **Part Number**          | SHT40-AD1B-R2        |
| **Interface**            | I2C (Fast Mode Plus) |
| **Address**              | 0x44                 |
| **Temperature Range**    | -40°C to +125°C      |
| **Temperature Accuracy** | ±0.2°C (typical)     |
| **Humidity Range**       | 0 - 100 %RH          |
| **Humidity Accuracy**    | ±1.8 %RH (25-75 %RH) |
| **Supply Voltage**       | 1.08V - 3.6V         |
| **Current (avg @ 1Hz)**  | 0.4 µA               |

**Datasheet**: https://sensirion.com/media/documents/33FD6951/662A593A/Datasheet_SHT4x.pdf

---

### ST LPS22HH - Pressure Sensor

| Property              | Value               |
| --------------------- | ------------------- |
| **Part Number**       | LPS22HHTR           |
| **Interface**         | I2C / SPI / I3C     |
| **Address**           | 0x5D                |
| **Pressure Range**    | 260 - 1260 hPa      |
| **Pressure Accuracy** | ±0.5 hPa            |
| **Noise**             | 0.65 Pa             |
| **ODR**               | 1 - 200 Hz          |
| **Supply Voltage**    | 1.7V - 3.6V         |
| **Current**           | 4 µA                |
| **Interrupt Pin**     | P1.04 (NRF_LPS_INT) |

**Datasheet**: https://www.st.com/resource/en/datasheet/lps22hh.pdf

---

## Memory

### Renesas AT25XE161D - SPI Flash

| Property              | Value               |
| --------------------- | ------------------- |
| **Part Number**       | AT25XE161D-MAHN-T   |
| **Interface**         | SPI (up to 104 MHz) |
| **Capacity**          | 16 Mbit (2 MB)      |
| **Page Size**         | 256 bytes           |
| **Sector Size**       | 4 KB                |
| **Block Size**        | 32/64 KB            |
| **Supply Voltage**    | 1.65V - 3.6V        |
| **Current (active)**  | 4 mA                |
| **Current (standby)** | 5 µA                |

**Pins**:

- CLK: P2.01
- MOSI: P2.00
- MISO: P2.02
- CS: P1.14
- WP: P1.13
- HOLD: P1.11

**Datasheet**: https://www.renesas.com/us/en/document/dst/at25xe161d-datasheet

---

## Power

### Battery ADC

| Property       | Value         |
| -------------- | ------------- |
| **ADC Pin**    | P1.05 (AIN1)  |
| **Enable Pin** | P1.04         |
| **Resolution** | 12-bit        |
| **Reference**  | Internal 0.6V |

### Qi Wireless Charging

| Property       | Value              |
| -------------- | ------------------ |
| **Status Pin** | P0.00 (active low) |
| **Enable 1**   | P1.00              |
| **Enable 2**   | P1.01              |

---

## User Interface

### RGB LED (CL-505S-X-SD-T)

| Channel | Pin   | Forward Voltage |
| ------- | ----- | --------------- |
| Red     | P1.06 | 1.85V @ 1mA     |
| Green   | P1.07 | 2.70V @ 1mA     |
| Blue    | P1.08 | 2.75V @ 1mA     |

### Button

| Property   | Value                  |
| ---------- | ---------------------- |
| **Pin**    | P1.09                  |
| **Active** | Low (pull-up required) |

---

## I2C Bus Configuration

All sensors share the same I2C bus:

| Signal | Pin   |
| ------ | ----- |
| SCL    | P2.03 |
| SDA    | P2.04 |

**Device Addresses**:

- STCC4 (CO2): 0x64
- SHT40 (Temp/Humidity): 0x44
- LPS22HH (Pressure): 0x5D

---

## Quick Links

| Component  | Manufacturer       | Datasheet                                                                           |
| ---------- | ------------------ | ----------------------------------------------------------------------------------- |
| STCC4      | Sensirion          | [Link](https://sensirion.com/products/catalog/STCC4)                                |
| SHT40      | Sensirion          | [Link](https://sensirion.com/media/documents/33FD6951/662A593A/Datasheet_SHT4x.pdf) |
| LPS22HH    | STMicroelectronics | [Link](https://www.st.com/resource/en/datasheet/lps22hh.pdf)                        |
| AT25XE161D | Renesas            | [Link](https://www.renesas.com/us/en/document/dst/at25xe161d-datasheet)             |
| nRF54L05   | Nordic             | [Link](https://www.nordicsemi.com/Products/nRF54L05)                                |

---

**Last Updated**: 2026-01-10
