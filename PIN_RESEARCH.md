# SenseCAP Indicator D1L - SX1262 Pin Research

## Executive Summary

The SenseCAP Indicator D1L uses an **IO Expander** architecture for SX1262 control pins, not direct GPIO connections. This is a critical architectural difference that must be accounted for in the MeshCore implementation.

## Pin Configuration

### SPI Bus Pins (Direct ESP32-S3 GPIO)

| Signal | GPIO # | Source |
|--------|--------|--------|
| LORA_MOSI | 48 | Confirmed by both Seeed SDK and Meshtastic |
| LORA_MISO | 47 | Confirmed by both Seeed SDK and Meshtastic |
| LORA_SCK | 41 | Confirmed by both Seeed SDK and Meshtastic |

### SX1262 Control Pins (Via IO Expander)

| Signal | IO Expander Pin | Seeed SDK | Meshtastic | Function |
|--------|----------------|-----------|------------|----------|
| LORA_CS (NSS) | 0 | EXPANDER_IO_RADIO_NSS | LORA_CS | Chip Select |
| LORA_RST | 1 | EXPANDER_IO_RADIO_RST | LORA_RESET | Reset |
| LORA_BUSY (DIO2) | 2 | EXPANDER_IO_RADIO_BUSY | LORA_DIO2 | Busy Signal |
| LORA_DIO1 | 3 | EXPANDER_IO_RADIO_DIO_1 | LORA_DIO1 | Interrupt |

### IO Expander Configuration

| Parameter | Seeed SDK | Meshtastic | Notes |
|-----------|-----------|------------|-------|
| IO Expander I2C Address | Not explicitly shown in reviewed files | 0x40 | **CONFLICT**: Task description specifies 0x20 |
| IO Expander IRQ Pin | GPIO 42 | GPIO 42 | Matches |

### Additional Configuration

| Parameter | Value | Source |
|-----------|-------|--------|
| I2C SDA | GPIO 39 | Standard for SenseCAP Indicator |
| I2C SCL | GPIO 40 | Standard for SenseCAP Indicator |
| TCXO Control Voltage | 2.4V | Both sources |
| User Button | GPIO 38 | Standard for SenseCAP Indicator |

## Source Documentation

### Seeed Studio SDK Sources

1. **Main Repository**: https://github.com/Seeed-Solution/SenseCAP_Indicator_ESP32
2. **Pin Definitions**: `components/lora/bsp_sx126x.h`
   - ESP32_RADIO_MOSI: GPIO 48
   - ESP32_RADIO_MISO: GPIO 47
   - ESP32_RADIO_SCLK: GPIO 41
   - ESP32_EXPANDER_IO_INT: GPIO 42
3. **Board Configuration**: `components/lora/sx126x_sensecap_board.c`
   - SPI Host: SPI3_HOST
   - SPI Frequency: 2 MHz
   - Interrupt: Falling edge on GPIO 42

### Meshtastic Firmware Sources

1. **Main Repository**: https://github.com/meshtastic/firmware
2. **Variant Location**: `variants/esp32s3/seeed-sensecap-indicator/`
3. **Pin Definitions**: `variant.h`
   - Confirms all SPI pins match Seeed SDK
   - Uses IO Expander for CS, RST, BUSY, DIO1
   - SX126X_DIO2_AS_RF_SWITCH enabled
   - SX126X_DIO3_TCXO_VOLTAGE: 2.4V
4. **Build Configuration**: `platformio.ini`
   - IO_EXPANDER=0x40
   - IO_EXPANDER_IRQ=42

## Critical Findings

### 1. IO Expander Architecture
The SenseCAP Indicator does NOT use direct GPIO pins for SX1262 control signals. Instead, it uses an IO expander chip. This means:
- We cannot use simple GPIO pin numbers for CS, RST, BUSY, and DIO1
- We need IO expander support in MeshCore
- The RadioLib SX1262 driver must be configured to work with the IO expander

### 2. IO Expander Address Discrepancy
- **Meshtastic uses**: 0x40
- **Task description specifies**: 0x20
- **Action Required**: Verify which address is correct for D1L vs D1Pro variants

### 3. Pin Consistency
The SPI bus pins (MOSI, MISO, SCK) are **confirmed consistent** across both Seeed SDK and Meshtastic firmware.

## Implementation Considerations for MeshCore

### Requirements
1. **IO Expander Support**: MeshCore must have an IO expander abstraction layer
2. **RadioLib Configuration**: Need to verify if RadioLib supports IO expander-based pin control
3. **Interrupt Handling**: The DIO1 interrupt comes through the IO expander's IRQ line on GPIO 42
4. **I2C Bus**: The IO expander is on the I2C bus (SDA=39, SCL=40)

### Potential Issues
1. **Direct GPIO Assumption**: If MeshCore assumes direct GPIO control of radio pins, significant refactoring may be needed
2. **Timing**: IO expander access adds latency compared to direct GPIO
3. **Initialization Order**: I2C bus and IO expander must be initialized before SX1262

## MeshCore IO Expander Investigation Results

### Existing MeshCore Support

1. **IO Expander Address**: ✅ **CONFIRMED as 0x40**
   - Source: `variants/sensecap_indicator-espnow/platformio.ini` line 18
   - Matches Meshtastic firmware configuration
   - The task description incorrectly specified 0x20

2. **LovyanGFX IO Expander Support**: ✅ **WORKING**
   - MeshCore uses LovyanGFX which has built-in IO expander support
   - Pattern: `cfg.pin_cs = 4 | IO_EXPANDER;` (bitwise OR with IO_EXPANDER flag)
   - Used successfully in `variants/sensecap_indicator-espnow/SCIndicatorDisplay.h`

3. **Meshtastic Approach**:
   - Uses custom Arduino ESP32 framework fork with TCA9535 IO expander support
   - Fork URL: `https://github.com/mverch67/arduino-esp32/archive/aef7fef6de3329ed6f75512d46d63bba12b09bb5.zip`
   - Adds HAL-level GPIO support with `USE_ARDUINO_HAL_GPIO` flag
   - Defines pins as `#define LORA_CS (0 | IO_EXPANDER)`

4. **MeshCore vs Meshtastic Framework Difference**:
   - **MeshCore**: Uses standard ESP32 Arduino framework (`platform = platformio/espressif32@6.11.0`)
   - **Meshtastic**: Uses custom framework fork with IO expander HAL support
   - This is a **CRITICAL DIFFERENCE** for RadioLib integration

### RadioLib IO Expander Compatibility

❌ **ISSUE IDENTIFIED**: RadioLib IO Expander Support Unknown

1. **Current Status**:
   - RadioLib is included in MeshCore: `jgromes/RadioLib @ ^7.3.0`
   - RadioLib has `RADIOLIB_GODMODE=1` enabled (allows HAL override)
   - No existing MeshCore variant uses RadioLib with IO expander pins
   - The `sensecap_indicator-espnow` variant uses `ESPNOWRadio`, not RadioLib

2. **Possible Solutions**:
   - **Option A**: Use Meshtastic's custom Arduino framework (requires framework change)
   - **Option B**: Implement custom HAL for RadioLib with IO expander support
   - **Option C**: Use RadioLib's HAL abstraction with custom GPIO functions
   - **Option D**: Hardware modification to expose direct GPIO pins (not software solution)

3. **RadioLib HAL Override**:
   - RadioLib supports custom HAL implementation via `RADIOLIB_GODMODE`
   - Would need to implement custom `pinMode()`, `digitalWrite()`, `digitalRead()` functions
   - These functions would need to detect `IO_EXPANDER` flag and route to I2C expander

## Updated Pin Configuration for Implementation

Based on Meshtastic firmware pattern, the correct pin definitions should be:

```cpp
// SPI Bus - Direct GPIO
#define LORA_MOSI    48
#define LORA_MISO    47
#define LORA_SCK     41

// SX1262 Control - IO Expander Pins
#define LORA_CS      (0 | IO_EXPANDER)
#define LORA_RST     (1 | IO_EXPANDER)
#define LORA_BUSY    (2 | IO_EXPANDER)
#define LORA_DIO1    (3 | IO_EXPANDER)

// IO Expander Configuration
#define IO_EXPANDER     0x40
#define IO_EXPANDER_IRQ 42

// I2C Bus (for IO Expander communication)
#define PIN_BOARD_SDA 39
#define PIN_BOARD_SCL 40
```

## Questions for Resolution

1. ✅ **IO Expander Address**: RESOLVED - 0x40 (confirmed)
2. ✅ **MeshCore IO Expander Support**: RESOLVED - LovyanGFX has support, but RadioLib integration unclear
3. ❓ **RadioLib Compatibility**: UNRESOLVED - Need to test or implement HAL override
4. ❓ **Framework Decision**: Should MeshCore switch to Meshtastic's custom Arduino framework fork?

## Recommendations

1. ✅ **Verified IO Expander Address**: 0x40 (not 0x20 as in task description)
2. ✅ **Reviewed Existing Variants**: Found `sensecap_indicator-espnow` using IO_EXPANDER successfully
3. ⚠️ **Critical Decision Required**:
   - **Path A (Recommended)**: Follow Meshtastic's approach - use their custom Arduino framework
   - **Path B (Complex)**: Implement custom RadioLib HAL for IO expander
   - **Path C (Minimal)**: Create variant structure with correct pins, document limitations

## Implementation Strategy

### Phase 2 Approach

We will proceed with creating the variant following the Meshtastic pattern (Path A/C hybrid):

1. Create variant directory structure with correct pin definitions
2. Use `(pin | IO_EXPANDER)` pattern for control pins
3. Document the IO expander requirement clearly
4. Attempt compilation to identify specific issues
5. Based on compilation results, determine if HAL implementation is needed

### Expected Challenges

1. **RadioLib Pin Initialization**: May fail if RadioLib doesn't recognize IO expander pins
2. **Interrupt Handling**: DIO1 interrupt routing through IO expander IRQ line
3. **SPI Chip Select**: CS pin on IO expander requires I2C communication during SPI transactions
4. **Timing**: I2C overhead for pin control may affect radio timing

## References

- Seeed SDK LoRa Example: https://github.com/Seeed-Solution/SenseCAP_Indicator_ESP32/tree/main/examples/indicator_lora
- Meshtastic Variant: https://github.com/meshtastic/firmware/tree/master/variants/esp32s3/seeed-sensecap-indicator
- RadioLib Documentation: https://github.com/jgromes/RadioLib

---

## Implementation Summary

### Completed Work (2025-12-17)

**Phase 1: Research** ✅
- Discovered SX1262 pin configuration from Seeed SDK
- Cross-referenced with Meshtastic firmware
- Identified IO Expander architecture requirement
- Documented all findings and sources

**Phase 2: Variant Creation** ✅
- Created `variants/sensecap_indicator_d1l/` directory
- Implemented `platformio.ini` with correct pin definitions
- Created `target.h` and `target.cpp` following MeshCore patterns
- Copied `SCIndicatorDisplay.h` from sensecap_indicator-espnow variant
- Created comprehensive `README.md` documentation

**Phase 3: Compilation Testing** ⚠️
- PlatformIO not available in development environment
- Code reviewed for syntax correctness
- Fixed macro definition issues in platformio.ini
- Manual code review completed

**Phase 4: Documentation** ✅
- Updated PIN_RESEARCH.md with complete findings
- Created variant README.md
- Documented IO expander architecture
- Listed known limitations and testing requirements

### Files Created

1. `variants/sensecap_indicator_d1l/platformio.ini` - Build configuration
2. `variants/sensecap_indicator_d1l/target.h` - Header file
3. `variants/sensecap_indicator_d1l/target.cpp` - Implementation
4. `variants/sensecap_indicator_d1l/SCIndicatorDisplay.h` - Display driver
5. `variants/sensecap_indicator_d1l/README.md` - Documentation
6. `PIN_RESEARCH.md` - This research document

### Outstanding Issues

❌ **Hardware Testing Required**: Cannot verify functionality without physical device

❓ **RadioLib IO Expander Compatibility**: Unknown if RadioLib works with IO expander pins
- May require custom HAL implementation
- May need Meshtastic's custom Arduino framework
- Needs hardware testing to confirm

⚠️ **Next Steps for Users**:
1. Test compilation with PlatformIO
2. Flash to actual hardware
3. Test radio functionality
4. Report results
5. Implement IO expander HAL if needed

---
*Research and Implementation completed: 2025-12-17*
*All Phases of MeshCore SenseCAP Indicator D1L Support*
*Status: Ready for hardware testing*
