#ifndef CUSTOM_RADIOLIB_HAL_H
#define CUSTOM_RADIOLIB_HAL_H

#include <RadioLib.h>
#include "TCA9535_GPIO.h"
#include "freertos_util.h"

/**
 * Custom RadioLib HAL for TCA9535 I/O Expander
 *
 * This HAL intercepts GPIO operations and routes virtual pins (100-199)
 * through TCA9535 I2C I/O expander while passing real ESP32 pins (0-99)
 * to standard ArduinoHal implementation.
 *
 * REQUIRES: RadioLib compiled with RADIOLIB_GODMODE=1
 *
 * Architecture:
 * ┌──────────────────────────────────────────────────────────┐
 * │              CustomRadioLibHal                           │
 * ├──────────────────────────────────────────────────────────┤
 * │                                                          │
 * │  pinMode(pin, mode)                                      │
 * │  ┌────────────────┐                                      │
 * │  │ Is pin virtual │                                      │
 * │  │ (100-199)?     │                                      │
 * │  └───┬────────┬───┘                                      │
 * │      │ YES    │ NO                                       │
 * │      ▼        ▼                                          │
 * │  ┌───────┐  ┌────────────┐                              │
 * │  │ TCA   │  │ ArduinoHal │                              │
 * │  │ 9535  │  │ (ESP32     │                              │
 * │  │ I2C   │  │  GPIO)     │                              │
 * │  └───────┘  └────────────┘                              │
 * │                                                          │
 * └──────────────────────────────────────────────────────────┘
 *
 * Thread Safety:
 * - Uses global mutex d1l_i2c_mutex (declared in .cpp) for I2C synchronization
 * - CustomRadioLibHal constructor initializes the mutex BEFORE TCA9535_GPIO
 * - Both pollingTask and TCA9535_GPIO use the SAME mutex instance
 *
 * Interrupt Handling:
 * - Uses FreeRTOS polling task (5ms interval) for virtual pin interrupts
 * - Future optimization: Could use TCA9535 hardware INT pin if connected
 * - Latency: ~5-10ms typical (acceptable for LoRa packet times)
 *
 * Usage Example:
 * ```cpp
 * TCA9535_GPIO ioExpander(0x20);
 * CustomRadioLibHal customHal(&ioExpander, SPI, spiSettings);
 * customHal.init();  // MUST call init() to create mutex and start polling
 *
 * ioExpander.begin(&Wire);  // Now safe - mutex is created
 *
 * SX1262 radio = new Module(
 *     100,  // NSS (virtual pin)
 *     103,  // DIO1 (virtual pin)
 *     101,  // RESET (virtual pin)
 *     102,  // BUSY (virtual pin)
 *     customHal
 * );
 * ```
 *
 * @author MeshCore Community
 * @version 2.0.0
 * @date 2025-12-25
 */
class CustomRadioLibHal : public ArduinoHal {
private:
    TCA9535_GPIO* ioExpander;  // Pointer to TCA9535 GPIO wrapper
    bool initialized;          // Tracks if HAL is properly initialized

#ifdef USE_TCA9535_INT_PIN
    // Friend declarations for static helper functions
    friend void intHandlerTask(void* parameter);
#endif

    /**
     * Determine if pin number is virtual (managed by TCA9535)
     *
     * Pin Range Classification:
     * - 0-99: Real ESP32 GPIO pins (pass through to ArduinoHal)
     * - 100-199: Virtual pins (route through TCA9535)
     * - 200+: Invalid
     *
     * @param pin Pin number to check
     * @return true if virtual pin, false if real GPIO
     */
    bool isVirtualPin(uint32_t pin) const {
        return (pin >= 100 && pin <= 199);
    }

    /**
     * Virtual interrupt storage structure
     *
     * Since TCA9535 pins cannot generate hardware interrupts directly,
     * we store callback information and poll for changes.
     */
    struct VirtualInterrupt {
        uint32_t pin;           // Virtual pin number
        void (*callback)(void); // Callback function
        uint32_t mode;          // Interrupt mode (RISING, FALLING, CHANGE)
        bool enabled;           // Is this slot active?
        uint8_t lastState;      // Last known pin state
    };

    static const int MAX_VIRTUAL_INTERRUPTS = 4;  // Support up to 4 interrupts
    VirtualInterrupt virtualInterrupts[MAX_VIRTUAL_INTERRUPTS];
    int virtualInterruptCount;

    // FreeRTOS task handle for polling (or INT handler in INT mode)
    TaskHandle_t pollTaskHandle;

#ifdef USE_TCA9535_INT_PIN
    // INT pin optimization is enabled (event-driven mode)
    // Task handle is stored in static variable at file scope (see .cpp)
#endif

    /**
     * FreeRTOS task for polling virtual interrupts
     *
     * This task runs continuously at 5ms intervals, checking all registered
     * virtual interrupt pins for state changes. When a change matching the
     * interrupt mode is detected, the registered callback is invoked.
     *
     * THREAD-SAFE: Uses global d1l_i2c_mutex for I2C synchronization
     *
     * @param parameter Pointer to CustomRadioLibHal instance
     */
    static void pollTask(void* parameter);

    /**
     * Internal polling function (called by task)
     *
     * Reads all registered virtual interrupt pins and triggers callbacks
     * when state changes match the configured interrupt mode.
     *
     * THREAD-SAFE: Uses global d1l_i2c_mutex for I2C read operations
     */
    void pollVirtualInterruptsInternal();

    /**
     * Process virtual interrupts from pin state snapshot
     *
     * Common logic shared by both polling mode and INT-driven mode.
     * Checks all registered virtual interrupts against the provided pin states
     * and fires callbacks when state changes match interrupt modes.
     *
     * @param inputState 16-bit snapshot of all TCA9535 input pins
     */
    void processVirtualInterrupts(uint16_t inputState);

public:
    /**
     * Constructor
     *
     * CRITICAL: This constructor initializes the global d1l_i2c_mutex.
     * It MUST be called BEFORE any TCA9535_GPIO::begin() calls.
     *
     * @param gpio Pointer to initialized TCA9535_GPIO instance
     * @param spi Reference to SPI class (for radio communication)
     * @param spiSettings SPI settings (clock, bit order, mode)
     */
    CustomRadioLibHal(TCA9535_GPIO* gpio, SPIClass& spi, SPISettings spiSettings);

    /**
     * Destructor
     *
     * Automatically cleans up resources when HAL object is destroyed.
     * Ensures polling task is stopped and global mutex is deleted.
     */
    ~CustomRadioLibHal();

    /**
     * Initialize HAL and start interrupt polling task
     *
     * Must be called after constructor and before using radio.
     * Creates a FreeRTOS task for polling virtual interrupts.
     * Verifies global mutex was created successfully.
     */
    void init() override;

    /**
     * Cleanup HAL and stop polling task
     *
     * Called when radio is being shut down or reset.
     * Deletes global mutex and stops polling task.
     */
    void term() override;

    /**
     * Override pinMode - route virtual pins through TCA9535
     *
     * Virtual pins (100-199) are routed to TCA9535 via I2C.
     * Real pins (0-99) are passed to standard ArduinoHal.
     *
     * @param pin Pin number (virtual or real)
     * @param mode INPUT, OUTPUT, INPUT_PULLUP, etc.
     */
    void pinMode(uint32_t pin, uint32_t mode) override;

    /**
     * Override digitalWrite - route virtual pins through TCA9535
     *
     * @param pin Pin number (virtual or real)
     * @param value HIGH or LOW
     */
    void digitalWrite(uint32_t pin, uint32_t value) override;

    /**
     * Override digitalRead - route virtual pins through TCA9535
     *
     * @param pin Pin number (virtual or real)
     * @return HIGH or LOW
     */
    uint32_t digitalRead(uint32_t pin) override;

    /**
     * Override attachInterrupt - store callbacks for polling
     *
     * For virtual pins, stores the callback and begins polling.
     * For real pins, passes through to hardware interrupt system.
     *
     * @param interruptNum Pin number (or interrupt number)
     * @param interruptCb Callback function to call when interrupt fires
     * @param mode RISING, FALLING, or CHANGE
     */
    void attachInterrupt(uint32_t interruptNum, void (*interruptCb)(void), uint32_t mode) override;

    /**
     * Override detachInterrupt
     *
     * @param interruptNum Pin number to detach
     */
    void detachInterrupt(uint32_t interruptNum) override;

    /**
     * Public method to manually poll interrupts
     *
     * Optional - the FreeRTOS task does this automatically.
     * Can be called from main loop as additional safety.
     */
    void pollVirtualInterrupts();

    /**
     * Get statistics about virtual interrupts
     *
     * @return Number of active virtual interrupts
     */
    int getVirtualInterruptCount() const;

    /**
     * Print HAL status for debugging
     */
    void printStatus() const;

    /**
     * Check if HAL is properly initialized
     *
     * @return true if polling task is running, false otherwise
     */
    bool isInitialized() const;

    // All other HAL methods pass through to ArduinoHal base class:
    // - delay(), delayMicroseconds()
    // - millis(), micros()
    // - spiBegin(), spiTransfer(), spiEnd()
    // - tone(), noTone()
    // - yield()
    //
    // These work normally without modification.
};

#endif // CUSTOM_RADIOLIB_HAL_H
