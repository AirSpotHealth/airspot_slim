# Simple Flashing Instructions

## Quick Steps to Flash Your Board

### Step 1: Make Sure Board is Powered
- Your board needs to be powered (battery or external supply)
- The board should be ON (LEDs working)

### Step 2: Connect J-Link
Connect these 4 wires from your J-Link to the board's J3 connector:

```
J-Link Pin 1 (SWDIO)  →  J3 Pin 1
J-Link Pin 9 (SWCLK)  →  J3 Pin 3 or 4
J-Link Pin 15 (nRESET) → J3 Pin 9 or 10
J-Link GND (Pin 4/6)  →  J3 GND pin (or any ground pad)
```

**Important**: J3 Pin 2 is POWER INPUT - don't connect J-Link to it!

### Step 3: Try to Flash

Open terminal and run:

```bash
cd /Users/nirajbhatt/airspot/Airspot_firmware/airspot/airspot_slim/application
west flash --runner jlink --device NRF54L05_M33
```

### Step 4: If That Doesn't Work - Try Recovery

If you get "debug port unavailable" error:

1. **Power OFF** the board
2. **Find TP48 test point** (or TP6) - this is the reset pin
3. **Short TP48 to GND** (use a wire to connect TP48 to any ground pad)
4. **While holding TP48 to GND**, **power ON** the board
5. **Keep holding for 2 seconds**
6. **Release TP48**
7. **Immediately** (within 1 second) run:

```bash
west flash --runner jlink --device NRF54L05_M33
```

### Step 5: If Still Not Working

Try using J-Link directly:

```bash
cd /Users/nirajbhatt/airspot/Airspot_firmware/airspot/airspot_slim/application
JLinkExe
```

Then type these commands one by one:
```
si 1
speed 1000
device NRF54L05_M33
r
h
sleep 500
connect
```

If it says "Found SWD-DP" or connects, then type:
```
loadfile build/merged.hex
r
g
exit
```

## What the Manufacturer Probably Did

They likely:
1. Used the same J-Link connection
2. Flashed before SWD was disabled
3. Or used a special recovery procedure

## If Nothing Works

Contact your manufacturer and ask:
- "How do you flash/program the boards?"
- "Is there a special procedure to enable SWD?"
- "Can you provide the flashing instructions?"

## Quick Checklist

- [ ] Board is powered ON
- [ ] J-Link connected (4 wires: SWDIO, SWCLK, nRESET, GND)
- [ ] Tried `west flash --runner jlink --device NRF54L05_M33`
- [ ] Tried recovery sequence (power cycle with reset held)
- [ ] Tried J-Link Commander directly

That's it. Start with Step 3, and if it fails, do Step 4.

