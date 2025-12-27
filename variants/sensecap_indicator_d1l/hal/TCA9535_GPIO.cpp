#include "TCA9535_GPIO.h"
#include "freertos_util.h"

/**
 * External reference to global I2C mutex
 *
 * This mutex is created and initialized by CustomRadioLibHal constructor.
 * CRITICAL: CustomRadioLibHal MUST be constructed BEFORE TCA9535_GPIO::begin()
 * is called to ensure this mutex is available.
 */
extern SemaphoreHandle_t d1l_i2c_mutex;

// Constructor
TCA9535_GPIO::TCA9535_GPIO(uint8_t addr)
    : ioExpander(nullptr), i2cAddress(addr), initialized(false) {
    outputCache = 0xFFFF;    // All high by default
    directionCache = 0xFFFF; // All inputs by default
    Serial.println("[TCA9535] Constructor called - will use global d1l_i2c_mutex");
}

// Destructor
TCA9535_GPIO::~TCA9535_GPIO() {
    if (ioExpander != nullptr) {
        delete ioExpander;
        ioExpander = nullptr;
    }
    // Do NOT delete d1l_i2c_mutex - it's owned by CustomRadioLibHal
}

// Initialize TCA9535 on I2C bus
bool TCA9535_GPIO::begin(TwoWire* wire) {
    Serial.printf("[TCA9535] Initializing at I2C address 0x%02X\n", i2cAddress);

    // Verify global mutex exists
    if (d1l_i2c_mutex == NULL) {
        Serial.println("[TCA9535] FATAL: Global I2C mutex not initialized!");
        Serial.println("[TCA9535] Ensure CustomRadioLibHal is constructed FIRST");
        return false;
    }

    // Create TCA9555 driver instance
    ioExpander = new TCA9555(i2cAddress, wire);
    if (ioExpander == nullptr) {
        Serial.println("[TCA9535] ERROR: Failed to allocate TCA9555 driver");
        return false;
    }

    // Initialize the driver (sets pin modes) - thread-safe I2C operation with RAII
    bool initSuccess = false;
    {
        SemaphoreLockGuard lock(d1l_i2c_mutex);
        if (!lock.isLocked()) {
            Serial.println("[TCA9535] ERROR: Failed to acquire I2C mutex in begin");
            delete ioExpander;
            ioExpander = nullptr;
            return false;
        }
        initSuccess = ioExpander->begin(INPUT);
    }

    if (!initSuccess) {
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

// Configure pin mode
void TCA9535_GPIO::pinMode(uint16_t pin, uint8_t mode) {
    if (!initialized) {
        Serial.println("[TCA9535] ERROR: Not initialized - call begin() first");
        return;
    }

    uint8_t physPin = virtualToPhysical(pin);
    if (physPin == 0xFF) return;

    // Thread-safe I2C operation with RAII
    {
        SemaphoreLockGuard lock(d1l_i2c_mutex);
        if (!lock.isLocked()) {
            Serial.println("[TCA9535] ERROR: Failed to acquire I2C mutex in pinMode");
            return;
        }
        ioExpander->pinMode1(physPin, mode);
    }

    // Update direction cache
    if (mode == OUTPUT) {
        directionCache &= ~(1 << physPin);
    } else {
        directionCache |= (1 << physPin);
    }

    Serial.printf("[TCA9535] Pin %d (phys %d) set to %s\n",
                 pin, physPin, mode == OUTPUT ? "OUTPUT" : "INPUT");
}

// Write digital value to pin
void TCA9535_GPIO::digitalWrite(uint16_t pin, uint8_t value) {
    if (!initialized) {
        Serial.println("[TCA9535] ERROR: Not initialized - call begin() first");
        return;
    }

    uint8_t physPin = virtualToPhysical(pin);
    if (physPin == 0xFF) return;

    // Check cache - skip I2C write if state unchanged
    uint16_t pinMask = (1 << physPin);
    bool currentState = (outputCache & pinMask) != 0;
    bool newState = (value == HIGH);

    if (currentState == newState) {
        // Pin already in requested state, skip I2C transaction
        return;
    }

    // Thread-safe I2C operation with RAII
    bool writeSuccess = false;
    {
        SemaphoreLockGuard lock(d1l_i2c_mutex);
        if (!lock.isLocked()) {
            Serial.printf("[TCA9535] ERROR: Failed to acquire I2C mutex in digitalWrite (pin %u)\n", pin);
            return;
        }
        writeSuccess = ioExpander->write1(physPin, value);
    }

    if (!writeSuccess) {
        Serial.printf("[TCA9535] ERROR: I2C write failed on pin %u\n", pin);
        return;
    }

    // Update output cache only on successful write
    if (value == HIGH) {
        outputCache |= pinMask;
    } else {
        outputCache &= ~pinMask;
    }

    // Verbose logging disabled for performance
    // Serial.printf("[TCA9535] Pin %d (phys %d) = %s\n",
    //              pin, physPin, value == HIGH ? "HIGH" : "LOW");
}

// Read digital value from pin
uint8_t TCA9535_GPIO::digitalRead(uint16_t pin) {
    if (!initialized) {
        Serial.println("[TCA9535] ERROR: Not initialized - call begin() first");
        return LOW;
    }

    uint8_t physPin = virtualToPhysical(pin);
    if (physPin == 0xFF) return LOW;

    // Thread-safe I2C operation with RAII
    uint8_t value = LOW;
    {
        SemaphoreLockGuard lock(d1l_i2c_mutex);
        if (!lock.isLocked()) {
            Serial.printf("[TCA9535] ERROR: Failed to acquire I2C mutex in digitalRead (pin %u)\n", pin);
            return LOW; // Safe default
        }
        value = ioExpander->read1(physPin);
    }

    // Verbose logging disabled for performance
    // Serial.printf("[TCA9535] Pin %d (phys %d) read = %s\n",
    //              pin, physPin, value == HIGH ? "HIGH" : "LOW");

    return value;
}

// Read all input pins at once (both ports)
uint16_t TCA9535_GPIO::readAllInputs() {
    if (!initialized) {
        Serial.println("[TCA9535] ERROR: Not initialized - call begin() first");
        return 0x0000;
    }

    // Thread-safe I2C operation with RAII
    uint16_t allInputs = 0x0000;
    {
        SemaphoreLockGuard lock(d1l_i2c_mutex);
        if (!lock.isLocked()) {
            Serial.println("[TCA9535] ERROR: Failed to acquire I2C mutex in readAllInputs");
            return 0x0000; // Safe default (all LOW)
        }

        // Use read16() to read both ports in 2 I2C transactions (instead of 16)
        // read16() internally calls read8(port 0) and read8(port 1)
        // Returns: [P1_7..P1_0 | P0_7..P0_0] where bit 0 = P0_0, bit 15 = P1_7
        allInputs = ioExpander->read16();
    }

    // Verbose logging disabled for performance
    // Serial.printf("[TCA9535] readAllInputs: 0x%04X\n", allInputs);

    return allInputs;
}

// Check if initialized
bool TCA9535_GPIO::isInitialized() const {
    return initialized;
}

// Get cached output state
uint16_t TCA9535_GPIO::getOutputCache() const {
    return outputCache;
}

// Get cached direction state
uint16_t TCA9535_GPIO::getDirectionCache() const {
    return directionCache;
}

// Get I2C address
uint8_t TCA9535_GPIO::getAddress() const {
    return i2cAddress;
}

// Print status
void TCA9535_GPIO::printStatus() const {
    Serial.println("===== TCA9535 Status =====");
    Serial.printf("I2C Address: 0x%02X\n", i2cAddress);
    Serial.printf("Initialized: %s\n", initialized ? "YES" : "NO");
    Serial.printf("Output Cache: 0x%04X\n", outputCache);
    Serial.printf("Direction Cache: 0x%04X\n", directionCache);
    Serial.printf("Using global mutex: %s\n", d1l_i2c_mutex != NULL ? "YES" : "NO");

    Serial.println("\nLoRa Pin States:");
    Serial.printf("  NSS (%d): %s\n", LORA_NSS,
                 (outputCache & (1 << (LORA_NSS - 100))) ? "HIGH" : "LOW");
    Serial.printf("  RESET (%d): %s\n", LORA_RESET,
                 (outputCache & (1 << (LORA_RESET - 100))) ? "HIGH" : "LOW");
    Serial.println("==========================");
}

// Convert virtual pin to physical
uint8_t TCA9535_GPIO::virtualToPhysical(uint16_t virtualPin) {
    if (virtualPin >= 100 && virtualPin < 116) {
        return virtualPin - 100;
    }
    Serial.printf("[TCA9535] ERROR: Invalid virtual pin %u (valid range: 100-115)\n", virtualPin);
    return 0xFF; // Invalid
}
