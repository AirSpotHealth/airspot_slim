# Using nRF54L15DK to Debug External Custom Board

## Your Setup
- ✅ nRF54L15DK connected via USB (onboard J-Link)
- ✅ Custom board connected to DK's "debug out" connector (0.5mm)
- ✅ Custom board powered separately (Power Profiler, 3.0V)
- ✅ VTref detected (3.300V) - J-Link sees your custom board's power

## The Issue
The DK's onboard J-Link should automatically route to the external target when connected. However, SWD still can't connect, which means:

1. **SWD is disabled in firmware** (most likely)
2. **Connections might need verification**
3. **DK might need specific configuration**

## Solution: Try Recovery Sequence

Since your custom board is working (LEDs, button), the hardware is fine. The firmware has disabled SWD.

### Step 1: Power OFF your custom board

### Step 2: Short TP48 (reset) to GND on your custom board
- Use a wire to connect TP48 to any ground pad
- Keep it shorted

### Step 3: Power ON your custom board (while TP48 is shorted to GND)

### Step 4: Keep TP48 shorted for 2-3 seconds, then release

### Step 5: Immediately (within 0.5 seconds) try to connect:

```bash
cd /Users/nirajbhatt/airspot/Airspot_firmware/airspot/airspot_slim/application
JLinkExe
```

Then type:
```
si 1
speed 1000
device NRF54L05_M33
r
h
connect
```

If it connects, type:
```
loadfile build/merged.hex
r
g
exit
```

## Alternative: Check DK Configuration

Some DKs have jumpers or switches to select internal vs external target. Check your nRF54L15DK:
- Look for jumpers labeled "DEBUG" or "TARGET"
- Check if there's a switch to select internal/external
- Refer to nRF54L15DK user guide

## Why Serial 1057704103?

That's the **J-Link's serial number** (the DK's onboard debugger), NOT the target device. The target device is **NRF54L05_M33** (your custom board's MCU).

The J-Link is working fine - it's detecting power (VTref=3.300V). The problem is that SWD on your custom board is disabled by firmware.

## Next Steps

1. Try the recovery sequence above (power cycle with reset held)
2. If that doesn't work, contact your manufacturer for their flashing procedure
3. Check if there's a way to enable SWD via button sequence or UART command

The hardware setup is correct - the issue is firmware disabling SWD.

