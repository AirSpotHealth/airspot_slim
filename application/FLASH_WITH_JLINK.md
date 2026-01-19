# Flash Using J-Link Directly (Bypass West)

Since `west flash` has Python dependency issues, use J-Link directly:

## Quick Method

### Step 1: Power OFF your board

### Step 2: Power ON your board

### Step 3: Within 0.5 seconds, run:

```bash
cd /Users/nirajbhatt/airspot/Airspot_firmware/airspot/airspot_slim/application
JLinkExe -CommandFile /tmp/flash_direct.jlink
```

Or manually:

```bash
JLinkExe
```

Then type these commands:
```
si 1
speed 4000
device NRF54L05_M33
r
h
loadfile /Users/nirajbhatt/airspot/Airspot_firmware/airspot/airspot_slim/application/build/merged.hex
r
g
exit
```

## If SWD Still Doesn't Connect

The firmware has disabled SWD. You need to:

1. **Power OFF** board
2. **Short TP48 to GND** (hold reset)
3. **Power ON** while holding reset
4. **Keep holding for 2 seconds**
5. **Release reset**
6. **Immediately** run the J-Link command above

## Alternative: Fix West Python Issue

If you want to use `west flash`, try:

```bash
# Install dependencies for the Python that west uses
python3 -m pip install intelhex pykwalify pyyaml --break-system-packages --user

# Or check which Python west uses
west --version
# Then install for that Python
```

But using J-Link directly is simpler and avoids the Python issue.

