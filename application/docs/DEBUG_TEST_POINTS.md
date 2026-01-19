# Debug Test Points and Power Supply Verification

## Power Supply Requirements

**CRITICAL**: The board uses **3.0V (3V0)**, NOT 3.3V!

From schematic Sheet 3 (Power Management):
- nPM1100 power management IC regulates voltage
- VOUTB can be configured to: **1V8, 2V1, 2V7, or 3V0**
- The nRF54L05 is powered by **+3V0** (3.0V nominal)

**J-Link detected VTref=3.300V** - This is slightly high! Should be ~3.0V.

## Test Points to Check

### Power Supply Test Points (Sheet 3 - Power Management)
- **TP12**: VSYS (system voltage input)
- **TP14**: VOUTBSET1 (voltage selection)
- **TP16**: NPM_CHG (charge status)
- **TP31**: AVSS (analog ground reference)
- **TP50**: VBATT_PROT (battery protection)

### nRF54L05 Test Points (Sheet 5 - nRF54L05)
- **TP1**: NRF_FLASH_nCS (flash chip select)
- **TP2**: NRF_SPI_CLK (SPI clock)
- **TP18**: NRF_QI_nCHG (Qi charging status)
- **TP19**: NRF_NPM_ERR (power management error)
- **TP20**: NRF_LPS_INT (low power sensor interrupt)
- **TP21**: NRF_I2C_SCL (I2C clock)
- **TP22**: NRF_I2C_SDA (I2C data)
- **TP27**: NRF_LPS_INT / NRF_BATT_ADC_EN
- **TP28**: NRF_BATT_ADC
- **TP32**: NRF_UI_nBTN_1 / NRF_NPM_Mode
- **TP41**: NRF_I2C_SCL
- **TP42**: NRF_I2C_SDA
- **TP44**: **NRF_SWDIO** (SWD data - CHECK THIS!)
- **TP46**: NRF_DEBUG_TX (UART TX)
- **TP47**: NRF_DEBUG_RX (UART RX)
- **TP48**: **NRF_UI_nBTN_1 / NRF_nRESET** (Reset - CHECK THIS!)

## SWD Connection Verification

### Critical Test Points for SWD:
1. **TP44** (NRF_SWDIO) - Should connect to J-Link pin 1
2. **TP48** (NRF_nRESET) - Should connect to J-Link pin 15
3. Check continuity from J3 connector to these test points

### SWD Pin Mapping (from Sheet 6):
- J3 Pin 1-2: NRF_SWDIO → TP44 → nRF54L05 P2.06
- J3 Pin 3-4: NRF_SWCLK → nRF54L05 P2.07
- J3 Pin 9-10: NRF_nRESET → TP48 → nRF54L05 P2.10

## Power Supply Troubleshooting

### Check These Voltages:
1. **+3V0 rail**: Should measure **~3.0V** (not 3.3V!)
   - Measure at any VDD test point or power rail
   - If measuring 3.3V, the nPM1100 might be misconfigured

2. **VTref from J-Link**: Currently detecting 3.300V
   - This suggests board is powered
   - But might be slightly high (should be ~3.0V)

3. **Power source**: 
   - Board should be powered externally (battery or supply)
   - J-Link should NOT supply power (per schematic note)

## Recommended Checks

1. **Measure +3V0 voltage**:
   - Use multimeter on VDD or power rail
   - Should read ~3.0V (acceptable range: 2.9V - 3.1V)
   - If reading 3.3V, check nPM1100 configuration

2. **Check SWD continuity**:
   - Measure from J3 connector to test points:
     - J3 pin 1-2 → TP44 (SWDIO)
     - J3 pin 3-4 → P2.07 pad (SWCLK)
     - J3 pin 9-10 → TP48 (nRESET)
   - All should show continuity (< 1 ohm)

3. **Check ground connection**:
   - Verify GND from J3 to board ground
   - Should have continuity

4. **Verify connector seating**:
   - 0.5mm connector must be fully seated
   - Check for bent pins
   - Verify all pins making contact

## If Power is Wrong

If the board is receiving 3.3V instead of 3.0V:
- Check nPM1100 VOUTBSET0 and VOUTBSET1 configuration
- From schematic: VOUTB can be 1V8, 2V1, 2V7, or 3V0
- Verify jumper/resistor settings for voltage selection

## Next Steps

1. Measure actual voltage on +3V0 rail
2. Check continuity of SWD signals using test points
3. Verify connector is fully seated
4. Try connecting with reset held (TP48 to ground temporarily)

