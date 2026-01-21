# Memory Usage Tracking

Track firmware memory usage across builds to monitor growth and ensure we stay within limits.

## Current Limits

| Region                   | Chip     | Available | Notes                  |
| ------------------------ | -------- | --------- | ---------------------- |
| **Flash (Primary Slot)** | nRF54L05 | 280KB     | MCUboot primary slot   |
| **RAM**                  | nRF54L05 | 96KB      | Total SRAM             |
| **Flash (Full)**         | nRF54L10 | ~900KB    | For future mesh builds |

## Build History

| Date       | Version | Phase   | Flash Used         | RAM Used          | Notes                           |
| ---------- | ------- | ------- | ------------------ | ----------------- | ------------------------------- |
| 2026-01-21 | 0.9.0   | 9 - DFU | 268,220 B (93.72%) | 70,688 B (71.91%) | MCUboot + MCUmgr + UART logging |

## Breakdown by Feature (Approximate)

| Feature                         | Flash Impact | RAM Impact | Notes                 |
| ------------------------------- | ------------ | ---------- | --------------------- |
| Base (Zephyr + BLE)             | ~120KB       | ~40KB      | Core system           |
| Sensors (SHT40, LPS22HH, STCC4) | ~15KB        | ~2KB       | I2C drivers           |
| SPI Flash + Data Log            | ~12KB        | ~4KB       | External flash driver |
| BLE NUS + Protocol              | ~25KB        | ~8KB       | App protocol handler  |
| MCUmgr (DFU)                    | ~30KB        | ~6KB       | OTA update support    |
| UART Logging                    | ~40KB        | ~8KB       | Debug output          |
| User Settings (NVS)             | ~8KB         | ~2KB       | Persistent storage    |

## Size Optimization Tips

When approaching limits:

1. **Disable UART logging** (`CONFIG_LOG=n`) - Saves ~40KB flash
2. **Reduce log level** (`CONFIG_LOG_DEFAULT_LEVEL=1`) - Saves ~10-20KB
3. **Disable unused BLE features** - Check `CONFIG_BT_*` options
4. **Use LTO** (`CONFIG_LTO=y`) - Can save 5-15%
5. **Optimize for size** (`CONFIG_SIZE_OPTIMIZATIONS=y`)

## Commands

Check current build size:

```bash
# After building
cat build/application/zephyr/zephyr.map | grep "^Memory region"
# Or
arm-none-eabi-size build/application/zephyr/zephyr.elf
```

Detailed size breakdown:

```bash
# By section
arm-none-eabi-size -A build/application/zephyr/zephyr.elf

# By symbol (largest first)
arm-none-eabi-nm --size-sort -r build/application/zephyr/zephyr.elf | head -50
```
