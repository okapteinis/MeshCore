# TCA9535 Hardware INT Pin Optimization - Design Document

**Status**: Design Phase (D.1 Complete)
**Date**: December 27, 2025
**Target**: SenseCAP Indicator D1L variant
**Branch**: `nightly`

---

## 1. Current Implementation Analysis (Phase D.1)

### 1.1 Hardware Wiring (Confirmed)

From `docs/hardware/d1l_pin_research.md` and HAL source code:

```
TCA9535 IO Expander (I2C Address: 0x40)
├── I2C Bus: SDA=GPIO 39, SCL=GPIO 40
├── INT Pin → ESP32 GPIO 42 (⚠️ CURRENTLY UNUSED)
└── Pin Mapping:
    ├── P0_0 (physical 0) → LORA_NSS (virtual 100)
    ├── P0_1 (physical 1) → LORA_RESET (virtual 101)
    ├── P0_2 (physical 2) → LORA_DIO0/BUSY (virtual 102)
    ├── P0_3 (physical 3) → LORA_DIO1 (virtual 103) ⭐ SX1262 INTERRUPT
    ├── P0_4 (physical 4) → DISPLAY_CS (virtual 104)
    └── P0_5 (physical 5) → DISPLAY_RST (virtual 105)

Current Interrupt Flow:
SX1262 DIO1 → TCA9535 P0_3 → TCA9535 INT → ESP32 GPIO 42
                                                  ↓
                                        [CURRENTLY IGNORED]
                                        [USING 5ms POLLING]
```

**⚠️ I2C Address Discrepancy**:
- `TCA9535_GPIO.h` header comment says **0x20** (default)
- `d1l_pin_research.md` says **0x40** (from I2C scan)
- **Constructor default**: `TCA9535_GPIO(uint8_t addr = 0x20)`
- **Action Required**: Verify actual hardware address via I2C scan

### 1.2 Current Polling Implementation

**Files**:
- `variants/sensecap_indicator_d1l/hal/CustomRadioLibHal.h` (interface)
- `variants/sensecap_indicator_d1l/hal/CustomRadioLibHal.cpp` (implementation)

**Polling Task**:
```cpp
// FreeRTOS task (started in CustomRadioLibHal::init())
void CustomRadioLibHal::pollTask(void* parameter) {
    while (true) {
        hal->pollVirtualInterruptsInternal();
        vTaskDelay(pdMS_TO_TICKS(5)); // ⚠️ 5ms interval (NOT 1ms)
    }
}
```

**Polling Function** (`pollVirtualInterruptsInternal()`):
```cpp
// Core polling logic (lines 42-108 in CustomRadioLibHal.cpp)
1. Quick check: if (virtualInterruptCount == 0) return;
2. For each of MAX_VIRTUAL_INTERRUPTS (4 slots):
   a. Acquire virtual_int_mutex
   b. Snapshot: pin, mode, lastState, callback, enabled
   c. Release virtual_int_mutex

   d. Acquire d1l_i2c_mutex
   e. Read current pin state via ioExpander->digitalRead(pin)
   f. Release d1l_i2c_mutex

   g. Detect edge: RISING, FALLING, or CHANGE
   h. Fire callback() if triggered (OUTSIDE mutex)

   i. Acquire virtual_int_mutex
   j. Update lastState if interrupt still enabled
   k. Release virtual_int_mutex
```

**Key Design Patterns**:
- ✅ **Snapshot Pattern**: Minimizes critical section time
- ✅ **Callbacks Fire Outside Mutex**: Prevents blocking on slow handlers
- ✅ **Two-Mutex Design**:
  - `virtual_int_mutex` protects virtualInterrupts[] array
  - `d1l_i2c_mutex` protects I2C bus access
  - Prevents deadlocks, allows fine-grained locking
- ✅ **Verification Before Update**: Checks interrupt still enabled before updating lastState

### 1.3 Virtual Interrupt Storage

**Structure** (CustomRadioLibHal.h lines 94-100):
```cpp
struct VirtualInterrupt {
    uint32_t pin;           // Virtual pin number (100-199)
    void (*callback)(void); // Callback function pointer
    uint32_t mode;          // RISING, FALLING, CHANGE
    bool enabled;           // Is this slot active?
    uint8_t lastState;      // Last known pin state (for edge detection)
};

static const int MAX_VIRTUAL_INTERRUPTS = 4;
VirtualInterrupt virtualInterrupts[MAX_VIRTUAL_INTERRUPTS];
int virtualInterruptCount;  // Number of active interrupts
```

**Current Usage** (from Module creation in target.cpp:378-384):
```cpp
radio_module = new Module(
    custom_hal,
    TCA9535_GPIO::LORA_NSS,    // NSS = 100
    TCA9535_GPIO::LORA_DIO1,   // DIO1 = 103 ⭐ INTERRUPT PIN
    TCA9535_GPIO::LORA_RESET,  // RESET = 101
    TCA9535_GPIO::LORA_DIO0    // BUSY = 102
);
```

**Interrupt Registration**: RadioLib calls `attachInterrupt(103, callback, RISING)` during radio.begin()

### 1.4 Thread Safety (Post-Security Fix)

**Two Mutexes Created in Constructor** (CustomRadioLibHal.cpp lines 125-140):

1. **d1l_i2c_mutex** (global):
   - Created in `CustomRadioLibHal` constructor
   - Used by both `CustomRadioLibHal` and `TCA9535_GPIO`
   - Protects all I2C read/write operations
   - **CRITICAL**: Must be created BEFORE `TCA9535_GPIO::begin()`

2. **virtual_int_mutex** (static):
   - Created in `CustomRadioLibHal` constructor
   - Protects `virtualInterrupts[]` array access
   - Prevents race between polling task and attach/detach
   - Added in recent security fix (Dec 2025)

**RAII Lock Guard** (freertos_util.h):
```cpp
SemaphoreLockGuard lock(d1l_i2c_mutex);
if (!lock.isLocked()) {
    // Handle mutex acquisition failure
    return;
}
// Automatic release when lock goes out of scope
```

### 1.5 Performance Characteristics

**Current Polling Approach**:
- **Polling Interval**: 5ms (200 Hz sampling rate)
- **Latency**: ~5-10ms typical (worst case: 5ms + I2C transaction time)
- **I2C Overhead**: ~4 reads/sec per active interrupt (if pin is stable)
- **CPU Overhead**: Minimal (FreeRTOS task priority 2, sleeps between polls)
- **Power Consumption**: Moderate (I2C bus active every 5ms)

**LoRa Timing Context** (for comparison):
- **SX1262 Packet Time**: ~50-200ms typical (SF8, BW 62.5kHz)
- **RadioLib Tolerance**: ±10ms interrupt latency is acceptable
- **Current 5ms latency**: ✅ Well within RadioLib requirements

**Bottleneck Analysis**:
- ✅ Not a latency problem (5ms << 50ms packet time)
- ⚠️ Inefficient use of I2C bus (polling even when no activity)
- ⚠️ Power consumption higher than necessary (periodic I2C reads)
- ✅ CPU impact minimal (task sleeps, not busy-waiting)

### 1.6 TCA9535 INT Pin Behavior (Hardware Datasheet)

**From TI TCA9535 Datasheet**:

The TCA9535 has a hardware interrupt output (INT) that:
1. **Active Low**: INT pin goes LOW when any input pin changes state
2. **Edge Triggered**: Fires on RISING or FALLING edges of input pins
3. **Auto-Clear Mechanism**: Reading the input port register clears the interrupt
4. **Global Interrupt**: Single INT line for all 16 GPIO pins
5. **Configuration**: Input Port register read required to determine which pin(s) changed

**INT Pin State Machine**:
```
Input Pin Changes → INT goes LOW → ESP32 GPIO 42 ISR fires
                                  ↓
                    Read Input Port register (0x00 or 0x01)
                                  ↓
                    INT returns HIGH (ready for next change)
```

**Key Constraints**:
- Cannot distinguish which pin changed without reading input registers
- No per-pin interrupt enable/disable (all inputs participate)
- Requires I2C transaction to clear interrupt (cannot clear via GPIO)

---

## 2. Design Goals for Optimization (Phase D.2)

### 2.1 Primary Goal: Reduce I2C Bus Load

**Current**: I2C read every 5ms (200 reads/sec) regardless of activity
**Target**: I2C read only when INT pin fires (event-driven)

**Benefits**:
- Reduces I2C bus contention with display/sensors
- Lower power consumption (I2C bus idle when no LoRa activity)
- Frees I2C bandwidth for other devices

### 2.2 Secondary Goal: Maintain Current Latency

**Requirement**: Keep ~5ms interrupt response time
**Strategy**: ESP32 hardware ISR + I2C read is faster than polling loop

### 2.3 Constraints

**Must Preserve**:
- ✅ Thread safety (existing two-mutex design)
- ✅ Backward compatibility (no API changes to CustomRadioLibHal)
- ✅ Snapshot pattern (callbacks fire outside mutex)
- ✅ Support for multiple virtual interrupts (4 slots)

**Must Avoid**:
- ❌ I2C transactions inside ESP32 ISR (illegal in FreeRTOS)
- ❌ Nested mutex acquisition (deadlock risk)
- ❌ Breaking existing polling fallback (for boards without INT wiring)

### 2.4 Success Criteria

1. ✅ I2C reads only occur when TCA9535 INT fires (not periodic)
2. ✅ No increase in interrupt latency (maintain ~5ms)
3. ✅ Thread-safe (all mutexes preserved)
4. ✅ Graceful fallback if INT pin not available
5. ✅ No breaking changes to RadioLib integration

---

## 3. Finalized IRQ-Assisted Strategy (Phase D.2-D.3) ✅

### 3.1 Hybrid Approach: ESP32 ISR + FreeRTOS Task Notification

**Architecture**:
```
TCA9535 INT (GPIO 42) → ESP32 Hardware ISR → vTaskNotifyGiveFromISR()
                                                        ↓
                                              Deferred Task (I2C allowed)
                                              [ulTaskNotifyTake() unblocks]
                                                        ↓
                                           Read TCA9535 Input Registers
                                                        ↓
                                           Fire callbacks for changed pins
```

**Rationale**:
- ESP32 ISR cannot do I2C (FreeRTOS restriction)
- Solution: ISR signals task via direct-to-task notification (more efficient than semaphore)
- FreeRTOS recommendation: Use task notifications for ISR-to-task signaling (lower overhead)
- Similar to RadioLib's own ISR design pattern

**Why Task Notifications vs Binary Semaphore**:
- ✅ **Faster**: Direct notification is ~45% faster than semaphore
- ✅ **Less RAM**: No semaphore object allocation
- ✅ **Simpler**: One-to-one signaling (perfect for this use case)
- ✅ **FreeRTOS Best Practice**: Recommended in official docs for ISR-to-task wake-up

### 3.2 Detailed Flow (Finalized)

**ESP32 Hardware ISR** (attached to GPIO 42, FALLING edge):
```cpp
// File: CustomRadioLibHal.cpp
// Static ISR function - MUST be in IRAM for performance
static void IRAM_ATTR tca9535IntPinISR(void) {
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    // Signal deferred task via direct task notification
    vTaskNotifyGiveFromISR(intHandlerTaskHandle, &xHigherPriorityTaskWoken);

    // Yield if necessary (allows task to run immediately)
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}
```

**Deferred Task** (replaces current polling task):
```cpp
// File: CustomRadioLibHal.cpp
// Static task function for INT-driven interrupt handling
static void intHandlerTask(void* parameter) {
    CustomRadioLibHal* hal = static_cast<CustomRadioLibHal*>(parameter);

    Serial.println("[CustomHAL] INT handler task started (event-driven mode)");

    while (true) {
        // Block until INT fires (NO periodic polling)
        // ulTaskNotifyTake clears notification count and returns number of notifications
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        // Level-triggered INT handling: loop while INT pin is LOW
        // This ensures no missed interrupts when multiple state changes occur
        do {
            uint16_t inputState;
            {
                SemaphoreLockGuard lock(d1l_i2c_mutex);
                if (!lock.isLocked()) {
                    Serial.println("[CustomHAL] ERROR: Failed to acquire I2C mutex in INT handler");
                    break;
                }
                // Read all 16 TCA9535 inputs at once (2 I2C transactions instead of 16)
                inputState = hal->ioExpander->readAllInputs();
            }

            // Process all registered virtual interrupts
            hal->processVirtualInterrupts(inputState);

        } while (digitalRead(TCA9535_INT_PIN) == LOW);
    }
}
```

**Benefits**:
- I2C only occurs when INT fires (event-driven)
- No periodic polling overhead
- Lower latency (task unblocks immediately on INT)
- Thread-safe (existing mutex design preserved)

### 3.3 Compile-Time Flag and Fallback Mode

**Build Flag**: `USE_TCA9535_INT_PIN` (defined in `platformio.ini`)

**Compile-Time Behavior**:
```cpp
#ifdef USE_TCA9535_INT_PIN
    // Use IRQ-assisted mode (ESP32 ISR + task notification)
    #define TCA9535_INT_PIN 42  // GPIO 42 on SenseCAP D1L
    // Create INT handler task instead of polling task
#else
    // Fall back to polling mode (current implementation)
    // Create polling task (5ms interval)
#endif
```

**Configuration** (in `platformio.ini`):
```ini
[env:SenseCapIndicator-D1L_repeater]
build_flags =
    -D USE_TCA9535_INT_PIN  ; Enable INT pin optimization
    ; ... other flags

[env:SenseCapIndicator-D1L_comp_radio_usb]
build_flags =
    -D USE_TCA9535_INT_PIN  ; Enable INT pin optimization
    ; ... other flags
```

**Fallback for Other Boards**:
- Boards without INT pin wiring: Do NOT define `USE_TCA9535_INT_PIN`
- HAL automatically uses polling mode (current 5ms implementation)
- No code changes required for fallback behavior

### 3.4 Code Changes (Internal Only)

**New Members in CustomRadioLibHal** (private, `#ifdef` guarded):
```cpp
// In CustomRadioLibHal.h
private:
    #ifdef USE_TCA9535_INT_PIN
    TaskHandle_t intHandlerTaskHandle;  // FreeRTOS task handle for INT handler
    #endif

    void processVirtualInterrupts(uint16_t inputState);  // Common logic (polling + IRQ)
```

**New Static Functions** (in CustomRadioLibHal.cpp):
```cpp
#ifdef USE_TCA9535_INT_PIN
// Static ISR (must be static for attachInterrupt)
static TaskHandle_t intHandlerTaskHandle = NULL;  // Global for ISR access
static void IRAM_ATTR tca9535IntPinISR(void);     // ESP32 hardware ISR
static void intHandlerTask(void* parameter);       // Deferred I2C handler task
#endif
```

**New Method in TCA9535_GPIO**:
```cpp
// In TCA9535_GPIO.h
uint16_t readAllInputs();  // Read both input port registers (0x00 and 0x01)

// In TCA9535_GPIO.cpp
uint16_t TCA9535_GPIO::readAllInputs() {
    if (!initialized) {
        Serial.println("[TCA9535] ERROR: Not initialized - call begin() first");
        return 0x0000;
    }

    uint16_t allInputs = 0x0000;
    {
        SemaphoreLockGuard lock(d1l_i2c_mutex);
        if (!lock.isLocked()) {
            Serial.println("[TCA9535] ERROR: Failed to acquire I2C mutex");
            return 0x0000;
        }

        // EFFICIENCY: Use read16() to read both ports in 2 I2C transactions
        // (instead of 16 individual read1() calls = 16 I2C transactions)
        // Returns: [P1_7..P1_0 | P0_7..P0_0] where bit 0 = P0_0, bit 15 = P1_7
        allInputs = ioExpander->read16();
    }

    return allInputs;
}
```

**Backward Compatibility**:
- ✅ Public API unchanged (no breaking changes)
- ✅ Existing `pollVirtualInterrupts()` still works (manual trigger)
- ✅ Polling mode still supported (without `USE_TCA9535_INT_PIN` flag)
- ✅ No changes to RadioLib integration

### 3.5 Exact File Locations for Implementation

**File 1: `variants/sensecap_indicator_d1l/hal/CustomRadioLibHal.h`**
- Add `processVirtualInterrupts(uint16_t inputState)` declaration (private)
- Add `#ifdef USE_TCA9535_INT_PIN` block with `intHandlerTaskHandle` member

**File 2: `variants/sensecap_indicator_d1l/hal/CustomRadioLibHal.cpp`**
- Add `#ifdef USE_TCA9535_INT_PIN` block at file scope:
  - `static TaskHandle_t intHandlerTaskHandle = NULL;`
  - `static void IRAM_ATTR tca9535IntPinISR(void)` implementation
  - `static void intHandlerTask(void* parameter)` implementation
- Modify `init()` method:
  - `#ifdef USE_TCA9535_INT_PIN`: Create INT handler task + attach ISR
  - `#else`: Create polling task (current behavior)
- Modify `term()` method:
  - `#ifdef USE_TCA9535_INT_PIN`: Detach ISR, delete INT handler task
  - `#else`: Delete polling task (current behavior)
- Extract `processVirtualInterrupts(uint16_t inputState)` from `pollVirtualInterruptsInternal()`
  - Shared by both polling and INT modes

**File 3: `variants/sensecap_indicator_d1l/hal/TCA9535_GPIO.h`**
- Add `uint16_t readAllInputs();` declaration (public)

**File 4: `variants/sensecap_indicator_d1l/hal/TCA9535_GPIO.cpp`**
- Implement `uint16_t TCA9535_GPIO::readAllInputs()`
  - Read input port 0 (register 0x00)
  - Read input port 1 (register 0x01)
  - Return combined 16-bit value
  - Thread-safe with `d1l_i2c_mutex`

**File 5: `platformio.ini`**
- Add `-D USE_TCA9535_INT_PIN` to build_flags for:
  - `[env:SenseCapIndicator-D1L_repeater]`
  - `[env:SenseCapIndicator-D1L_comp_radio_usb]`

---

## 4. Implementation Plan (Phases D.3-D.5)

### Phase D.3: Prepare HAL Changes (Interface-Level Plan)

**Tasks**:
1. ✅ Add `#ifdef TCA9535_INT_PIN` guards to CustomRadioLibHal.h
2. ✅ Add new private members: `int_semaphore`, `intPinISR()`, `intHandlerTask()`
3. ✅ Extract common logic into `processVirtualInterrupts(inputState)`
4. ✅ Update `init()` to choose polling vs IRQ mode at runtime
5. ✅ Add `readAllInputs()` method to TCA9535_GPIO (reads both input registers)

### Phase D.4: Implement Minimal INT Support (Code Changes)

**Files to Modify**:
1. `variants/sensecap_indicator_d1l/hal/CustomRadioLibHal.h` (add #ifdef blocks)
2. `variants/sensecap_indicator_d1l/hal/CustomRadioLibHal.cpp` (implement IRQ mode)
3. `variants/sensecap_indicator_d1l/hal/TCA9535_GPIO.h` (add readAllInputs() declaration)
4. `variants/sensecap_indicator_d1l/hal/TCA9535_GPIO.cpp` (implement readAllInputs())
5. `variants/sensecap_indicator_d1l/target.h` (define TCA9535_INT_PIN 42)

**Testing Strategy**:
- Verify compilation with `#define TCA9535_INT_PIN 42`
- Verify compilation with `#undef TCA9535_INT_PIN` (polling fallback)
- Add serial debug output to confirm ISR fires on LoRa packets

### Phase D.5: Build and Basic Runtime Verification

**Build Test**:
```bash
pio run -e SenseCapIndicator-D1L_repeater
```

**Expected Serial Output**:
```
[CustomHAL] TCA9535 INT pin detected: GPIO 42
[CustomHAL] Using IRQ-assisted interrupt handling
[CustomHAL] Deferred interrupt task created (priority 3)
[CustomHAL] ESP32 ISR attached to GPIO 42 (FALLING edge)
```

**Regression Test** (ensure no breaking changes):
```bash
pio run -e SenseCapIndicator-D1L_comp_radio_usb
```

---

## 5. Risk Assessment

### 5.1 Low-Risk Aspects ✅

1. **Backward Compatible**: Polling mode still works (`#undef TCA9535_INT_PIN`)
2. **No API Changes**: Public interface unchanged (internal optimization only)
3. **Proven Pattern**: ESP32 ISR + FreeRTOS semaphore is standard practice
4. **Thread Safety Preserved**: Existing two-mutex design untouched
5. **Incremental**: Can be added without removing polling code

### 5.2 Medium-Risk Aspects ⚠️

1. **I2C Address Discrepancy**: Must verify 0x20 vs 0x40 before testing
2. **TCA9535 INT Auto-Clear**: Must confirm reading input registers clears INT
3. **ESP32 GPIO 42 Conflicts**: Verify GPIO 42 not used by other peripherals
4. **FreeRTOS Priority**: INT task priority (3) must not starve other tasks
5. **Missed Interrupts**: If INT fires while I2C transaction in progress

### 5.3 Mitigation Strategies

**For I2C Address**:
- Add I2C scanner debug output in `TCA9535_GPIO::begin()`
- Auto-detect address (try 0x20, fall back to 0x40)

**For INT Auto-Clear**:
- Datasheet review (confirm behavior)
- Hardware test with oscilloscope (verify INT returns HIGH after read)

**For GPIO Conflicts**:
- Review SenseCAP schematic (confirm GPIO 42 dedicated to TCA9535 INT)
- Add compile-time check: `static_assert(TCA9535_INT_PIN == 42)`

**For Missed Interrupts**:
- After I2C read, check if INT still LOW (indicates another change occurred)
- If LOW, re-trigger semaphore without waiting for new ISR

---

## 6. Performance Expectations

### 6.1 Current Polling Mode (Baseline)

- **I2C Reads/Second**: 200 (one per 5ms poll)
- **I2C Transactions/Hour**: 720,000
- **Power Consumption**: Moderate (I2C active 200 times/sec)
- **Latency**: ~5-10ms typical

### 6.2 IRQ-Assisted Mode (Target)

**Low Traffic Scenario** (1 LoRa packet/minute):
- **I2C Reads/Second**: ~0.017 (only on packet events)
- **I2C Transactions/Hour**: ~60 (99.99% reduction!)
- **Power Consumption**: Minimal (I2C mostly idle)
- **Latency**: ~2-5ms (faster than polling)

**High Traffic Scenario** (10 LoRa packets/second):
- **I2C Reads/Second**: ~10 (one per packet event)
- **I2C Transactions/Hour**: ~36,000 (95% reduction)
- **Power Consumption**: Low-moderate (I2C active only during traffic)
- **Latency**: ~2-5ms (still faster than polling)

**CPU Overhead**:
- Polling mode: FreeRTOS task wakes every 5ms (high context switch overhead)
- IRQ mode: Task sleeps until semaphore signaled (zero overhead when idle)

---

## 7. Next Steps (Immediate Actions)

**Phase D.1** ✅ **COMPLETE**:
- [x] Document current wiring (GPIO 42 → TCA9535 INT)
- [x] Analyze polling implementation (5ms interval, two mutexes)
- [x] Confirm virtual pin 103 (LORA_DIO1) is the SX1262 interrupt
- [x] Review thread safety design (snapshot pattern, RAII locks)

**Phase D.2** (Design) - **READY TO START**:
- [ ] Finalize IRQ-assisted architecture (ESP32 ISR + deferred task)
- [ ] Design fallback mechanism (#ifdef TCA9535_INT_PIN)
- [ ] Plan TCA9535_GPIO::readAllInputs() implementation
- [ ] Create detailed code change checklist

**Phase D.3** (Prepare HAL Changes):
- [ ] Add compile-time guards to CustomRadioLibHal.h
- [ ] Define new private members (int_semaphore, intPinISR, etc.)
- [ ] Extract processVirtualInterrupts() common logic
- [ ] Update constructor/destructor for IRQ mode

**Phase D.4** (Implement):
- [ ] Implement ESP32 ISR (intPinISR)
- [ ] Implement deferred task (intHandlerTask)
- [ ] Implement TCA9535_GPIO::readAllInputs()
- [ ] Add #define TCA9535_INT_PIN 42 to target.h
- [ ] Verify compilation (both IRQ and polling modes)

**Phase D.5** (Verification):
- [ ] Build test (SenseCapIndicator-D1L_repeater)
- [ ] Serial output verification (confirm ISR fires)
- [ ] Regression test (companion_radio_usb)
- [ ] Hardware test (if available): Send LoRa packets, verify interrupts

---

## Document Metadata

- **Author**: AI Assistant (Claude Sonnet 4.5)
- **Created**: December 27, 2025
- **MeshCore Version**: 1.10.0 (nightly branch)
- **Status**: Phase D.1 Complete, D.2 Design Ready
- **Hardware**: SenseCAP Indicator D1L (ESP32-S3 + TCA9535 + SX1262)
- **Related Files**:
  - `docs/hardware/d1l_pin_research.md` (hardware wiring)
  - `variants/sensecap_indicator_d1l/hal/CustomRadioLibHal.{h,cpp}` (current implementation)
  - `variants/sensecap_indicator_d1l/hal/TCA9535_GPIO.{h,cpp}` (IO expander wrapper)

---

**End of Design Document**
