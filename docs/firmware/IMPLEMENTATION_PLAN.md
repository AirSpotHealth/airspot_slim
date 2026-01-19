# AirSpot Slim Firmware Implementation Plan

## Executive Summary

This document outlines the migration plan from the existing nRF52832-based firmware (using nRF5 SDK v17.1.0) to a new nRF54L05-based firmware (using nRF Connect SDK with Zephyr RTOS) for the AirSpot Slim device.

**Target Platform:**
- **MCU**: nRF54L05 (ARM Cortex-M33, dual-core)
- **SDK**: nRF Connect SDK (Zephyr RTOS)
- **Development Board**: nRF54L15DK (connected via JTAG 0.5mm)
- **Project Location**: `/slim/` folder

**Key Hardware Changes:**
- ✅ **Qi Wireless Charging** (replaces USB charging)
- ❌ **No Display** (ST7301 LCD removed)
- ✅ **Circuit redesign** (new board layout)
- ✅ **Same CO2 Sensor**: Sensirion SCD4x series
- ✅ **Same Flash**: ZB25D16 SPI flash (likely)
- ✅ **Same BLE**: Bluetooth Low Energy

---

## 1. Feature Analysis - Existing Firmware

### 1.1 Core Features to Migrate

#### ✅ **Must Migrate (All Features)**

1. **CO2 Monitoring**
   - Real-time CO2, temperature, humidity measurement
   - SCD4x sensor driver (`scd4x/` folder)
   - Automatic Self-Calibration (ASC)
   - Sensor error handling and recovery
   - CO2 scale factor adjustment

2. **BLE Communication**
   - Nordic UART Service (NUS)
   - Custom protocol implementation (`protocol.c/h`)
   - All 78+ protocol commands (see `protocol.h`)
   - BLE advertising and connection management
   - Bonding and security (peer manager)
   - DFU (Device Firmware Update) support

3. **Data Logging**
   - Historical data storage to SPI flash
   - History management (`history/` folder)
   - Configuration storage (`cfg_fstorage.c/h`)
   - Flash driver (`flash_spi.c/h`, `zb25d16.c/h`)

4. **Power Management**
   - Multiple power modes (On-demand, Low, Mid, High)
   - Sleep/wake functionality
   - Power-off capability
   - Battery monitoring and voltage reading

5. **Battery Management**
   - Battery voltage monitoring via ADC
   - Battery level calculation (0-100%)
   - Charging detection (now Qi-based instead of USB)
   - Low battery detection and alerts

6. **User Interface (No Display)**
   - Button handling (`button.c/h`)
   - Buzzer control (PWM)
   - Vibrator control (GPIO)
   - LED indicators (if present on new board)
   - Haptic feedback

7. **Alarm System**
   - Configurable CO2 level alarms
   - Buzzer/vibrator activation
   - Advanced alarm settings
   - Do Not Disturb (DND) mode
   - Alarm on CO2 falling

8. **Factory Testing**
   - Comprehensive factory test system (`factory_test_task.c`)
   - Automatic electronic tests
   - Manual tests via BLE commands
   - Real-time test result transmission

9. **System Services**
   - Task scheduler (`ttask.h`)
   - Event system
   - Timer management
   - System time/clock
   - Watchdog timer

10. **Configuration Management**
    - Device name
    - Power mode settings
    - Alarm thresholds
    - Graph display ranges (for app, not device)
    - Bluetooth enable/disable
    - Flight mode

#### ❌ **Remove (Display-Related)**

1. **LCD Display**
   - ST7301 LCD controller driver (`lcd/st7301.c/h`)
   - UI framework (`lcd/ui.c/h`, `lcd/ui_task.c`)
   - Display refresh and rendering
   - Graph display on device
   - Screen illumination on alarm
   - Power-on display sequence

2. **Display-Related Protocol Commands**
   - `CMD_SET_SCREEN_CONST` (0x0E)
   - `CMD_SET_GRAPH_MODE` (0x28)
   - `CMD_SET_ILLUMINATE_SCREEN_ON_ALARM` (0x2D)
   - Factory test screen tests (edge, black, white)

#### 🔄 **Modify (Hardware Changes)**

1. **Charging Detection**
   - **Current**: USB charging via `PIN_USB_IN` and `PIN_CHRG_STA`
   - **New**: Qi wireless charging detection
   - Need to understand Qi charging IC and detection method

2. **Pin Configuration**
   - New board layout means different GPIO assignments
   - Need new `custom_board.h` or device tree configuration

3. **Factory Tests**
   - Remove LCD-related tests
   - Add Qi charging detection test
   - Update test sequence

---

## 2. Hardware Differences & Requirements

### 2.1 MCU Comparison

| Feature | nRF52832 (Current) | nRF54L05 (New) |
|---------|-------------------|----------------|
| Architecture | ARM Cortex-M4F | ARM Cortex-M33 (dual-core) |
| Flash | 512 KB | 1 MB |
| RAM | 64 KB | 256 KB |
| BLE | Bluetooth 5.0 | Bluetooth 5.4 |
| SDK | nRF5 SDK v17.1.0 | nRF Connect SDK (Zephyr) |
| Development | Keil µVision | VS Code + nRF Connect for VS Code |

### 2.2 Hardware Components

#### ✅ **Confirmed Present**
- **CO2 Sensor**: Sensirion SCD4x (I2C)
- **Flash Memory**: ZB25D16 SPI flash (assumed)
- **Button**: Single button (GPIO)
- **Buzzer**: PWM-controlled
- **Vibrator**: GPIO-controlled
- **Battery**: Li-ion/LiPo with ADC monitoring

#### ❌ **Removed**
- **Display**: ST7301 LCD controller
- **USB Charging**: USB connector and charge detection

#### ❓ **Need Information**
- **Qi Charging IC**: Which IC? (e.g., BQ51013B, STWBC86, etc.)
- **Qi Detection Method**: How is charging detected? (GPIO, I2C, ADC?)
- **LEDs**: Are there status LEDs on the new board?
- **Pin Assignments**: Complete GPIO pin mapping for new board
- **Power Management IC**: Any PMIC changes?
- **Crystal**: Same 32.768 kHz crystal for RTC?

---

## 3. Migration Strategy: nRF5 SDK → nRF Connect SDK

### 3.1 Architecture Changes

#### Current Architecture (nRF5 SDK)
- **RTOS**: None (bare-metal with scheduler)
- **Task System**: Custom task scheduler (`ttask.h`)
- **Event System**: Custom event groups
- **Drivers**: nRF5 SDK drivers (nrfx)
- **BLE Stack**: SoftDevice S112/S132/S140
- **Build System**: Keil µVision

#### New Architecture (nRF Connect SDK)
- **RTOS**: Zephyr RTOS
- **Task System**: Zephyr threads/kthreads
- **Event System**: Zephyr events/k_events or message queues
- **Drivers**: Zephyr device drivers
- **BLE Stack**: Zephyr BLE stack (nRF Connect SDK)
- **Build System**: CMake + West (VS Code)

### 3.2 Key Migration Tasks

#### Phase 1: Project Setup
1. Create Zephyr application structure
2. Configure device tree for nRF54L05
3. Set up build system (CMake, Kconfig)
4. Configure BLE stack
5. Set up debugging (JTAG via nRF54L15DK)

#### Phase 2: Hardware Abstraction Layer
1. Port pin configuration to device tree
2. Create GPIO driver wrappers
3. Port SPI driver (for flash)
4. Port I2C driver (for CO2 sensor)
5. Port ADC driver (for battery)
6. Port PWM driver (for buzzer)
7. Implement Qi charging detection

#### Phase 3: Core Drivers
1. Port SCD4x sensor driver
2. Port SPI flash driver (ZB25D16)
3. Port battery monitoring
4. Port button driver
5. Port buzzer/vibrator control

#### Phase 4: Application Layer
1. Port task scheduler to Zephyr threads
2. Port event system to Zephyr events
3. Port BLE protocol implementation
4. Port data logging system
5. Port power management
6. Port factory testing

#### Phase 5: Remove Display Code
1. Remove all LCD/display drivers
2. Remove UI framework
3. Remove display-related protocol commands
4. Update factory tests

#### Phase 6: Testing & Validation
1. Unit testing of drivers
2. Integration testing
3. BLE protocol validation
4. Factory test validation
5. Power consumption testing

---

## 4. Implementation Phases

### Phase 1: Foundation (Week 1-2)
**Goal**: Get basic Zephyr application running on nRF54L05

**Tasks**:
- [ ] Set up nRF Connect SDK development environment
- [ ] Create Zephyr application skeleton in `/slim/`
- [ ] Configure device tree for nRF54L05 custom board
- [ ] Set up CMake build system
- [ ] Configure JTAG debugging via nRF54L15DK
- [ ] Get basic BLE advertising working
- [ ] Verify UART logging works

**Deliverables**:
- Working Zephyr application that boots
- BLE advertising visible
- Serial console logging functional

### Phase 2: Hardware Drivers (Week 3-4)
**Goal**: Port all hardware drivers to Zephyr

**Tasks**:
- [ ] Port GPIO configuration (device tree)
- [ ] Port I2C driver for CO2 sensor
- [ ] Port SPI driver for flash memory
- [ ] Port ADC driver for battery monitoring
- [ ] Port PWM driver for buzzer
- [ ] Port button driver (GPIO interrupt)
- [ ] Implement Qi charging detection
- [ ] Port vibrator control (GPIO)

**Deliverables**:
- All hardware peripherals accessible
- Basic sensor communication working
- Battery voltage reading functional

### Phase 3: Sensor & Storage (Week 5-6)
**Goal**: CO2 sensor and flash storage working

**Tasks**:
- [ ] Port SCD4x sensor driver
- [ ] Port CO2 measurement logic
- [ ] Port SPI flash driver (ZB25D16)
- [ ] Port history storage system
- [ ] Port configuration storage
- [ ] Test data logging functionality

**Deliverables**:
- CO2 readings working
- Data logging to flash functional
- Configuration persistence working

### Phase 4: BLE Protocol (Week 7-8)
**Goal**: Full BLE communication with mobile app

**Tasks**:
- [ ] Port Nordic UART Service (NUS)
- [ ] Port protocol frame parsing
- [ ] Port all protocol commands (78+ commands)
- [ ] Port BLE bonding/security
- [ ] Port DFU support
- [ ] Test with mobile app

**Deliverables**:
- All BLE protocol commands working
- Mobile app can communicate with device
- DFU updates functional

### Phase 5: Application Logic (Week 9-10)
**Goal**: Core application features working

**Tasks**:
- [ ] Port task scheduler to Zephyr threads
- [ ] Port event system
- [ ] Port power management
- [ ] Port alarm system
- [ ] Port DND mode
- [ ] Port factory testing (without display tests)
- [ ] Remove all display-related code

**Deliverables**:
- All application features working
- Power management optimized
- Factory tests functional

### Phase 6: Integration & Testing (Week 11-12)
**Goal**: Complete system validation

**Tasks**:
- [ ] End-to-end testing
- [ ] Power consumption optimization
- [ ] BLE range and reliability testing
- [ ] Factory test validation
- [ ] Bug fixes and refinements
- [ ] Documentation updates

**Deliverables**:
- Production-ready firmware
- Complete test coverage
- Updated documentation

---

## 5. Information Needed from Hardware Team

See [../board/HARDWARE_INFO_NEEDED.md](../board/HARDWARE_INFO_NEEDED.md) for complete checklist.

### Critical Information Required:

1. **Qi Charging Implementation**
   - [ ] Which Qi charging IC is used?
   - [ ] How is charging detected? (GPIO pin, I2C register, ADC?)
   - [ ] What signals are available? (CHG_STAT, CHG_DET, etc.)
   - [ ] Charging current/voltage specifications?

2. **Pin Configuration**
   - [ ] Complete GPIO pin mapping document
   - [ ] I2C pins for CO2 sensor
   - [ ] SPI pins for flash memory
   - [ ] ADC pin for battery voltage
   - [ ] PWM pin for buzzer
   - [ ] GPIO pins for vibrator, button
   - [ ] Qi charging detection pins

3. **Power Management**
   - [ ] Power consumption targets
   - [ ] Sleep current requirements
   - [ ] Wake-up sources
   - [ ] Power sequencing requirements

4. **Hardware Features**
   - [ ] Are there status LEDs? (GPIO pins?)
   - [ ] Any other new peripherals?
   - [ ] Flash memory model confirmed? (ZB25D16?)
   - [ ] Crystal frequency? (32.768 kHz?)

5. **Mechanical/Electrical**
   - [ ] JTAG connector pinout (0.5mm pitch)
   - [ ] Power supply specifications
   - [ ] Battery specifications

---

## 6. Project Structure (Proposed)

```
slim/
├── CMakeLists.txt                 # Main CMake build file
├── prj.conf                       # Zephyr Kconfig settings
├── boards/
│   └── airspot_slim.overlay      # Device tree overlay for custom board
├── src/
│   ├── main.c                     # Application entry point
│   ├── battery.c/h               # Battery management
│   ├── button.c/h                # Button handling
│   ├── protocol.c/h              # BLE protocol
│   ├── user.c/h                   # User settings
│   ├── scd4x/                     # CO2 sensor driver
│   │   ├── driver_scd4x.c/h
│   │   └── co2.c/h
│   ├── flash/                     # Flash storage
│   │   ├── flash_spi.c/h
│   │   ├── zb25d16.c/h
│   │   ├── history.c/h
│   │   └── cfg_fstorage.c/h
│   ├── qi_charging.c/h           # Qi charging detection (NEW)
│   ├── factory_test.c/h           # Factory testing
│   └── tasks/                      # Zephyr thread implementations
│       ├── co2_task.c/h
│       ├── ble_task.c/h
│       └── factory_test_task.c/h
├── include/
│   └── custom_board.h             # Board-specific definitions
└── README.md                       # Project documentation
```

---

## 7. Key Technical Challenges

### 7.1 Task Scheduler Migration
**Challenge**: Current firmware uses custom task scheduler (`ttask.h`) with cooperative multitasking.

**Solution**: 
- Map tasks to Zephyr threads
- Use Zephyr events/k_events for task coordination
- Use `k_sleep()` instead of `TaskDelay()`
- Use message queues for inter-task communication

### 7.2 Event System Migration
**Challenge**: Custom event group system needs Zephyr equivalent.

**Solution**:
- Use Zephyr `k_event` API for event groups
- Or use message queues with event messages
- Maintain same event bit definitions

### 7.3 BLE Stack Differences
**Challenge**: SoftDevice vs Zephyr BLE stack API differences.

**Solution**:
- Use nRF Connect SDK BLE APIs (similar to SoftDevice)
- Port NUS service implementation
- Adapt connection parameter management

### 7.4 Power Management
**Challenge**: Different power management APIs in Zephyr.

**Solution**:
- Use Zephyr power management APIs
- Configure device tree for low-power modes
- Use Zephyr PM hooks for sleep/wake

### 7.5 Qi Charging Detection
**Challenge**: New hardware feature, no existing code.

**Solution**:
- Research Qi charging IC datasheet
- Implement GPIO interrupt for charge detection
- Or use I2C polling if IC supports it
- Integrate with battery management

---

## 8. Testing Strategy

### 8.1 Unit Testing
- Individual driver testing
- Protocol command testing
- Sensor communication testing

### 8.2 Integration Testing
- BLE communication with mobile app
- Data logging functionality
- Power management validation
- Factory test sequence

### 8.3 Hardware Testing
- Qi charging detection
- Battery monitoring accuracy
- Power consumption measurements
- BLE range and reliability

### 8.4 Factory Test Validation
- All automatic tests pass
- Manual test procedures verified
- Test result transmission confirmed

---

## 9. Risk Assessment

### High Risk
- **BLE Protocol Compatibility**: Ensure mobile app compatibility
- **Power Consumption**: May need optimization for battery life
- **Qi Charging Detection**: New feature, may need hardware debugging

### Medium Risk
- **Task Scheduler Migration**: Complex logic to port
- **Flash Driver Compatibility**: Ensure ZB25D16 works with Zephyr SPI
- **Factory Test Timing**: May need adjustment for Zephyr timing

### Low Risk
- **Sensor Driver**: SCD4x driver should port easily
- **Button/Buzzer/Vibrator**: Simple GPIO/PWM operations
- **Configuration Storage**: Similar flash storage pattern

---

## 10. Success Criteria

### Functional Requirements
- ✅ All 78+ BLE protocol commands working
- ✅ CO2 monitoring with ASC functional
- ✅ Data logging to flash working
- ✅ Qi charging detection working
- ✅ Factory tests passing
- ✅ DFU updates working
- ✅ Power management optimized

### Performance Requirements
- ✅ BLE connection stable
- ✅ CO2 reading accuracy maintained
- ✅ Battery life meets specifications
- ✅ Factory test completion time acceptable

### Quality Requirements
- ✅ No memory leaks
- ✅ Robust error handling
- ✅ Comprehensive logging
- ✅ Code documentation complete

---

## 11. Next Steps

### Immediate Actions:
1. **Gather Hardware Information** (see Section 5)
2. **Set up Development Environment**
   - Install nRF Connect SDK
   - Install VS Code + nRF Connect extension
   - Set up JTAG connection via nRF54L15DK
3. **Create Project Skeleton**
   - Initialize Zephyr application
   - Configure device tree
   - Set up build system

### Questions to Answer:
1. What Qi charging IC is used?
2. Complete pin mapping for new board?
3. Are there status LEDs?
4. Power consumption targets?
5. Any other hardware changes not mentioned?

---

## 12. References

### Existing Firmware
- Location: `nRF5_SDK_17.1.0_ddde560/examples/ble_peripheral/ble_app_uart_minico2/`
- Key files: `main.c`, `protocol.c/h`, `custom_board.h`

### Documentation
- **Hardware**: See [../board/](../board/) for pin assignments and schematic analysis
- **Schematic**: See [../datasheets/](../datasheets/) for board schematics
- nRF Connect SDK Documentation: https://developer.nordicsemi.com/
- Zephyr RTOS Documentation: https://docs.zephyrproject.org/
- nRF54L05 Product Page: https://www.nordicsemi.com/Products/nRF54L05
- SCD4x Sensor Datasheet: https://www.sensirion.com/en/environmental-sensors/carbon-dioxide-sensors/carbon-dioxide-sensor-scd4x/

---

**Document Version**: 1.0  
**Last Updated**: 2025-01-XX  
**Author**: Firmware Development Team  
**Status**: Draft - Awaiting Hardware Information

