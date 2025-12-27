#include "CustomRadioLibHal.h"
#include "freertos_util.h"

/**
 * Global I2C mutex shared between CustomRadioLibHal and TCA9535_GPIO
 *
 * This mutex MUST be initialized in CustomRadioLibHal constructor BEFORE
 * any TCA9535_GPIO instance is created. Both classes use this same handle
 * to synchronize access to the TCA9535 I2C expander.
 *
 * CRITICAL: This prevents race conditions between:
 * - FreeRTOS polling task (reading DIO1 interrupts)
 * - Main RadioLib thread (reading BUSY/input pins)
 */
SemaphoreHandle_t d1l_i2c_mutex = NULL;

/**
 * Virtual interrupt array mutex
 *
 * Protects the virtualInterrupts[] array from race conditions between:
 * - Polling task reading interrupt states (pollVirtualInterruptsInternal)
 * - Main thread modifying interrupt handlers (attachInterrupt/detachInterrupt)
 *
 * This is SEPARATE from d1l_i2c_mutex to prevent deadlocks and allow
 * fine-grained locking (I2C operations can happen without blocking interrupt setup)
 */
static SemaphoreHandle_t virtual_int_mutex = NULL;

#ifdef USE_TCA9535_INT_PIN
/**
 * INT Pin Optimization (Event-Driven Mode)
 *
 * When USE_TCA9535_INT_PIN is defined, the HAL uses hardware interrupt on GPIO 42
 * instead of 5ms polling. This reduces I2C bus load by 95-99%.
 */

// GPIO pin connected to TCA9535 INT output
#define TCA9535_INT_PIN 42

// Task handle for INT handler task (must be static for ISR access)
static TaskHandle_t intHandlerTaskHandle = NULL;

/**
 * ESP32 Hardware ISR for TCA9535 INT pin
 *
 * Fires when TCA9535 INT pin goes LOW (any input pin changed state).
 * Cannot do I2C operations here (FreeRTOS restriction), so we signal
 * a deferred task via task notification.
 *
 * IRAM_ATTR: ISR must be in instruction RAM for fast execution
 */
static void IRAM_ATTR tca9535IntPinISR(void) {
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    // Signal deferred task via direct-to-task notification
    if (intHandlerTaskHandle != NULL) {
        vTaskNotifyGiveFromISR(intHandlerTaskHandle, &xHigherPriorityTaskWoken);
    }

    // Yield to INT handler task if it has higher priority
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

/**
 * Deferred INT Handler Task
 *
 * Woken by ISR via task notification when TCA9535 INT fires.
 * Reads TCA9535 input registers (auto-clears INT) and processes
 * virtual interrupts.
 *
 * NOTE: Not static - declared as friend in CustomRadioLibHal class
 */
void intHandlerTask(void* parameter) {
    CustomRadioLibHal* hal = static_cast<CustomRadioLibHal*>(parameter);

    Serial.println("[CustomHAL] INT handler task started (event-driven mode)");
    Serial.printf("[CustomHAL] Using GPIO %d for TCA9535 INT pin\n", TCA9535_INT_PIN);

    while (true) {
        // Block until INT fires (event-driven, no periodic polling)
        // ulTaskNotifyTake clears notification count and returns notifications received
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        // Read TCA9535 input registers (this auto-clears the INT pin)
        uint16_t inputState;
        {
            SemaphoreLockGuard lock(d1l_i2c_mutex);
            if (!lock.isLocked()) {
                Serial.println("[CustomHAL] ERROR: Failed to acquire I2C mutex in INT handler");
                continue;
            }
            inputState = hal->ioExpander->readAllInputs();
        }

        // Process all registered virtual interrupts with snapshot
        hal->processVirtualInterrupts(inputState);

        // Check if INT is still LOW (multiple pin changes during processing)
        if (digitalRead(TCA9535_INT_PIN) == LOW) {
            Serial.println("[CustomHAL] INT still LOW after processing - re-triggering");
            // Re-read inputs to catch additional changes
            {
                SemaphoreLockGuard lock(d1l_i2c_mutex);
                if (lock.isLocked()) {
                    inputState = hal->ioExpander->readAllInputs();
                    hal->processVirtualInterrupts(inputState);
                }
            }
        }
    }
}

#endif // USE_TCA9535_INT_PIN

// Static task function for polling virtual interrupts
void CustomRadioLibHal::pollTask(void* parameter) {
    CustomRadioLibHal* hal = static_cast<CustomRadioLibHal*>(parameter);

    Serial.println("[CustomHAL] Polling task started (5ms interval)");

    while (true) {
        hal->pollVirtualInterruptsInternal();
        vTaskDelay(pdMS_TO_TICKS(5)); // Poll every 5ms (reduced bus congestion)
    }
}

// Internal polling function
void CustomRadioLibHal::pollVirtualInterruptsInternal() {
    // Quick check without mutex (safe - virtualInterruptCount changes are atomic enough for this use)
    if (virtualInterruptCount == 0) return;

    for (int i = 0; i < MAX_VIRTUAL_INTERRUPTS; i++) {
        // Snapshot interrupt data while holding mutex (minimize critical section)
        uint32_t pin;
        uint32_t mode;
        uint8_t lastState;
        void (*callback)(void);
        bool enabled;

        {
            SemaphoreLockGuard lock(virtual_int_mutex);
            if (!lock.isLocked()) {
                Serial.println("[CustomHAL] ERROR: Failed to acquire virtual interrupt mutex in polling task");
                continue;
            }

            // Snapshot all needed data
            enabled = virtualInterrupts[i].enabled;
            if (!enabled) continue; // Skip disabled interrupts

            pin = virtualInterrupts[i].pin;
            mode = virtualInterrupts[i].mode;
            lastState = virtualInterrupts[i].lastState;
            callback = virtualInterrupts[i].callback;
        } // Release virtual_int_mutex before I2C access

        // Read current state from TCA9535 with I2C mutex protection
        uint8_t currentState;
        {
            SemaphoreLockGuard lock(d1l_i2c_mutex);
            if (!lock.isLocked()) {
                Serial.println("[CustomHAL] ERROR: Failed to acquire I2C mutex in polling task");
                continue; // Skip this interrupt check
            }
            currentState = ioExpander->digitalRead(pin);
        } // Release I2C mutex

        // Check if interrupt should fire based on mode
        bool trigger = false;
        if (mode == RISING && lastState == LOW && currentState == HIGH) {
            trigger = true;
        } else if (mode == FALLING && lastState == HIGH && currentState == LOW) {
            trigger = true;
        } else if (mode == CHANGE && lastState != currentState) {
            trigger = true;
        }

        // Fire callback OUTSIDE of mutex (callbacks may take time)
        if (trigger) {
            Serial.printf("[CustomHAL] Virtual interrupt triggered on pin %d (%s)\n",
                        pin, currentState ? "HIGH" : "LOW");
            callback();
        }

        // Update lastState with mutex protection
        {
            SemaphoreLockGuard lock(virtual_int_mutex);
            if (lock.isLocked() && virtualInterrupts[i].enabled && virtualInterrupts[i].pin == pin) {
                // Verify interrupt wasn't detached while we were running
                virtualInterrupts[i].lastState = currentState;
            }
        }
    }
}

// Process virtual interrupts from pin state snapshot
void CustomRadioLibHal::processVirtualInterrupts(uint16_t inputState) {
    // Quick check without mutex (safe - virtualInterruptCount changes are atomic enough for this use)
    if (virtualInterruptCount == 0) return;

    for (int i = 0; i < MAX_VIRTUAL_INTERRUPTS; i++) {
        // Snapshot interrupt data while holding mutex (minimize critical section)
        uint32_t pin;
        uint32_t mode;
        uint8_t lastState;
        void (*callback)(void);
        bool enabled;

        {
            SemaphoreLockGuard lock(virtual_int_mutex);
            if (!lock.isLocked()) {
                Serial.println("[CustomHAL] ERROR: Failed to acquire virtual interrupt mutex in processVirtualInterrupts");
                continue;
            }

            // Snapshot all needed data
            enabled = virtualInterrupts[i].enabled;
            if (!enabled) continue; // Skip disabled interrupts

            pin = virtualInterrupts[i].pin;
            mode = virtualInterrupts[i].mode;
            lastState = virtualInterrupts[i].lastState;
            callback = virtualInterrupts[i].callback;
        } // Release virtual_int_mutex

        // Extract current pin state from snapshot (pin is physical pin number 0-15)
        // Virtual pins 100-115 map to physical pins 0-15
        uint8_t physPin = (pin >= 100 && pin <= 115) ? (pin - 100) : 0xFF;
        if (physPin == 0xFF) {
            Serial.printf("[CustomHAL] ERROR: Invalid virtual pin %d in processVirtualInterrupts\n", pin);
            continue;
        }

        // Extract current state from inputState bitmap
        uint8_t currentState = (inputState & (1 << physPin)) ? HIGH : LOW;

        // Check if interrupt should fire based on mode
        bool trigger = false;
        if (mode == RISING && lastState == LOW && currentState == HIGH) {
            trigger = true;
        } else if (mode == FALLING && lastState == HIGH && currentState == LOW) {
            trigger = true;
        } else if (mode == CHANGE && lastState != currentState) {
            trigger = true;
        }

        // Fire callback OUTSIDE of mutex (callbacks may take time)
        if (trigger) {
            Serial.printf("[CustomHAL] Virtual interrupt triggered on pin %d (%s)\n",
                        pin, currentState ? "HIGH" : "LOW");
            callback();
        }

        // Update lastState with mutex protection
        {
            SemaphoreLockGuard lock(virtual_int_mutex);
            if (lock.isLocked() && virtualInterrupts[i].enabled && virtualInterrupts[i].pin == pin) {
                // Verify interrupt wasn't detached while we were running
                virtualInterrupts[i].lastState = currentState;
            }
        }
    }
}

// Constructor
CustomRadioLibHal::CustomRadioLibHal(TCA9535_GPIO* gpio, SPIClass& spi, SPISettings spiSettings)
    : ArduinoHal(spi, spiSettings),
      ioExpander(gpio),
      virtualInterruptCount(0),
      pollTaskHandle(NULL),
      initialized(false) {

    // Initialize virtual interrupt array
    for (int i = 0; i < MAX_VIRTUAL_INTERRUPTS; i++) {
        virtualInterrupts[i].enabled = false;
    }

    // CRITICAL: Initialize global I2C mutex FIRST
    // This MUST happen before any TCA9535_GPIO operations
    d1l_i2c_mutex = xSemaphoreCreateMutex();
    if (d1l_i2c_mutex == NULL) {
        Serial.println("[CustomHAL] FATAL: Failed to create I2C mutex");
        Serial.println("[CustomHAL] System cannot guarantee thread-safety!");
    } else {
        Serial.println("[CustomHAL] Global I2C mutex created successfully");
    }

    // Initialize virtual interrupt mutex
    virtual_int_mutex = xSemaphoreCreateMutex();
    if (virtual_int_mutex == NULL) {
        Serial.println("[CustomHAL] FATAL: Failed to create virtual interrupt mutex");
        Serial.println("[CustomHAL] Virtual interrupts will NOT be thread-safe!");
    } else {
        Serial.println("[CustomHAL] Virtual interrupt mutex created successfully");
    }

    Serial.println("[CustomHAL] Created");
    Serial.println("[CustomHAL] Virtual pin range: 100-199 → TCA9535");
    Serial.println("[CustomHAL] Real pin range: 0-99 → ESP32 GPIO");
}

// Destructor
CustomRadioLibHal::~CustomRadioLibHal() {
    // Stop polling task if running
    if (pollTaskHandle != NULL) {
        vTaskDelete(pollTaskHandle);
        pollTaskHandle = NULL;
    }

    // Delete mutexes if created
    if (d1l_i2c_mutex != NULL) {
        vSemaphoreDelete(d1l_i2c_mutex);
        d1l_i2c_mutex = NULL;
    }

    if (virtual_int_mutex != NULL) {
        vSemaphoreDelete(virtual_int_mutex);
        virtual_int_mutex = NULL;
    }

    Serial.println("[CustomHAL] Destructor called - resources cleaned up");
}

// Initialize HAL and start interrupt handler (polling or INT-driven)
void CustomRadioLibHal::init() {
    ArduinoHal::init();

    // Verify mutex was created
    if (d1l_i2c_mutex == NULL) {
        Serial.println("[CustomHAL] FATAL: Cannot init - mutex not created!");
        initialized = false;
        return;
    }

#ifdef USE_TCA9535_INT_PIN
    // ========== INT Pin Optimization Enabled ==========
    Serial.println("[CustomHAL] INT pin optimization enabled");
    Serial.printf("[CustomHAL] TCA9535 INT pin: GPIO %d\n", TCA9535_INT_PIN);

    // Configure GPIO 42 as input with internal pull-up
    pinMode(TCA9535_INT_PIN, INPUT_PULLUP);

    // Create FreeRTOS task for INT-driven interrupt handling
    BaseType_t result = xTaskCreate(
        intHandlerTask,
        "TCA9535INT",  // Task name
        3072,          // Stack size (bytes) - minimum for I2C operations
        this,          // Task parameter (this HAL instance)
        3,             // Priority (higher than polling to reduce latency)
        &intHandlerTaskHandle
    );

    if (result != pdPASS) {
        initialized = false;
        Serial.println("[CustomHAL] FATAL ERROR: Failed to create INT handler task!");
        Serial.println("[CustomHAL] Possible causes:");
        Serial.println("[CustomHAL]   - Out of memory");
        Serial.println("[CustomHAL]   - Too many FreeRTOS tasks");
        Serial.println("[CustomHAL] Radio interrupts will NOT work!");
        return;
    }

    // Attach ESP32 hardware ISR to INT pin (FALLING edge)
    attachInterrupt(digitalPinToInterrupt(TCA9535_INT_PIN), tca9535IntPinISR, FALLING);

    initialized = true;
    Serial.println("[CustomHAL] Initialized successfully (INT-driven mode)");
    Serial.println("[CustomHAL] INT handler task started (priority 3, event-driven)");
    Serial.println("[CustomHAL] Expected I2C load reduction: 95-99%");

#else
    // ========== Polling Mode (Fallback) ==========
    Serial.println("[CustomHAL] Using polling mode (5ms interval)");

    // Create FreeRTOS task for polling virtual interrupts
    BaseType_t result = xTaskCreate(
        pollTask,
        "TCA9535Poll",  // Task name
        3072,           // Stack size (bytes) - minimum for I2C operations
        this,           // Task parameter (this HAL instance)
        2,              // Priority (higher than normal tasks)
        &pollTaskHandle
    );

    if (result == pdPASS) {
        initialized = true;
        Serial.println("[CustomHAL] Initialized successfully (polling mode)");
        Serial.println("[CustomHAL] Polling task started (priority 2, 5ms interval)");
    } else {
        initialized = false;
        Serial.println("[CustomHAL] FATAL ERROR: Failed to create polling task!");
        Serial.println("[CustomHAL] Possible causes:");
        Serial.println("[CustomHAL]   - Out of memory");
        Serial.println("[CustomHAL]   - Too many FreeRTOS tasks");
        Serial.println("[CustomHAL] Radio interrupts will NOT work!");
    }
#endif
}

// Cleanup HAL and stop interrupt handler (polling or INT-driven)
void CustomRadioLibHal::term() {
#ifdef USE_TCA9535_INT_PIN
    // Detach ESP32 hardware ISR
    detachInterrupt(digitalPinToInterrupt(TCA9535_INT_PIN));
    Serial.printf("[CustomHAL] Detached ISR from GPIO %d\n", TCA9535_INT_PIN);

    // Delete INT handler task
    if (intHandlerTaskHandle != NULL) {
        vTaskDelete(intHandlerTaskHandle);
        intHandlerTaskHandle = NULL;
        Serial.println("[CustomHAL] INT handler task stopped");
    }
#else
    // Delete polling task
    if (pollTaskHandle != NULL) {
        vTaskDelete(pollTaskHandle);
        pollTaskHandle = NULL;
        Serial.println("[CustomHAL] Polling task stopped");
    }
#endif

    // The global d1l_i2c_mutex is NOT deleted here because other
    // components (TCA9535_GPIO) may still need it after radio termination.
    // Mutex lifecycle is managed by constructor/destructor only.

    initialized = false;
    ArduinoHal::term();
    Serial.println("[CustomHAL] Terminated");
}

// pinMode override
void CustomRadioLibHal::pinMode(uint32_t pin, uint32_t mode) {
    if (isVirtualPin(pin)) {
        ioExpander->pinMode(pin, mode);
    } else {
        ArduinoHal::pinMode(pin, mode);
    }
}

// digitalWrite override
void CustomRadioLibHal::digitalWrite(uint32_t pin, uint32_t value) {
    if (isVirtualPin(pin)) {
        ioExpander->digitalWrite(pin, value);
    } else {
        ArduinoHal::digitalWrite(pin, value);
    }
}

// digitalRead override
uint32_t CustomRadioLibHal::digitalRead(uint32_t pin) {
    if (isVirtualPin(pin)) {
        return ioExpander->digitalRead(pin);
    } else {
        return ArduinoHal::digitalRead(pin);
    }
}

// attachInterrupt override
void CustomRadioLibHal::attachInterrupt(uint32_t interruptNum, void (*interruptCb)(void), uint32_t mode) {
    if (isVirtualPin(interruptNum)) {
        Serial.printf("[CustomHAL] attachInterrupt virtual pin %d mode %d\n",
                     interruptNum, mode);

        // Read initial pin state (outside of virtual_int_mutex to avoid nested locking)
        uint8_t initialState;
        {
            SemaphoreLockGuard lock(d1l_i2c_mutex);
            if (!lock.isLocked()) {
                Serial.println("[CustomHAL] ERROR: Failed to acquire I2C mutex for initial state read");
                return;
            }
            initialState = ioExpander->digitalRead(interruptNum);
            // Also configure pin as input while we have I2C mutex
            ioExpander->pinMode(interruptNum, INPUT);
        }

        // Now modify virtualInterrupts array with mutex protection
        SemaphoreLockGuard lock(virtual_int_mutex);
        if (!lock.isLocked()) {
            Serial.println("[CustomHAL] ERROR: Failed to acquire virtual interrupt mutex in attachInterrupt");
            return;
        }

        // Find empty slot
        for (int i = 0; i < MAX_VIRTUAL_INTERRUPTS; i++) {
            if (!virtualInterrupts[i].enabled) {
                virtualInterrupts[i].pin = interruptNum;
                virtualInterrupts[i].callback = interruptCb;
                virtualInterrupts[i].mode = mode;
                virtualInterrupts[i].lastState = initialState;
                virtualInterrupts[i].enabled = true;  // Set enabled LAST (atomic publish)
                virtualInterruptCount++;

                Serial.printf("[CustomHAL] Virtual interrupt registered (slot %d, initial state: %s)\n",
                             i, initialState ? "HIGH" : "LOW");
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

// detachInterrupt override
void CustomRadioLibHal::detachInterrupt(uint32_t interruptNum) {
    if (isVirtualPin(interruptNum)) {
        Serial.printf("[CustomHAL] detachInterrupt virtual pin %d\n", interruptNum);

        SemaphoreLockGuard lock(virtual_int_mutex);
        if (!lock.isLocked()) {
            Serial.println("[CustomHAL] ERROR: Failed to acquire virtual interrupt mutex in detachInterrupt");
            return;
        }

        for (int i = 0; i < MAX_VIRTUAL_INTERRUPTS; i++) {
            if (virtualInterrupts[i].enabled && virtualInterrupts[i].pin == interruptNum) {
                virtualInterrupts[i].enabled = false;  // Disable FIRST (atomic unpublish)
                virtualInterruptCount--;
                Serial.printf("[CustomHAL] Virtual interrupt detached (slot %d)\n", i);
                return;
            }
        }
    } else {
        ArduinoHal::detachInterrupt(interruptNum);
    }
}

// Public method to manually poll interrupts
void CustomRadioLibHal::pollVirtualInterrupts() {
    pollVirtualInterruptsInternal();
}

// Get statistics
int CustomRadioLibHal::getVirtualInterruptCount() const {
    SemaphoreLockGuard lock(virtual_int_mutex);
    return virtualInterruptCount;
}

// Print status
void CustomRadioLibHal::printStatus() const {
    Serial.println("===== CustomRadioLibHal Status =====");

    {
        SemaphoreLockGuard lock(virtual_int_mutex);
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
    }

    Serial.printf("Polling Task: %s\n", pollTaskHandle != NULL ? "Running" : "Stopped");
    Serial.printf("I2C Mutex: %s\n", d1l_i2c_mutex != NULL ? "Created" : "NULL");
    Serial.printf("Virtual Interrupt Mutex: %s\n", virtual_int_mutex != NULL ? "Created" : "NULL");
    Serial.println("====================================");
}

// Check initialization status
bool CustomRadioLibHal::isInitialized() const {
    return initialized;
}
