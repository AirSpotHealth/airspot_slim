# JTAG/SWD Connection Guide for AirSpot Slim

## Critical Information from Schematic

### ⚠️ IMPORTANT NOTES:
1. **"Ensure programmer does not supply 3V3 power"** - J-Link must NOT supply power!
2. **Board requires EXTERNAL 3.0V power** (EXT_3V0)
3. **Power is on J3 Pin 2** (EXT_3V0), not from J-Link

## J3 Connector Pinout (10-Pin JTAG Compatible)

| Pin | Signal | Description | J-Link Pin | Notes |
|-----|--------|-------------|------------|-------|
| 1 | NRF_SWDIO | SWD Data I/O | Pin 1 (SWDIO/TMS) | **Required** |
| 2 | **EXT_3V0** | **External 3.0V Power** | **DO NOT CONNECT** | **Board must supply this!** |
| 3-4 | NRF_SWCLK | SWD Clock | Pin 9 (SWCLK/TCK) | **Required** |
| 5-6 | NRF_SWO | SWO/Trace | Pin 13 (SWO) | Optional |
| 7-8 | (unused) | - | - | - |
| 9-10 | NRF_nRESET | Reset | Pin 15 (nRESET) | **Required** |
| GND | Ground | Ground | Pin 4 or 6 | **Required** |

## Connection Requirements

### ✅ What to Connect:
1. **SWDIO** (J3 Pin 1) → J-Link Pin 1
2. **SWCLK** (J3 Pin 3-4) → J-Link Pin 9
3. **nRESET** (J3 Pin 9-10) → J-Link Pin 15
4. **GND** → J-Link Pin 4 or 6

### ❌ What NOT to Connect:
- **J3 Pin 2 (EXT_3V0)** → **DO NOT connect to J-Link!**
- **J-Link Vref** → **DO NOT use to power board!**

### ⚡ Power Supply:
- Board must be powered **externally** via EXT_3V0 (J3 Pin 2)
- Power supply must be **3.0V** (not 3.3V!)
- J-Link should detect VTref=3.0V when board is powered

## Alternative Connection Points

### Wire Pads (WP) - If J3 is not accessible:
- **WP10**: NRF_DEBUG_TX (UART TX)
- **WP11**: NRF_DEBUG_RX (UART RX)
- **WP12**: NRF_nRESET (Reset)

### Test Points:
- **TP6**: nRESET (Reset test point)
- **TP44**: NRF_SWDIO (SWDIO test point)
- **TP48**: NRF_nRESET (Reset test point)

## Connection Verification

### Step 1: Verify Power
1. Board must be powered externally (battery or supply)
2. Measure voltage on EXT_3V0 (J3 Pin 2) - should be ~3.0V
3. J-Link should detect VTref when connected

### Step 2: Verify Connections
Use multimeter in continuity mode:

| Check | Expected Result |
|-------|----------------|
| J3 Pin 1 → TP44 | Should beep (< 1 ohm) |
| J3 Pin 3-4 → P2.07 pad | Should beep |
| J3 Pin 9-10 → TP48 or TP6 | Should beep |
| GND → Any ground pad | Should beep |

### Step 3: Verify J-Link Settings
- Device: NRF54L05_M33
- Interface: SWD
- Speed: 1000-4000 kHz
- **DO NOT supply power from J-Link**

## Common Issues

### Issue 1: "Debug port unavailable"
**Cause**: SWD disabled in firmware or device locked
**Solution**: 
- Power cycle with reset held (TP48/TP6 to GND)
- Try connecting immediately after power-on

### Issue 2: "No VTref detected"
**Cause**: Board not powered externally
**Solution**: 
- Ensure board is powered via EXT_3V0 (J3 Pin 2)
- Check power supply is 3.0V

### Issue 3: "Cannot connect"
**Cause**: Wrong connections or SWD disabled
**Solution**:
- Verify all 4 connections (SWDIO, SWCLK, nRESET, GND)
- Try recovery sequence (power cycle with reset)

## Recovery Procedure

If SWD is disabled:

1. **Power OFF** board completely
2. **Short TP48 (or TP6) to GND** (use J3 GND or any ground pad)
3. **While holding reset**, power ON board
4. **Keep reset held** for 2-3 seconds
5. **Release reset**
6. **Within 1 second**, try connecting:

```bash
west flash --runner jlink --device NRF54L05_M33
```

## J9 Connector (Alternative - May Not Be Populated)

**Note**: Schematic says "'Do not fit' we will only fit a few of these in house"

If J9 (10-Pin Tag-Connect) is present:
- Pin 1: NRF_SWDIO
- Pin 2: NRF_SWCLK  
- Pin 3: NRF_DEBUG_TX
- Pin 4: NRF_DEBUG_RX
- Pin 5: NRF_nRESET
- Pin 6: NRF_SWO
- Pin 9: nRESET
- Pin 10: GND

## Summary

**Key Points**:
1. ✅ Connect: SWDIO, SWCLK, nRESET, GND
2. ❌ Do NOT connect: EXT_3V0 to J-Link
3. ⚡ Board must be powered externally (3.0V)
4. 🔄 If SWD disabled, use recovery sequence

The fact that your board works (LEDs, button) confirms hardware is fine. The SWD connection issue is likely firmware-related (SWD disabled) or connection-related (missing GND or wrong pins).

