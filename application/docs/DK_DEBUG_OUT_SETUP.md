# nRF54L15DK Debug Out Setup for External Board

## Reference
Based on: https://docs.nordicsemi.com/bundle/ug_nrf54l15_dk/page/UG/nRF54L15_DK/program_debug/debug_out_ext_board.html

## Key Points from Documentation

1. **Debug Out Connector**: P19 on the DK
2. **10-pin cable**: Connect to external board
3. **Power**: External board must be powered separately
4. **VDDIO Range**: 1.8V to 3.6V (your 3.0V is fine!)
5. **DK acts as J-Link**: The DK's onboard debugger routes to external target

## Voltage Detection (3.3V vs 3.0V)

### Why J-Link Shows 3.300V

**Possible reasons:**

1. **nPM1100 Configuration**: Your board's nPM1100 might be configured to output 3.3V instead of 3.0V
   - Check VOUTBSET0 and VOUTBSET1 settings
   - From schematic: VOUTB can be 1V8, 2V1, 2V7, or **3V0**
   - If resistors/jumpers are set wrong, it might output 3.3V

2. **J-Link Reading DK's Voltage**: The VTref might be reading the DK's internal voltage (3.3V) instead of your custom board
   - This is OK - as long as your board is powered, it should work

3. **Voltage Tolerance**: 3.0V ± 10% = 2.7V to 3.3V
   - 3.300V is within acceptable range
   - Your board should work fine

### How to Verify Actual Voltage

**Measure on your custom board:**
1. Use multimeter on VDD pad or +3V0 rail
2. Should read ~3.0V (2.9V - 3.1V ideal)
3. If reading 3.3V, check nPM1100 configuration

**Test Points to Check:**
- TP12: VSYS (system voltage input)
- Any VDD pad: Should be ~3.0V
- J3 Pin 2 (EXT_3V0): Should be ~3.0V

## DK Debug Out Connection

According to Nordic documentation, the DK's debug out (P19) should automatically route to external target when connected.

**Your Setup:**
- ✅ DK connected via USB (onboard J-Link active)
- ✅ Custom board connected to DK's debug out (P19) via 0.5mm cable
- ✅ Custom board powered separately (Power Profiler, 3.0V)
- ✅ VTref detected (3.300V)

## The Real Issue

The voltage (3.0V vs 3.3V) is **NOT the problem**. The issue is:

**SWD is disabled by firmware** - that's why you can't connect.

The 3.300V reading is fine (within tolerance). Even if it's slightly high, it won't prevent SWD connection. The "debug port unavailable" error is because SWD is disabled, not because of voltage.

## Next Steps

1. **Verify actual voltage** on your board with multimeter
2. **If it's 3.3V instead of 3.0V**: Check nPM1100 VOUTBSET configuration
3. **Focus on SWD recovery**: The voltage is fine - focus on enabling SWD access

## Summary

- **3.300V is acceptable** (within 3.0V ± 10% tolerance)
- **Voltage is NOT causing the SWD issue**
- **SWD disabled by firmware** is the real problem
- **Contact manufacturer** for their flashing procedure

