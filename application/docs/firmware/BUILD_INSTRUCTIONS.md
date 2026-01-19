# Building AirSpot Slim Minimal Baseline

## Prerequisites

1. **Install nRF Connect SDK**
   - Follow: https://developer.nordicsemi.com/nRF_Connect_SDK/doc/latest/nrf/getting_started.html
   - Recommended: Use nRF Connect for VS Code extension

2. **Install West Tool**
   - Included with nRF Connect SDK installation

3. **Install J-Link Software**
   - Download from: https://www.segger.com/downloads/jlink/

## Building

### Using VS Code (Recommended)

1. Open VS Code
2. Install "nRF Connect for VS Code" extension
3. Open the `slim/` folder
4. Use the nRF Connect extension to build and flash

### Using Command Line

```bash
# Navigate to project directory
cd slim

# Initialize west workspace (first time only)
west init -l .

# Update west workspace
west update

# Build the project
west build -b nrf54l05dk_nrf54l05_cpuapp

# Flash to device
west flash
```

## Flashing

### Using nRF Connect Programmer

1. Connect nRF54L15DK via USB
2. Connect custom board via JTAG (0.5mm connector)
3. Open nRF Connect Programmer
4. Select device
5. Load hex file: `build/zephyr/zephyr.hex`
6. Click "Write"

### Using Command Line

```bash
# Flash using west
west flash

# Or using nrfjprog directly
nrfjprog --program build/zephyr/zephyr.hex --chiperase --verify --reset
```

## DFU (Device Firmware Update)

### Setting up MCUboot

MCUboot is configured in `prj.conf`. To enable DFU:

1. **Serial Recovery Mode**:
   - Hold button during boot to enter recovery
   - Use `mcumgr` tool to upload new firmware

2. **BLE DFU** (if enabled):
   - Use nRF Connect mobile app
   - Or custom DFU implementation

### Building DFU Package

```bash
# Build with MCUboot
west build -b nrf54l05dk_nrf54l05_cpuapp -- -DCONFIG_BOOTLOADER_MCUBOOT=y

# Generate DFU package (requires mcumgr)
mcumgr image upload build/zephyr/zephyr.signed.bin
```

## Debugging

### Using VS Code

1. Set up launch configuration for nRF Connect
2. Set breakpoints
3. Start debugging session

### Using J-Link GDB

```bash
# Start GDB server
JLinkGDBServer -device nRF54L05 -if SWD -port 2331

# In another terminal, connect GDB
arm-none-eabi-gdb build/zephyr/zephyr.elf
(gdb) target remote localhost:2331
(gdb) monitor reset
(gdb) load
(gdb) continue
```

## Testing

### Basic Functionality

1. **LED Test**: LEDs should blink on startup
2. **Button Test**: Press button, check logs
3. **BLE Test**: 
   - Device should advertise as "AirSpot Slim"
   - Connect with nRF Connect app
   - Check connection status

### Serial Logging

Connect UART to see logs:
- Baud rate: 115200
- TX: Pin 6 (configurable in device tree)
- RX: Pin 8 (configurable in device tree)

## Troubleshooting

### Build Errors

- **West not found**: Add west to PATH or use VS Code extension
- **SDK not found**: Set `ZEPHYR_BASE` environment variable
- **Device tree errors**: Check overlay file syntax

### Flash Errors

- **Device not found**: Check JTAG connection
- **Permission denied**: Run with sudo or add udev rules
- **Wrong device**: Verify nRF54L05 is selected

### Runtime Issues

- **No logs**: Check UART connection and baud rate
- **LEDs not working**: Verify device tree pin assignments
- **Button not working**: Check GPIO configuration
- **BLE not advertising**: Check BLE stack initialization

## Next Steps

Once baseline is working:

1. Add CO2 sensor driver
2. Add flash storage
3. Add BLE protocol implementation
4. Add power management
5. Add factory testing

---

**Last Updated**: 2025-01-XX

