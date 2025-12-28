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
            // Use 5-second timeout instead of portMAX_DELAY to prevent infinite hangs
            // If mutex isn't acquired within 5s, there's likely a deadlock or blocking issue
            if (xSemaphoreTake(mutex, pdMS_TO_TICKS(5000)) == pdTRUE) {
                locked = true;
            } else {
                // Mutex acquisition failed - likely deadlock or task holding mutex is blocked
                #ifdef ARDUINO
                Serial.println("[CRITICAL] SemaphoreLockGuard: Failed to acquire mutex within 5s timeout!");
                Serial.println("[CRITICAL] Possible deadlock or priority inversion detected!");
                #endif
            }
        }
    }

    /**
     * Destructor - releases semaphore
     *
     * Only releases if semaphore was successfully acquired in constructor.
     * Note: locked==true guarantees mutex is valid (checked in constructor).
     */
    ~SemaphoreLockGuard() {
        if (locked) {
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

/**
 * RAII Lock Guard for FreeRTOS Recursive Mutex
 *
 * Same as SemaphoreLockGuard but supports recursive locking (same thread can
 * acquire the mutex multiple times in nested function calls).
 *
 * CRITICAL: Only use with mutexes created with xSemaphoreCreateRecursiveMutex()!
 *
 * Usage Example:
 * ```cpp
 * SemaphoreHandle_t myRecursiveMutex = xSemaphoreCreateRecursiveMutex();
 * {
 *     RecursiveSemaphoreLockGuard lock(myRecursiveMutex);
 *     // ... can call functions that also take the same mutex ...
 * }  // Mutex automatically released here
 * ```
 *
 * @author MeshCore Community
 * @version 1.0.0
 * @date 2025-12-28
 */
class RecursiveSemaphoreLockGuard {
private:
    SemaphoreHandle_t mutex;  // Store by value, not reference
    bool locked;

public:
    /**
     * Constructor - acquires recursive mutex
     *
     * @param m Recursive mutex handle to acquire
     */
    explicit RecursiveSemaphoreLockGuard(SemaphoreHandle_t m) : mutex(m), locked(false) {
        if (mutex != NULL) {
            // Use 5-second timeout to detect deadlocks
            if (xSemaphoreTakeRecursive(mutex, pdMS_TO_TICKS(5000)) == pdTRUE) {
                locked = true;
            } else {
                #ifdef ARDUINO
                Serial.println("[CRITICAL] RecursiveSemaphoreLockGuard: Failed to acquire mutex within 5s timeout!");
                Serial.println("[CRITICAL] Possible deadlock or priority inversion detected!");
                #endif
            }
        }
    }

    /**
     * Destructor - releases recursive mutex
     */
    ~RecursiveSemaphoreLockGuard() {
        if (locked) {
            xSemaphoreGiveRecursive(mutex);
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
    RecursiveSemaphoreLockGuard(const RecursiveSemaphoreLockGuard&) = delete;
    RecursiveSemaphoreLockGuard& operator=(const RecursiveSemaphoreLockGuard&) = delete;
};

#endif // FREERTOS_UTIL_H
