#ifndef TCA9535_GPIO_H
#define TCA9535_GPIO_H

#include <Arduino.h>
#include <Wire.h>
#include <TCA9555.h>
#include "freertos_util.h"

/**
 * TCA9535 I/O Expander GPIO Wrapper for MeshCore
 *
 * Provides Arduino-style GPIO interface for TCA9535 I/O expander
 * Used to control LoRa module pins on SenseCAP Indicator D1L
 *
 * Hardware Connection:
 * - I2C Address: 0x20 (default for SenseCAP Indicator)
 * - ESP32-S3 I2C: SDA=GPIO6, SCL=GPIO7
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
 * Usage Example:
 * ```cpp
 * TCA9535_GPIO gpio(0x20);
 * Wire.begin(6, 7);  // SDA, SCL
 * if (!gpio.begin(&Wire)) {
 *     Serial.println("TCA9535 init failed!");
 * }
 * gpio.pinMode(TCA9535_GPIO::LORA_NSS, OUTPUT);
 * gpio.digitalWrite(TCA9535_GPIO::LORA_NSS, HIGH);
 * ```
 *
 * @author MeshCore Community
 * @version 1.0.0
 * @date 2025-12-17
 */
class TCA9535_GPIO {
private:
    TCA9555* ioExpander;     // Underlying driver (TCA9555 is compatible with TCA9535)
    uint8_t i2cAddress;      // I2C address of TCA9535
    bool initialized;        // Initialization state
    SemaphoreHandle_t _mutex; // Mutex for thread-safe cache operations
    bool mutexCreated;       // Tracks if mutex was successfully created

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
    uint8_t virtualToPhysical(uint16_t virtualPin) {
        if (virtualPin >= 100 && virtualPin < 116) {
            return virtualPin - 100;
        }
        Serial.printf("[TCA9535] ERROR: Invalid virtual pin %u (valid range: 100-115)\n", virtualPin);
        return 0xFF; // Invalid
    }

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
    TCA9535_GPIO(uint8_t addr = 0x20)
        : ioExpander(nullptr), i2cAddress(addr), initialized(false), mutexCreated(false) {
        outputCache = 0xFFFF;    // All high by default
        directionCache = 0xFFFF; // All inputs by default

        // Create mutex for thread-safe cache operations
        _mutex = xSemaphoreCreateMutex();
        if (_mutex == NULL) {
            Serial.println("[TCA9535] ERROR: Failed to create mutex");
            Serial.println("[TCA9535] Thread-safety cannot be guaranteed!");
            mutexCreated = false;
        } else {
            mutexCreated = true;
        }
    }

    /**
     * Destructor
     *
     * Cleans up resources when object is destroyed.
     */
    ~TCA9535_GPIO() {
        if (ioExpander != nullptr) {
            delete ioExpander;
            ioExpander = nullptr;
        }
        if (_mutex != NULL) {
            vSemaphoreDelete(_mutex);
            _mutex = NULL;
        }
    }

    /**
     * Initialize TCA9535 on I2C bus
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
    bool begin(TwoWire* wire = &Wire) {
        Serial.printf("[TCA9535] Initializing at I2C address 0x%02X\n", i2cAddress);

        // Check if mutex was successfully created
        if (!mutexCreated) {
            Serial.println("[TCA9535] FATAL: Mutex creation failed during construction");
            Serial.println("[TCA9535] Cannot initialize - thread-safety required");
            return false;
        }

        // Create TCA9555 driver instance with address and Wire
        ioExpander = new TCA9555(i2cAddress, wire);
        if (ioExpander == nullptr) {
            Serial.println("[TCA9535] ERROR: Failed to allocate TCA9555 driver");
            return false;
        }

        // Initialize the driver (sets pin modes)
        if (!ioExpander->begin(INPUT)) {
            Serial.printf("[TCA9535] ERROR: Device not found at address 0x%02X\n", i2cAddress);
            Serial.println("[TCA9535] Check I2C wiring: SDA=GPIO39, SCL=GPIO40");
            Serial.println("[TCA9535] Run I2C scanner to verify address");
            delete ioExpander;
            ioExpander = nullptr;
            return false;
        }

        initialized = true;
        Serial.println("[TCA9535] Device found and initialized");

        // Configure LoRa pins to safe defaults
        Serial.println("[TCA9535] Configuring LoRa control pins...");

        // NSS: Output, HIGH (deselect SPI)
        pinMode(LORA_NSS, OUTPUT);
        digitalWrite(LORA_NSS, HIGH);

        // RESET: Output, HIGH (not in reset)
        pinMode(LORA_RESET, OUTPUT);
        digitalWrite(LORA_RESET, HIGH);

        // DIO0/BUSY: Input
        pinMode(LORA_DIO0, INPUT);

        // DIO1: Input (will be used for interrupts)
        pinMode(LORA_DIO1, INPUT);

        Serial.println("[TCA9535] LoRa pins configured:");
        Serial.printf("  NSS (pin %d): OUTPUT, HIGH\n", LORA_NSS);
        Serial.printf("  RESET (pin %d): OUTPUT, HIGH\n", LORA_RESET);
        Serial.printf("  BUSY (pin %d): INPUT\n", LORA_DIO0);
        Serial.printf("  DIO1 (pin %d): INPUT\n", LORA_DIO1);

        // Configure Display control pins
        Serial.println("[TCA9535] Configuring Display control pins...");

        // CS: Output, HIGH (deselected - important for ST7701 init)
        pinMode(DISPLAY_CS, OUTPUT);
        digitalWrite(DISPLAY_CS, HIGH);

        // RST: Output, HIGH (not in reset)
        pinMode(DISPLAY_RST, OUTPUT);
        digitalWrite(DISPLAY_RST, HIGH);

        Serial.printf("  Display CS (pin %d / P0_4): OUTPUT, HIGH\n", DISPLAY_CS);
        Serial.printf("  Display RST (pin %d / P0_5): OUTPUT, HIGH\n", DISPLAY_RST);

        return true;
    }

    /**
     * Configure pin mode (INPUT or OUTPUT)
     *
     * @param pin Virtual pin number (100-115)
     * @param mode INPUT or OUTPUT (Arduino constants)
     */
    void pinMode(uint16_t pin, uint8_t mode) {
        if (!initialized) {
            Serial.println("[TCA9535] ERROR: Not initialized - call begin() first");
            return;
        }

        uint8_t physPin = virtualToPhysical(pin);
        if (physPin == 0xFF) return;

        // Set pin mode on TCA9535
        ioExpander->pinMode1(physPin, mode);

        // Update direction cache
        if (mode == OUTPUT) {
            directionCache &= ~(1 << physPin);
        } else {
            directionCache |= (1 << physPin);
        }

        Serial.printf("[TCA9535] Pin %d (phys %d) set to %s\n",
                     pin, physPin, mode == OUTPUT ? "OUTPUT" : "INPUT");
    }

    /**
     * Write digital value to pin
     *
     * Pin must be configured as OUTPUT first using pinMode().
     * Uses cache to skip I2C write if pin state hasn't changed.
     * Thread-safe via RAII mutex guard.
     *
     * @param pin Virtual pin number (100-115)
     * @param value HIGH (1) or LOW (0)
     */
    void digitalWrite(uint16_t pin, uint8_t value) {
        if (!initialized) {
            Serial.println("[TCA9535] ERROR: Not initialized - call begin() first");
            return;
        }

        uint8_t physPin = virtualToPhysical(pin);
        if (physPin == 0xFF) return;

        // RAII lock guard - automatically releases mutex on all exit paths
        SemaphoreLockGuard lock(_mutex);

        // Check cache - skip I2C write if state unchanged
        uint16_t pinMask = (1 << physPin);
        bool currentState = (outputCache & pinMask) != 0;
        bool newState = (value == HIGH);

        if (currentState == newState) {
            // Pin already in requested state, skip I2C transaction
            return;  // Lock guard automatically releases mutex
        }

        // Write to TCA9535 and verify success
        if (!ioExpander->write1(physPin, value)) {
            Serial.printf("[TCA9535] ERROR: I2C write failed on pin %u\n", pin);
            // Lock guard automatically releases mutex when function exits
            return;  // Don't update cache if hardware write failed
        }

        // Update output cache only on successful write
        if (value == HIGH) {
            outputCache |= pinMask;
        } else {
            outputCache &= ~pinMask;
        }

        // Verbose logging only for debugging - comment out in production
        // Serial.printf("[TCA9535] Pin %d (phys %d) = %s\n",
        //              pin, physPin, value == HIGH ? "HIGH" : "LOW");

        // Lock guard automatically releases mutex when function exits
    }

    /**
     * Read digital value from pin
     *
     * Pin should be configured as INPUT first using pinMode().
     * Can also read OUTPUT pins to verify written state.
     *
     * @param pin Virtual pin number (100-115)
     * @return HIGH (1) or LOW (0)
     */
    uint8_t digitalRead(uint16_t pin) {
        if (!initialized) {
            Serial.println("[TCA9535] ERROR: Not initialized - call begin() first");
            return LOW;
        }

        uint8_t physPin = virtualToPhysical(pin);
        if (physPin == 0xFF) return LOW;

        // Read from TCA9535
        uint8_t value = ioExpander->read1(physPin);

        // Verbose logging only for debugging - uncomment if needed
        // Serial.printf("[TCA9535] Pin %d (phys %d) read = %s\n",
        //              pin, physPin, value == HIGH ? "HIGH" : "LOW");

        return value;
    }

    /**
     * Check if TCA9535 is initialized and ready
     *
     * @return true if initialized, false otherwise
     */
    bool isInitialized() const {
        return initialized;
    }

    /**
     * Get cached output state
     *
     * Returns the last written output states without performing I2C read.
     * Useful for debugging or optimization.
     *
     * @return 16-bit value with each bit representing a pin state
     */
    uint16_t getOutputCache() const {
        return outputCache;
    }

    /**
     * Get cached direction state
     *
     * Returns the configured pin directions without performing I2C read.
     * Bit = 0: OUTPUT, Bit = 1: INPUT
     *
     * @return 16-bit value with each bit representing pin direction
     */
    uint16_t getDirectionCache() const {
        return directionCache;
    }

    /**
     * Get I2C address
     *
     * @return I2C address of this TCA9535
     */
    uint8_t getAddress() const {
        return i2cAddress;
    }

    /**
     * Print status information for debugging
     */
    void printStatus() const {
        Serial.println("===== TCA9535 Status =====");
        Serial.printf("I2C Address: 0x%02X\n", i2cAddress);
        Serial.printf("Initialized: %s\n", initialized ? "YES" : "NO");
        Serial.printf("Output Cache: 0x%04X\n", outputCache);
        Serial.printf("Direction Cache: 0x%04X\n", directionCache);

        Serial.println("\nLoRa Pin States:");
        Serial.printf("  NSS (%d): %s\n", LORA_NSS,
                     (outputCache & (1 << (LORA_NSS - 100))) ? "HIGH" : "LOW");
        Serial.printf("  RESET (%d): %s\n", LORA_RESET,
                     (outputCache & (1 << (LORA_RESET - 100))) ? "HIGH" : "LOW");
        Serial.println("==========================");
    }
};

#endif // TCA9535_GPIO_H
