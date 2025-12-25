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
    if (virtualInterruptCount == 0) return;

    for (int i = 0; i < MAX_VIRTUAL_INTERRUPTS; i++) {
        if (!virtualInterrupts[i].enabled) continue;

        // Read current state from TCA9535 with RAII mutex protection
        uint8_t currentState;
        {
            SemaphoreLockGuard lock(d1l_i2c_mutex);
            if (!lock.isLocked()) {
                Serial.println("[CustomHAL] ERROR: Failed to acquire I2C mutex in polling task");
                continue; // Skip this interrupt check
            }
            currentState = ioExpander->digitalRead(virtualInterrupts[i].pin);
        } // Mutex automatically released here

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

    // Delete mutex if created
    if (d1l_i2c_mutex != NULL) {
        vSemaphoreDelete(d1l_i2c_mutex);
        d1l_i2c_mutex = NULL;
    }

    Serial.println("[CustomHAL] Destructor called - resources cleaned up");
}

// Initialize HAL and start polling task
void CustomRadioLibHal::init() {
    ArduinoHal::init();

    // Verify mutex was created
    if (d1l_i2c_mutex == NULL) {
        Serial.println("[CustomHAL] FATAL: Cannot init - mutex not created!");
        initialized = false;
        return;
    }

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

// Cleanup HAL and stop polling task
void CustomRadioLibHal::term() {
    if (pollTaskHandle != NULL) {
        vTaskDelete(pollTaskHandle);
        pollTaskHandle = NULL;
        Serial.println("[CustomHAL] Polling task stopped");
    }

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

// detachInterrupt override
void CustomRadioLibHal::detachInterrupt(uint32_t interruptNum) {
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

// Public method to manually poll interrupts
void CustomRadioLibHal::pollVirtualInterrupts() {
    pollVirtualInterruptsInternal();
}

// Get statistics
int CustomRadioLibHal::getVirtualInterruptCount() const {
    return virtualInterruptCount;
}

// Print status
void CustomRadioLibHal::printStatus() const {
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
    Serial.printf("I2C Mutex: %s\n", d1l_i2c_mutex != NULL ? "Created" : "NULL");
    Serial.println("====================================");
}

// Check initialization status
bool CustomRadioLibHal::isInitialized() const {
    return initialized;
}
