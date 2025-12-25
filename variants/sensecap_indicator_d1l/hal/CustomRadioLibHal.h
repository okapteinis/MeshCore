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
 * Interrupt Handling:
 * - Uses FreeRTOS polling task (1ms interval) for virtual pin interrupts
 * - Future optimization: Could use TCA9535 hardware INT pin if connected
 * - Latency: ~1-2ms typical (acceptable for LoRa packet times)
 *
 * Usage Example:
 * ```cpp
 * TCA9535_GPIO ioExpander(0x20);
 * ioExpander.begin(&Wire);
 *
 * CustomRadioLibHal customHal(&ioExpander, SPI, spiSettings);
 * customHal.init();
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
 * @version 1.0.0
 * @date 2025-12-17
 */
class CustomRadioLibHal : public ArduinoHal {
private:
    TCA9535_GPIO* ioExpander;  // Pointer to TCA9535 GPIO wrapper
    bool initialized;          // Tracks if HAL is properly initialized

public:
    SemaphoreHandle_t i2cMutex; // Protects I2C operations for thread safety (shared with TCA9535_GPIO)

private:

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

    // FreeRTOS task handle for polling
    TaskHandle_t pollTaskHandle;

    /**
     * FreeRTOS task for polling virtual interrupts
     *
     * This task runs continuously at 1ms intervals, checking all registered
     * virtual interrupt pins for state changes. When a change matching the
     * interrupt mode is detected, the registered callback is invoked.
     *
     * @param parameter Pointer to CustomRadioLibHal instance
     */
    static void pollTask(void* parameter) {
        CustomRadioLibHal* hal = static_cast<CustomRadioLibHal*>(parameter);

        Serial.println("[CustomHAL] Polling task started (5ms interval)");

        while (true) {
            hal->pollVirtualInterruptsInternal();
            vTaskDelay(pdMS_TO_TICKS(5)); // Poll every 5ms (reduced bus congestion)
        }
    }

    /**
     * Internal polling function (called by task)
     *
     * Reads all registered virtual interrupt pins and triggers callbacks
     * when state changes match the configured interrupt mode.
     */
    void pollVirtualInterruptsInternal() {
        if (virtualInterruptCount == 0) return;

        for (int i = 0; i < MAX_VIRTUAL_INTERRUPTS; i++) {
            if (!virtualInterrupts[i].enabled) continue;

            // Read current state from TCA9535 (thread-safe with mutex)
            uint8_t currentState;
            {
                SemaphoreLockGuard lock(i2cMutex);
                currentState = ioExpander->digitalRead(virtualInterrupts[i].pin);
            }
            uint8_t lastState = virtualInterrupts[i].lastState;

            bool trigger = false;

            // Check if interrupt should fire based on mode
            if (virtualInterrupts[i].mode == RISING && lastState == LOW && currentState == HIGH) {
                trigger = true;
            } else if (virtualInterrupts[i].mode == FALLING && lastState == HIGH && currentState == LOW) {
                trigger = true;
            } else if (virtualInterrupts[i].mode == CHANGE && lastState != currentState) {
                trigger = true;
            }

            if (trigger) {
                Serial.printf("[CustomHAL] Virtual interrupt triggered on pin %d (%s)\n",
                            virtualInterrupts[i].pin,
                            currentState ? "HIGH" : "LOW");
                virtualInterrupts[i].callback();
            }

            virtualInterrupts[i].lastState = currentState;
        }
    }

public:
    /**
     * Constructor
     *
     * @param gpio Pointer to initialized TCA9535_GPIO instance
     * @param spi Reference to SPI class (for radio communication)
     * @param spiSettings SPI settings (clock, bit order, mode)
     */
    CustomRadioLibHal(TCA9535_GPIO* gpio, SPIClass& spi, SPISettings spiSettings)
        : ArduinoHal(spi, spiSettings),
          ioExpander(gpio),
          virtualInterruptCount(0),
          pollTaskHandle(NULL),
          initialized(false) {

        // Initialize virtual interrupt array
        for (int i = 0; i < MAX_VIRTUAL_INTERRUPTS; i++) {
            virtualInterrupts[i].enabled = false;
        }

        // Initialize I2C mutex for thread-safe operations
        i2cMutex = xSemaphoreCreateMutex();
        if (i2cMutex == NULL) {
            Serial.println("[CustomHAL] WARNING: Failed to create I2C mutex");
        } else {
            // Share the mutex with TCA9535_GPIO for synchronized I2C access
            ioExpander->setSharedMutex(&i2cMutex);
        }

        Serial.println("[CustomHAL] Created");
        Serial.println("[CustomHAL] Virtual pin range: 100-199 → TCA9535");
        Serial.println("[CustomHAL] Real pin range: 0-99 → ESP32 GPIO");
    }

    /**
     * Destructor
     *
     * Automatically cleans up resources when HAL object is destroyed.
     * Ensures polling task is stopped and mutex is deleted.
     */
    ~CustomRadioLibHal() {
        // Stop polling task if running
        if (pollTaskHandle != NULL) {
            vTaskDelete(pollTaskHandle);
            pollTaskHandle = NULL;
        }

        // Delete mutex if created
        if (i2cMutex != NULL) {
            vSemaphoreDelete(i2cMutex);
            i2cMutex = NULL;
        }

        Serial.println("[CustomHAL] Destructor called - resources cleaned up");
    }

    /**
     * Initialize HAL and start interrupt polling task (3KB stack)
     *
     * Must be called after constructor and before using radio.
     * Creates a FreeRTOS task for polling virtual interrupts.
     */
    void init() override {
        ArduinoHal::init();

        // Create FreeRTOS task for polling virtual interrupts
        BaseType_t result = xTaskCreate(
            pollTask,
            "TCA9535Poll",  // Task name
            3072,           // Stack size (bytes) - recommended minimum for I2C operations
            this,           // Task parameter (this HAL instance)
            2,              // Priority (higher than normal tasks)
            &pollTaskHandle
        );

        if (result == pdPASS) {
            initialized = true;
            Serial.println("[CustomHAL] Initialized successfully");
            Serial.println("[CustomHAL] Polling task started (priority 2, 5ms interval)");
        } else {
            initialized = false;
            Serial.println("[CustomHAL] FATAL ERROR: Failed to create polling task!");
            Serial.println("[CustomHAL] Possible causes:");
            Serial.println("[CustomHAL]   - Out of memory");
            Serial.println("[CustomHAL]   - Too many FreeRTOS tasks");
            Serial.println("[CustomHAL] Radio interrupts will NOT work!");
        }
    }

    /**
     * Cleanup HAL and stop polling task
     *
     * Called when radio is being shut down or reset.
     */
    void term() override {
        if (pollTaskHandle != NULL) {
            vTaskDelete(pollTaskHandle);
            pollTaskHandle = NULL;
            Serial.println("[CustomHAL] Polling task stopped");
        }

        if (i2cMutex != NULL) {
            vSemaphoreDelete(i2cMutex);
            i2cMutex = NULL;
            Serial.println("[CustomHAL] I2C mutex deleted");
        }

        initialized = false;
        ArduinoHal::term();
        Serial.println("[CustomHAL] Terminated");
    }

    /**
     * Override pinMode - route virtual pins through TCA9535
     *
     * Virtual pins (100-199) are routed to TCA9535 via I2C.
     * Real pins (0-99) are passed to standard ArduinoHal.
     *
     * @param pin Pin number (virtual or real)
     * @param mode INPUT, OUTPUT, INPUT_PULLUP, etc.
     */
    void pinMode(uint32_t pin, uint32_t mode) override {
        if (isVirtualPin(pin)) {
            // Verbose logging disabled for performance - uncomment for debugging:
            // Serial.printf("[CustomHAL] pinMode virtual pin %d mode %s\n",
            //              pin, mode == OUTPUT ? "OUTPUT" : "INPUT");
            SemaphoreLockGuard lock(i2cMutex);  // RAII guard
            ioExpander->pinMode(pin, mode);
        } else {
            ArduinoHal::pinMode(pin, mode);
        }
    }

    /**
     * Override digitalWrite - route virtual pins through TCA9535
     *
     * @param pin Pin number (virtual or real)
     * @param value HIGH or LOW
     */
    void digitalWrite(uint32_t pin, uint32_t value) override {
        if (isVirtualPin(pin)) {
            // Verbose logging disabled for performance - uncomment for debugging:
            // Serial.printf("[CustomHAL] digitalWrite virtual pin %d = %s\n",
            //              pin, value ? "HIGH" : "LOW");
            SemaphoreLockGuard lock(i2cMutex);  // RAII guard
            ioExpander->digitalWrite(pin, value);
        } else {
            ArduinoHal::digitalWrite(pin, value);
        }
    }

    /**
     * Override digitalRead - route virtual pins through TCA9535
     *
     * @param pin Pin number (virtual or real)
     * @return HIGH or LOW
     */
    uint32_t digitalRead(uint32_t pin) override {
        if (isVirtualPin(pin)) {
            SemaphoreLockGuard lock(i2cMutex);  // RAII guard
            uint32_t value = ioExpander->digitalRead(pin);
            // Verbose logging disabled for performance - uncomment for debugging:
            // Serial.printf("[CustomHAL] digitalRead virtual pin %d = %s\n",
            //              pin, value ? "HIGH" : "LOW");
            return value;
        } else {
            return ArduinoHal::digitalRead(pin);
        }
    }

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
    void attachInterrupt(uint32_t interruptNum, void (*interruptCb)(void), uint32_t mode) override {
        if (isVirtualPin(interruptNum)) {
            Serial.printf("[CustomHAL] attachInterrupt virtual pin %d mode %d\n",
                         interruptNum, mode);

            // Find empty slot
            for (int i = 0; i < MAX_VIRTUAL_INTERRUPTS; i++) {
                if (!virtualInterrupts[i].enabled) {
                    virtualInterrupts[i].pin = interruptNum;
                    virtualInterrupts[i].callback = interruptCb;
                    virtualInterrupts[i].mode = mode;
                    virtualInterrupts[i].enabled = true;
                    virtualInterrupts[i].lastState = ioExpander->digitalRead(interruptNum);
                    virtualInterruptCount++;

                    // Configure pin as input
                    ioExpander->pinMode(interruptNum, INPUT);

                    Serial.printf("[CustomHAL] Virtual interrupt registered (slot %d, initial state: %s)\n",
                                 i, virtualInterrupts[i].lastState ? "HIGH" : "LOW");
                    return;
                }
            }

            Serial.printf("[CustomHAL] ERROR: No free virtual interrupt slots (max %d)!\n",
                         MAX_VIRTUAL_INTERRUPTS);
        } else {
            Serial.printf("[CustomHAL] attachInterrupt real pin %d\n", interruptNum);
            ArduinoHal::attachInterrupt(interruptNum, interruptCb, mode);
        }
    }

    /**
     * Override detachInterrupt
     *
     * @param interruptNum Pin number to detach
     */
    void detachInterrupt(uint32_t interruptNum) override {
        if (isVirtualPin(interruptNum)) {
            Serial.printf("[CustomHAL] detachInterrupt virtual pin %d\n", interruptNum);

            for (int i = 0; i < MAX_VIRTUAL_INTERRUPTS; i++) {
                if (virtualInterrupts[i].enabled && virtualInterrupts[i].pin == interruptNum) {
                    virtualInterrupts[i].enabled = false;
                    virtualInterruptCount--;
                    Serial.printf("[CustomHAL] Virtual interrupt detached (slot %d)\n", i);
                    return;
                }
            }
        } else {
            ArduinoHal::detachInterrupt(interruptNum);
        }
    }

    /**
     * Public method to manually poll interrupts
     *
     * Optional - the FreeRTOS task does this automatically.
     * Can be called from main loop as additional safety.
     */
    void pollVirtualInterrupts() {
        pollVirtualInterruptsInternal();
    }

    /**
     * Get statistics about virtual interrupts
     *
     * @return Number of active virtual interrupts
     */
    int getVirtualInterruptCount() const {
        return virtualInterruptCount;
    }

    /**
     * Print HAL status for debugging
     */
    void printStatus() const {
        Serial.println("===== CustomRadioLibHal Status =====");
        Serial.printf("Virtual Interrupts: %d / %d active\n",
                     virtualInterruptCount, MAX_VIRTUAL_INTERRUPTS);

        for (int i = 0; i < MAX_VIRTUAL_INTERRUPTS; i++) {
            if (virtualInterrupts[i].enabled) {
                Serial.printf("  Slot %d: Pin %d, Mode %d, State %d\n",
                             i, virtualInterrupts[i].pin,
                             virtualInterrupts[i].mode,
                             virtualInterrupts[i].lastState);
            }
        }

        Serial.printf("Polling Task: %s\n", pollTaskHandle != NULL ? "Running" : "Stopped");
        Serial.println("====================================");
    }

    /**
     * Check if HAL is properly initialized
     *
     * @return true if polling task is running, false otherwise
     */
    bool isInitialized() const {
        return initialized;
    }

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
