# SWD Connection Troubleshooting Guide

## Error: "Setting the debug port SELECT register failed"

This error means the J-Link cannot access the SWD debug port. Common causes:

### 1. SWD Disabled in Firmware (Most Likely)
The manufacturer's firmware may have disabled SWD for security.

**Solution**: Force SWD access during boot

### 2. Device is Protected/Locked
The device may have APPROTECT enabled.

**Solution**: Recovery sequence required

### 3. Hardware Connection Issue
SWD lines not properly connected.

**Solution**: Verify connections

## Step-by-Step Troubleshooting

### Step 1: Verify Hardware Connections

Check these connections on your JTAG connector J3:

| J3 Pin | Signal | nRF54L05 Pin | J-Link Pin | Test Point |
|--------|--------|--------------|------------|------------|
| 1-2 | SWDIO | P2.06 | Pin 1 | TP44 |
| 3-4 | SWCLK | P2.07 | Pin 9 | - |
| 9-10 | nRESET | P2.10 | Pin 15 | TP48 |
| GND | Ground | - | Pin 4/6 | Any GND |

**Verification**:
1. Use multimeter in continuity mode
2. Check: J3 pin 1-2 → TP44 (should beep)
3. Check: J3 pin 9-10 → TP48 (should beep)
4. Check: GND → Any ground pad (should beep)

### Step 2: Check Power Supply

**Required**: 3.0V (not 3.3V!)

1. Measure voltage on VDD pad or power rail
2. Should read ~3.0V (2.9V - 3.1V acceptable)
3. J-Link detected VTref=3.300V (slightly high but OK)

**If voltage is wrong**:
- Check nPM1100 configuration
- Verify power supply is correct

### Step 3: Force SWD Access (Recovery Sequence)

Since the board is working, SWD is likely disabled. Try this:

#### Method A: Power Cycle with Reset Held

1. **Power OFF** the board completely (remove battery/USB)
2. **Short TP48 to GND** (use J3 pin 10 or any GND pad)
3. **While holding TP48 to GND**, power ON the board
4. **Keep TP48 shorted** for 2-3 seconds
5. **Release TP48**
6. **Within 1 second**, try to connect:

```bash
cd /Users/nirajbhatt/airspot/Airspot_firmware/airspot/airspot_slim/application
west flash --runner jlink --device NRF54L05_M33
```

#### Method B: Use J-Link to Control Reset

```bash
cd /Users/nirajbhatt/airspot/Airspot_firmware/airspot/airspot_slim/application
JLinkExe
```

Then in J-Link Commander:
```
si 1
speed 1000
device NRF54L05_M33
r
h
sleep 500
connect
```

This uses J-Link to hold reset, then tries to connect.

### Step 4: Check if SWD Pins are Repurposed

The current firmware might be using SWD pins for other functions:
- P2.06 (SWDIO) - Check if used for LED or other function
- P2.07 (SWCLK) - Check if used for other function

**If pins are repurposed**: You may need to:
1. Power cycle with reset held
2. Connect immediately before firmware starts
3. Or use recovery mode

### Step 5: Try Recovery Mode

If the device is locked, try recovery:

```bash
nrfjprog --recover --log
```

Or with J-Link:
```bash
JLinkExe -CommandFile /dev/stdin << 'EOF'
si 1
speed 1000
device NRF54L05_M33
r
h
sleep 1000
r
g
exit
EOF
```

### Step 6: Verify SWD is Not Permanently Disabled

Some firmware permanently disables SWD. Check:

1. Try connecting immediately after power-on (before firmware starts)
2. If it connects during boot but not after, SWD is being disabled by firmware
3. Solution: Flash new firmware that doesn't disable SWD

## Quick Verification Checklist

- [ ] Power supply is 3.0V (measured with multimeter)
- [ ] SWDIO connection verified (J3 pin 1-2 → TP44)
- [ ] SWCLK connection verified (J3 pin 3-4 → P2.07)
- [ ] nRESET connection verified (J3 pin 9-10 → TP48)
- [ ] GND connection verified
- [ ] Connector is fully seated
- [ ] Tried power cycle with reset held
- [ ] Tried connecting immediately after power-on

## Alternative: Use UART Instead

If SWD cannot be enabled, you can:

1. **Use UART for communication**:
   - Connect to `/dev/tty.usbmodem0010577041031` or `/dev/tty.usbmodem0010577041033`
   - Baud rate: 115200
   - Check if firmware has UART commands

2. **Check if firmware supports OTA updates**:
   - Some firmware allows updates via BLE or UART
   - This might be easier than SWD

## If Nothing Works

If SWD is permanently disabled and cannot be recovered:

1. **Contact manufacturer** for:
   - Recovery firmware
   - Special recovery procedure
   - Unlock code/sequence

2. **Check if there's a recovery button sequence**:
   - Some boards have special button combinations
   - Check manufacturer documentation

3. **Consider if you need SWD**:
   - If board is working, you might be able to:
     - Use UART for debugging
     - Use BLE for updates
     - Develop without SWD initially

## Next Steps

1. **First**: Verify all hardware connections with multimeter
2. **Second**: Try power cycle with reset held
3. **Third**: Try connecting immediately after power-on
4. **Fourth**: Check if UART can be used instead

The fact that your board is working (LEDs, button) means hardware is fine. The issue is almost certainly that SWD is disabled in the manufacturer's firmware.

