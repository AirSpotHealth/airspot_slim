# Boot-Time Flash Procedure

## The Problem
SWD is disabled by the firmware after boot. We need to connect **during boot** before firmware disables it.

## Solution: Catch Device During Boot

### Method 1: Power Cycle + Immediate Connect

1. **Power OFF** the board completely
2. **Have the flash command ready** (don't run it yet)
3. **Power ON** the board
4. **Within 0.5 seconds** of power-on, run:

```bash
cd /Users/nirajbhatt/airspot/Airspot_firmware/airspot/airspot_slim/application
west flash --runner jlink --device NRF54L05_M33
```

The key is timing - you need to connect before the firmware starts and disables SWD.

### Method 2: Use J-Link with Auto-Connect

Create a script that keeps trying to connect:

```bash
cd /Users/nirajbhatt/airspot/Airspot_firmware/airspot/airspot_slim/application

# Power OFF board, then run this:
while true; do
  JLinkExe -CommandFile /dev/stdin << 'EOF' 2>&1 | grep -q "Found\|Connected" && break
si 1
speed 1000
device NRF54L05_M33
connect
exit
EOF
  sleep 0.1
done

# If connected, flash:
west flash --runner jlink --device NRF54L05_M33
```

### Method 3: Hold Reset During Power-On

1. **Power OFF** board
2. **Short TP48 to GND** (hold reset)
3. **Power ON** while holding reset
4. **Keep holding reset** and run:

```bash
cd /Users/nirajbhatt/airspot/Airspot_firmware/airspot/airspot_slim/application
west flash --runner jlink --device NRF54L05_M33
```

5. **Release reset** after flash starts

## Alternative: Contact Manufacturer

Since they flashed it, ask them:
- "What tool/procedure do you use to flash the boards?"
- "Is there a special recovery mode or button sequence?"
- "Can you provide the flashing instructions?"

They might have:
- A special firmware that enables SWD
- A recovery procedure
- A different programming method

