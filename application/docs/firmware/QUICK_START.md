# Quick Start Guide - AirSpot Slim Baseline

This is a **minimal working baseline** with BLE, DFU, button, and LED support.

## What's Included

✅ **BLE** - Advertising and basic connection  
✅ **DFU** - MCUboot for OTA updates  
✅ **Button** - Press/release/long-press detection  
✅ **LEDs** - 4 RGB LEDs with basic control  
✅ **Logging** - Serial UART logging  

## Prerequisites

1. **nRF Connect SDK** installed
2. **VS Code** with "nRF Connect for VS Code" extension (recommended)
3. **J-Link** software installed
4. **Hardware**: nRF54L15DK connected to custom board via JTAG

## Build & Flash (VS Code)

1. Open `slim/` folder in VS Code
2. Click "Build" in nRF Connect extension
3. Connect hardware
4. Click "Flash" in nRF Connect extension

## Build & Flash (Command Line)

```bash
cd slim
west build -b nrf54l05dk_nrf54l05_cpuapp
west flash
```

## Verify It Works

1. **LEDs**: Should blink in sequence on startup
2. **Serial Logs**: Connect UART (115200 baud) - should see:
   ```
   AirSpot Slim Firmware Starting...
   Version: 0.1.0 (Minimal Baseline)
   LED initialized
   Button initialized
   BLE initialized and advertising
   System ready
   ```
3. **BLE**: Scan with nRF Connect app - should see "AirSpot Slim"
4. **Button**: Press button - check serial logs for events

## Next Steps

Once baseline is working:

1. **Update Pin Assignments**: Edit `boards/nrf54l05dk_nrf54l05_cpuapp.overlay` with actual custom board pins
2. **Add Sensors**: Start with CO2 sensor driver
3. **Add Protocol**: Implement BLE UART service
4. **Add Storage**: Implement flash storage

## Troubleshooting

### No Serial Output
- Check UART connection (TX: Pin 6, RX: Pin 8)
- Verify baud rate: 115200
- Check device tree UART configuration

### LEDs Not Working
- Verify device tree LED pin assignments
- Check if LEDs are defined in overlay file
- Use `led0`, `led1`, etc. aliases

### Button Not Working
- Verify device tree button pin assignment
- Check GPIO pull-up configuration
- Use `sw0` alias in device tree

### BLE Not Advertising
- Check BLE stack initialization in logs
- Verify BLE is enabled in `prj.conf`
- Check antenna connection

### DFU Not Working
- Verify MCUboot is enabled in `prj.conf`
- Check bootloader is flashed first
- Use serial recovery mode for testing

## Project Structure

```
slim/
├── CMakeLists.txt          # Build configuration
├── prj.conf                 # Zephyr Kconfig settings
├── west.yml                 # West manifest (optional)
├── boards/
│   └── *.overlay           # Device tree overlay
├── src/
│   ├── main.c              # Application entry
│   ├── ble_app.c/h         # BLE implementation
│   ├── button.c/h           # Button handling
│   └── led.c/h              # LED control
└── include/                 # Headers (if needed)
```

## Configuration

### BLE Settings
Edit `prj.conf`:
- `CONFIG_BT_DEVICE_NAME` - Change device name
- `CONFIG_BT_MAX_CONN` - Max connections

### Pin Assignments
Edit `boards/nrf54l05dk_nrf54l05_cpuapp.overlay`:
- Update LED pins
- Update button pin
- Update UART pins (if needed)

## DFU Testing

### Serial Recovery
1. Hold button during boot
2. Device enters recovery mode
3. Use `mcumgr` to upload firmware:
   ```bash
   mcumgr image upload build/zephyr/zephyr.signed.bin
   ```

### BLE DFU (Future)
- Will be implemented with BLE protocol
- Use nRF Connect app or custom implementation

---

**Status**: Minimal Baseline - Ready for Testing  
**Version**: 0.1.0

