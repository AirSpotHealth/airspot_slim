# Validation Plan

Hardware verification and production readiness checklist for AirSpot Slim.

## Scope

Focus on post-implementation validation:

- Qi charging behavior
- Power consumption targets
- Sensor accuracy and stability
- BLE/NUS protocol verification
- MCUboot DFU validation
- Factory test flow

## Prerequisites

- Production or EVT/DVT hardware
- J-Link connected via nRF54L15DK
- UART log access at 115200 baud
- Mobile app for BLE protocol validation

## 1. Qi Charging Verification

1. Place device on Qi charger and confirm charge detect
2. Verify LED charging indication patterns
3. Confirm charging status in BLE device state
4. Validate battery percentage increases over time
5. Remove charger and confirm status transitions cleanly

## 2. Power Consumption Profiling

1. Measure idle current in System ON
2. Measure System OFF current after shipping mode
3. Verify power modes:
   - On-demand (manual measurement)
   - Low (3 min), Mid (1 min), High (5 sec)
4. Confirm sensor manager cadence aligns with mode

## 3. Sensor Verification

1. Validate SHT40 readings against reference
2. Validate LPS22HH pressure stability
3. Validate STCC4 CO2 stability and compensation
4. Confirm sensor reset command recovers from fault

## 4. BLE + NUS Protocol

1. Connect and verify MTU negotiation
2. Execute key protocol commands:
   - Get Device State
   - Get Realtime CO2
   - Refresh CO2
   - Get/Set Device Name
   - History fetch and erase
3. Confirm notifications are received correctly

## 5. Data Logging

1. Verify periodic history storage
2. Fetch history pages and validate payloads
3. Erase history and confirm notification

## 6. MCUboot DFU

1. Install signed update via BLE (MCUmgr)
2. Verify swap/confirm sequence
3. Validate rollback behavior on invalid image

## 7. Factory Test Flow

1. Run factory test command over BLE
2. Validate LED pass/fail indication
3. Confirm report data format

## 8. Release Readiness

- All tests pass
- Logs reviewed for errors/warnings
- Documentation updated
- Release artifact version tagged

## Related Docs

- [docs/firmware/DEVELOPER_GUIDE.md](DEVELOPER_GUIDE.md)
- [docs/firmware/BUILD_INSTRUCTIONS.md](BUILD_INSTRUCTIONS.md)
- [docs/firmware/BLE_PROTOCOL.md](BLE_PROTOCOL.md)
- [docs/firmware/SERVICES.md](SERVICES.md)
