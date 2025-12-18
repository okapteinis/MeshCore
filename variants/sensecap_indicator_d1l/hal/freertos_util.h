#ifndef FREERTOS_UTIL_H
#define FREERTOS_UTIL_H

#include <freertos/semphr.h>

/**
 * RAII Lock Guard for FreeRTOS Semaphore
 *
 * Automatically acquires semaphore on construction and releases on destruction.
 * Ensures mutex is always released even on early returns or exceptions.
 *
 * THREAD-SAFE: Stores semaphore handle by value to prevent issues if original
 * handle is modified during guard lifetime.
 *
 * SAFE ACQUISITION: Verifies xSemaphoreTake succeeded before marking as locked.
 *
 * Usage Example:
 * ```cpp
 * SemaphoreHandle_t myMutex = xSemaphoreCreateMutex();
 * {
 *     SemaphoreLockGuard lock(myMutex);
 *     // ... critical section ...
 * }  // Mutex automatically released here
 * ```
 *
 * @author MeshCore Community
 * @version 1.1.0
 * @date 2025-12-18
 */
class SemaphoreLockGuard {
private:
    SemaphoreHandle_t mutex;  // Store by value, not reference
    bool locked;

public:
    /**
     * Constructor - acquires semaphore
     *
     * @param m Semaphore handle to acquire
     */
    explicit SemaphoreLockGuard(SemaphoreHandle_t m) : mutex(m), locked(false) {
        if (mutex != NULL) {
            // Check return value to ensure semaphore was actually acquired
            if (xSemaphoreTake(mutex, portMAX_DELAY) == pdTRUE) {
                locked = true;
            }
        }
    }

    /**
     * Destructor - releases semaphore
     *
     * Only releases if semaphore was successfully acquired in constructor.
     */
    ~SemaphoreLockGuard() {
        if (locked && mutex != NULL) {
            xSemaphoreGive(mutex);
        }
    }

    /**
     * Check if lock was successfully acquired
     *
     * @return true if semaphore is locked, false otherwise
     */
    bool isLocked() const {
        return locked;
    }

    // Prevent copying
    SemaphoreLockGuard(const SemaphoreLockGuard&) = delete;
    SemaphoreLockGuard& operator=(const SemaphoreLockGuard&) = delete;
};

#endif // FREERTOS_UTIL_H
