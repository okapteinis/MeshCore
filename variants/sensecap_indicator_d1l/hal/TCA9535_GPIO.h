#ifndef TCA9535_GPIO_H
#define TCA9535_GPIO_H

#include <Arduino.h>
#include <Wire.h>
#include <TCA9555.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

/**
 * TCA9535 I/O Expander GPIO Wrapper for MeshCore
 *
 * Provides Arduino-style GPIO interface for TCA9535 I/O expander
 * Used to control LoRa module pins on SenseCAP Indicator D1L
 *
 * Hardware Connection:
 * - I2C Address: 0x20 (default for SenseCAP Indicator)
 * - ESP32-S3 I2C: SDA=GPIO39, SCL=GPIO40
 * - I2C Clock: 400kHz recommended
 *
 * TCA9535 Pin Mapping (SenseCAP Indicator D1L):
 * - P0_0 (pin 0): LoRa NSS/CS (Virtual pin 100)
 * - P0_1 (pin 1): LoRa RESET (Virtual pin 101)
 * - P0_2 (pin 2): LoRa DIO0/BUSY (Virtual pin 102)
 * - P0_3 (pin 3): LoRa DIO1 (Virtual pin 103)
 *
 * Virtual Pin Scheme:
 * To avoid conflicts with ESP32-S3 GPIO pins (0-48), TCA9535 pins
 * are mapped to virtual pin numbers 100-115 (16 GPIO pins).
 *
 * Thread Safety:
 * - Uses EXTERN global mutex d1l_i2c_mutex (defined in CustomRadioLibHal.cpp)
 * - All I2C operations are wrapped with xSemaphoreTake/Give
 * - CRITICAL: CustomRadioLibHal MUST be constructed BEFORE TCA9535_GPIO::begin()
 *
 * Usage Example:
 * ```cpp
 * TCA9535_GPIO gpio(0x20);
 * Wire.begin(39, 40);  // SDA, SCL
 *
 * // MUST create CustomRadioLibHal FIRST to initialize global mutex
 * CustomRadioLibHal customHal(&gpio, SPI, spiSettings);
 * customHal.init();
 *
 * // NOW safe to initialize TCA9535
 * if (!gpio.begin(&Wire)) {
 *     Serial.println("TCA9535 init failed!");
 * }
 * gpio.pinMode(TCA9535_GPIO::LORA_NSS, OUTPUT);
 * gpio.digitalWrite(TCA9535_GPIO::LORA_NSS, HIGH);
 * ```
 *
 * @author MeshCore Community
 * @version 2.0.0
 * @date 2025-12-25
 */
class TCA9535_GPIO {
private:
    TCA9555* ioExpander;     // Underlying driver (TCA9555 is compatible with TCA9535)
    uint8_t i2cAddress;      // I2C address of TCA9535
    bool initialized;        // Initialization state

    // Pin state cache to minimize I2C transactions
    // digitalWrite checks cache and skips I2C write if state unchanged
    uint16_t outputCache;    // Cached output states
    uint16_t directionCache; // Cached direction states

    /**
     * Convert virtual pin number to physical TCA9535 pin
     *
     * Virtual pins: 100-115 (offset by 100 to avoid conflicts with real GPIO)
     * Physical pins: 0-15 (TCA9535 has 16 GPIO pins in two ports)
     *
     * @param virtualPin Virtual pin number (100-115)
     * @return Physical pin number (0-15) or 0xFF if invalid
     */
    uint8_t virtualToPhysical(uint16_t virtualPin);

public:
    // Virtual pin definitions for SenseCAP Indicator D1L
    // These map to TCA9535 physical pins but are offset by 100
    static const uint16_t LORA_NSS = 100;    // TCA9535 P0_0 - LoRa Chip Select
    static const uint16_t LORA_RESET = 101;  // TCA9535 P0_1 - LoRa Reset
    static const uint16_t LORA_DIO0 = 102;   // TCA9535 P0_2 - LoRa BUSY (DIO0 on SX1262)
    static const uint16_t LORA_DIO1 = 103;   // TCA9535 P0_3 - LoRa DIO1 (IRQ)

    // Display control pins (shared TCA9535 at 0x20)
    static const uint16_t DISPLAY_CS = 104;   // TCA9535 P0_4 - ST7701 Chip Select
    static const uint16_t DISPLAY_RST = 105;  // TCA9535 P0_5 - ST7701 Reset

    /**
     * Constructor
     *
     * @param addr I2C address of TCA9535 (default 0x20 for SenseCAP Indicator)
     */
    TCA9535_GPIO(uint8_t addr = 0x20);

    /**
     * Destructor
     *
     * Cleans up resources when object is destroyed.
     * Does NOT delete d1l_i2c_mutex (owned by CustomRadioLibHal)
     */
    ~TCA9535_GPIO();

    /**
     * Initialize TCA9535 on I2C bus
     *
     * CRITICAL: This must be called AFTER CustomRadioLibHal constructor has run
     * to ensure d1l_i2c_mutex is initialized.
     *
     * This must be called after Wire.begin() and before using any GPIO functions.
     * Will configure LoRa control pins to their default states:
     * - NSS: OUTPUT, HIGH (deselected)
     * - RESET: OUTPUT, HIGH (not in reset)
     * - DIO0/BUSY: INPUT
     * - DIO1: INPUT
     *
     * @param wire Pointer to TwoWire (I2C) instance (default &Wire)
     * @return true if initialization successful, false otherwise
     */
    bool begin(TwoWire* wire = &Wire);

    /**
     * Configure pin mode (INPUT or OUTPUT)
     *
     * THREAD-SAFE: Uses global d1l_i2c_mutex
     *
     * @param pin Virtual pin number (100-115)
     * @param mode INPUT or OUTPUT (Arduino constants)
     */
    void pinMode(uint16_t pin, uint8_t mode);

    /**
     * Write digital value to pin
     *
     * Pin must be configured as OUTPUT first using pinMode().
     * Uses cache to skip I2C write if pin state hasn't changed.
     *
     * THREAD-SAFE: Uses global d1l_i2c_mutex
     *
     * @param pin Virtual pin number (100-115)
     * @param value HIGH (1) or LOW (0)
     */
    void digitalWrite(uint16_t pin, uint8_t value);

    /**
     * Read digital value from pin
     *
     * Pin should be configured as INPUT first using pinMode().
     * Can also read OUTPUT pins to verify written state.
     *
     * THREAD-SAFE: Uses global d1l_i2c_mutex
     *
     * @param pin Virtual pin number (100-115)
     * @return HIGH (1) or LOW (0)
     */
    uint8_t digitalRead(uint16_t pin);

    /**
     * Check if TCA9535 is initialized and ready
     *
     * @return true if initialized, false otherwise
     */
    bool isInitialized() const;

    /**
     * Get cached output state
     *
     * Returns the last written output states without performing I2C read.
     * Useful for debugging or optimization.
     *
     * @return 16-bit value with each bit representing a pin state
     */
    uint16_t getOutputCache() const;

    /**
     * Get cached direction state
     *
     * Returns the configured pin directions without performing I2C read.
     * Bit = 0: OUTPUT, Bit = 1: INPUT
     *
     * @return 16-bit value with each bit representing pin direction
     */
    uint16_t getDirectionCache() const;

    /**
     * Get I2C address
     *
     * @return I2C address of this TCA9535
     */
    uint8_t getAddress() const;

    /**
     * Print status information for debugging
     */
    void printStatus() const;
};

#endif // TCA9535_GPIO_H
