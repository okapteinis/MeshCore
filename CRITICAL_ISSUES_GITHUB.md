# Critical Issues - GitHub Issue Templates

Copy each section below as a separate GitHub issue.

---

## Issue 1: Race Conditions in D1L Virtual Interrupt Handler ⚠️ CRITICAL

**Labels**: `bug`, `critical`, `D1L`, `concurrency`, `hardware`
**Milestone**: v1.11.0
**Priority**: P0 (Blocker)

### Description

The D1L variant's `CustomRadioLibHal` has critical race conditions that can cause data corruption, missed radio interrupts, and potential crashes.

### Root Cause

The `virtualInterrupts[]` array is accessed by multiple threads without proper synchronization:

1. **Main thread**: Calls `attachInterrupt()` and `detachInterrupt()` to modify array
2. **Polling task**: Runs every 5ms reading the same array
3. **Shared state**: `virtualInterruptCount` is incremented/decremented non-atomically

**File**: `variants/sensecap_indicator_d1l/hal/CustomRadioLibHal.cpp`

**Problematic code** (lines 201-208):
```cpp
// attachInterrupt() - NO MUTEX PROTECTION
for (int i = 0; i < MAX_VIRTUAL_INTERRUPTS; i++) {
    if (!virtualInterrupts[i].enabled) {
        virtualInterrupts[i].pin = interruptNum;          // RACE
        virtualInterrupts[i].callback = interruptCb;      // RACE
        virtualInterrupts[i].mode = mode;                 // RACE
        virtualInterrupts[i].enabled = true;              // RACE
        virtualInterruptCount++;                          // RACE (not atomic)
```

**Polling task** (lines 30-40) reads same array with only I2C mutex (not array mutex).

### Impact

- **Data corruption**: Polling task reads partially-written interrupt configuration
- **Missed interrupts**: DIO1 state changes not detected → radio RX/TX failures
- **Crashes**: Invalid callback pointers if structure corrupted mid-write
- **Intermittent failures**: Race conditions are timing-dependent, hard to reproduce

### Steps to Reproduce

1. Build D1L repeater: `pio run -e SenseCapIndicator-D1L_repeater`
2. Run radio stress test with rapid RX/TX cycles
3. Observe occasional missed interrupts or crashes

**Probability**: High under heavy radio traffic

### Proposed Fix

Add separate mutex for `virtualInterrupts[]` array protection:

```cpp
// Add to CustomRadioLibHal.cpp
static SemaphoreHandle_t virtual_int_mutex = NULL;

// In constructor:
virtual_int_mutex = xSemaphoreCreateMutex();

// In attachInterrupt():
void CustomRadioLibHal::attachInterrupt(uint32_t interruptNum, void (*interruptCb)(), uint32_t mode) {
    if (isVirtualPin(interruptNum)) {
        SemaphoreLockGuard lock(virtual_int_mutex);
        if (!lock.isLocked()) {
            Serial.printf("[CustomHAL] ERROR: Failed to acquire virtual interrupt mutex\n");
            return;
        }

        // ... existing code (now protected) ...
    }
}

// In pollVirtualInterruptsInternal():
void CustomRadioLibHal::pollVirtualInterruptsInternal() {
    SemaphoreLockGuard lock(virtual_int_mutex);
    if (!lock.isLocked()) return;

    // Snapshot count to avoid races
    int count = virtualInterruptCount;

    for (int i = 0; i < MAX_VIRTUAL_INTERRUPTS; i++) {
        if (!virtualInterrupts[i].enabled) continue;

        // Read state with I2C mutex (nested locking - safe with FreeRTOS)
        uint8_t currentState;
        {
            SemaphoreLockGuard i2c_lock(d1l_i2c_mutex);
            if (!i2c_lock.isLocked()) continue;
            currentState = ioExpander->digitalRead(virtualInterrupts[i].pin);
        }

        // ... edge detection logic ...
    }
}
```

### Testing Plan

1. Add stress test that rapidly attaches/detaches interrupts
2. Run radio for 24+ hours with heavy traffic
3. Verify no crashes or missed interrupts
4. Check mutex contention with `uxSemaphoreGetCount()`

### Related Files

- `variants/sensecap_indicator_d1l/hal/CustomRadioLibHal.h`
- `variants/sensecap_indicator_d1l/hal/CustomRadioLibHal.cpp`
- `variants/sensecap_indicator_d1l/hal/freertos_util.h` (SemaphoreLockGuard)

### References

- [FreeRTOS Semaphore Documentation](https://www.freertos.org/a00113.html)
- [CODE_REVIEW_REPORT.md - Section 1.1](CODE_REVIEW_REPORT.md)

---

## Issue 2: Buffer Overflow Risk in Packet::readFrom() ⚠️ HIGH

**Labels**: `bug`, `security`, `high-priority`, `buffer-overflow`
**Milestone**: v1.11.0
**Priority**: P1 (High)

### Description

`Packet::readFrom()` can read beyond buffer bounds when parsing malformed packets with transport codes enabled.

### Root Cause

**File**: `src/Packet.cpp:41-58`

```cpp
bool Packet::readFrom(const uint8_t src[], uint8_t len) {
    uint8_t i = 0;
    header = src[i++];
    if (hasTransportCodes()) {
        memcpy(&transport_codes[0], &src[i], 2); i += 2;  // ⚠️ NO BOUNDS CHECK
        memcpy(&transport_codes[1], &src[i], 2); i += 2;  // i could be > len
    }
    // ...
}
```

If `len < 5` and `hasTransportCodes()` returns true (based on header byte), the code reads 4 bytes starting at `src[1]`, potentially reading beyond the buffer.

### Attack Vector

1. Attacker in radio range sends packet with:
   - `len = 1` (minimal packet)
   - Header byte with transport code flag set
2. Code reads `src[1]`, `src[2]`, `src[3]`, `src[4]` → buffer overread
3. Could expose memory contents or cause segmentation fault

**Likelihood**: Low (requires attacker in range with custom firmware)
**Impact**: High (potential memory disclosure or crash)

### Proposed Fix

Add bounds checking before all memcpy operations:

```cpp
bool Packet::readFrom(const uint8_t src[], uint8_t len) {
    if (len < 1) return false;  // Minimum packet size

    uint8_t i = 0;
    header = src[i++];

    // Validate transport codes fit in buffer
    if (hasTransportCodes()) {
        if (i + 4 > len) return false;  // ADD CHECK
        memcpy(&transport_codes[0], &src[i], 2); i += 2;
        memcpy(&transport_codes[1], &src[i], 2); i += 2;
    } else {
        transport_codes[0] = transport_codes[1] = 0;
    }

    // Validate path length field is readable
    if (i >= len) return false;
    path_len = src[i++];

    // Validate path data fits in buffer
    if (i + path_len > len) return false;  // ADD CHECK
    if (path_len > sizeof(path)) return false;  // Existing check
    memcpy(path, &src[i], path_len); i += path_len;

    // Calculate payload length
    if (i > len) return false;  // Should be >= not >
    payload_len = len - i;

    // Validate payload fits in buffer
    if (payload_len > sizeof(payload)) return false;  // Existing check
    memcpy(payload, &src[i], payload_len);

    return true;
}
```

### Testing Plan

1. Create fuzzing tests with malformed packets:
   - `len=0`, `len=1`, `len=2`, `len=3`, `len=4`
   - Various header flag combinations
   - Oversized path/payload lengths
2. Run under AddressSanitizer (ASAN) or Valgrind
3. Verify all return `false` without crashes

### Related Issues

- Issue #3: TRACE packet bounds check (similar pattern)

### References

- [OWASP: Buffer Overflow](https://owasp.org/www-community/vulnerabilities/Buffer_Overflow)
- [CODE_REVIEW_REPORT.md - Section 1.2](CODE_REVIEW_REPORT.md)

---

## Issue 3: TRACE Packet Bounds Check Missing ⚠️ HIGH

**Labels**: `bug`, `security`, `high-priority`, `buffer-overflow`
**Milestone**: v1.11.0
**Priority**: P1 (High)

### Description

TRACE packet handling reads 8 bytes from payload without verifying `payload_len >= 8`.

### Root Cause

**File**: `src/Mesh.cpp:47-60`

```cpp
if (pkt->isRouteDirect() && pkt->getPayloadType() == PAYLOAD_TYPE_TRACE) {
    if (pkt->path_len < MAX_PATH_SIZE) {
        uint8_t i = 0;
        uint32_t trace_tag;
        memcpy(&trace_tag, &pkt->payload[i], 4); i += 4;  // ⚠️ NO CHECK
        uint32_t auth_code;
        memcpy(&auth_code, &pkt->payload[i], 4); i += 4;  // ⚠️ NO CHECK
        uint8_t flags = pkt->payload[i++];  // ⚠️ NO CHECK
        // ...
```

If `payload_len < 9`, memcpy reads beyond payload buffer.

### Impact

- **Memory corruption**: Reading uninitialized memory
- **Potential crashes**: If payload buffer is at end of allocation
- **Information disclosure**: Attacker could craft packets to leak memory

### Proposed Fix

Add payload length validation:

```cpp
if (pkt->isRouteDirect() && pkt->getPayloadType() == PAYLOAD_TYPE_TRACE) {
    if (pkt->path_len < MAX_PATH_SIZE && pkt->payload_len >= 9) {  // ADD CHECK
        uint8_t i = 0;
        uint32_t trace_tag;
        memcpy(&trace_tag, &pkt->payload[i], 4); i += 4;
        uint32_t auth_code;
        memcpy(&auth_code, &pkt->payload[i], 4); i += 4;
        uint8_t flags = pkt->payload[i++];
        // ... rest of code ...
    }
}
```

### Testing Plan

1. Send TRACE packet with `payload_len = 0, 1, 2, ..., 8`
2. Verify function returns without crash
3. Send valid TRACE packet (`payload_len >= 9`) and verify processing

### Related Issues

- Issue #2: Packet::readFrom() bounds check (similar pattern)

### References

- [CODE_REVIEW_REPORT.md - Section 1.3](CODE_REVIEW_REPORT.md)

---

## Issue 4: Private Key Leaked in Debug Output 🔒 HIGH

**Labels**: `security`, `high-priority`, `credentials`, `production-hardening`
**Milestone**: v1.11.0
**Priority**: P1 (High)

### Description

`LocalIdentity::printTo()` outputs the private key in plaintext to serial output, which can permanently compromise node identity.

### Root Cause

**File**: `src/Identity.cpp:63-66`

```cpp
void LocalIdentity::printTo(Stream& s) const {
    s.print("pub_key: "); Utils::printHex(s, pub_key, PUB_KEY_SIZE); s.println();
    s.print("prv_key: "); Utils::printHex(s, prv_key, PRV_KEY_SIZE); s.println();  // ⚠️ LEAKS KEY
}
```

### Impact

- **Permanent compromise**: If attacker gains serial access (USB, UART), they can steal private key
- **Node impersonation**: Attacker can sign packets as victim node indefinitely
- **No recovery**: Private key cannot be rotated without reflashing firmware and losing identity

### Attack Scenarios

1. **Physical access**: Attacker plugs into USB/UART during debug session
2. **Log files**: Developer accidentally commits logs containing private keys to Git
3. **Remote logging**: If serial output forwarded over network (SSH, telnet)

### Proposed Fix

**Option 1: Conditional compilation** (Recommended)
```cpp
void LocalIdentity::printTo(Stream& s) const {
    s.print("pub_key: "); Utils::printHex(s, pub_key, PUB_KEY_SIZE); s.println();
#ifdef ENABLE_PRIVATE_KEY_EXPORT
    s.print("prv_key: "); Utils::printHex(s, prv_key, PRV_KEY_SIZE); s.println();
#else
    s.print("prv_key: [REDACTED - enable ENABLE_PRIVATE_KEY_EXPORT to view]");
    s.println();
#endif
}
```

**Option 2: Separate function**
```cpp
void LocalIdentity::printTo(Stream& s) const {
    s.print("pub_key: "); Utils::printHex(s, pub_key, PUB_KEY_SIZE); s.println();
    s.print("prv_key: [REDACTED]"); s.println();
}

#ifdef ENABLE_PRIVATE_KEY_EXPORT
void LocalIdentity::printPrivateKey(Stream& s) const {
    s.print("prv_key: "); Utils::printHex(s, prv_key, PRV_KEY_SIZE); s.println();
}
#endif
```

### Audit Required

Search entire codebase for calls to `LocalIdentity::printTo()`:
```bash
grep -rn "printTo" . --include="*.cpp" --include="*.h"
```

Verify no production code paths call this function.

### Testing Plan

1. Build with `ENABLE_PRIVATE_KEY_EXPORT` undefined
2. Call `printTo()` and verify private key is redacted
3. Build with `ENABLE_PRIVATE_KEY_EXPORT` defined
4. Verify private key is shown (development builds only)

### References

- [OWASP: Sensitive Data Exposure](https://owasp.org/www-project-top-ten/2017/A3_2017-Sensitive_Data_Exposure)
- [CODE_REVIEW_REPORT.md - Section 2.1](CODE_REVIEW_REPORT.md)

---

## Issue 5: Time Manipulation Attack in CMD_SET_DEVICE_TIME 🔒 MEDIUM

**Labels**: `security`, `medium-priority`, `validation`, `BLE-protocol`
**Milestone**: v1.11.1
**Priority**: P2 (Medium)

### Description

`CMD_SET_DEVICE_TIME` BLE command accepts any timestamp without validation, allowing time manipulation attacks.

### Root Cause

**File**: `examples/simple_repeater/MyMesh.cpp:1439-1460`

```cpp
} else if (cmd_frame[0] == CMD_SET_DEVICE_TIME && len >= 5) {
    uint32_t epochSeconds;
    epochSeconds = (uint32_t)cmd_frame[1] | ((uint32_t)cmd_frame[2] << 8) |
                   ((uint32_t)cmd_frame[3] << 16) | ((uint32_t)cmd_frame[4] << 24);

    struct timeval tv;
    tv.tv_sec = epochSeconds;
    tv.tv_usec = 0;

    if (settimeofday(&tv, NULL) == 0) {  // ⚠️ NO VALIDATION
```

Accepts any 32-bit value:
- `0` (Jan 1, 1970)
- `4294967295` (Feb 7, 2106)
- Breaks time-based security

### Impact

**Direct Effects**:
- Advertisement timestamp validation bypassed (replay attacks)
- Rate limiting broken (`discover_limiter` checks timestamp)
- Logs show incorrect timestamps

**Potential Future Issues**:
- Certificate expiration checks (if TLS added)
- Time-based key rotation
- Scheduled tasks (OTA updates, backups)

### Attack Scenarios

1. **Replay attack enablement**:
   - Attacker sets time to year 2100
   - Sends advertisement with timestamp 2025
   - Victim accepts (timestamp appears "old" → replay protection broken)

2. **Rate limiting bypass**:
   - Set time to 1970
   - Spam discover requests (rate limiter thinks time hasn't advanced)

### Proposed Fix

Validate timestamp is reasonable:

```cpp
} else if (cmd_frame[0] == CMD_SET_DEVICE_TIME && len >= 5) {
    uint32_t epochSeconds;
    epochSeconds = (uint32_t)cmd_frame[1] | ((uint32_t)cmd_frame[2] << 8) |
                   ((uint32_t)cmd_frame[3] << 16) | ((uint32_t)cmd_frame[4] << 24);

    // Validate timestamp is reasonable
    const uint32_t MIN_VALID_TIME = 946684800;   // Jan 1, 2000
    const uint32_t MAX_VALID_TIME = 2147483647;  // Jan 19, 2038 (Unix epoch limit)

    if (epochSeconds < MIN_VALID_TIME || epochSeconds > MAX_VALID_TIME) {
        writeErrFrame(ERR_CODE_ILLEGAL_ARG);
        MESH_DEBUG_PRINTLN("CMD_SET_DEVICE_TIME: Invalid timestamp %u (range: %u-%u)",
                           epochSeconds, MIN_VALID_TIME, MAX_VALID_TIME);
        return;
    }

    struct timeval tv;
    tv.tv_sec = epochSeconds;
    tv.tv_usec = 0;

    if (settimeofday(&tv, NULL) == 0) {
        // ... success handling ...
    }
}
```

**Optional Enhancement**: Rate limiting
```cpp
static uint32_t last_time_set = 0;
static uint32_t time_set_count = 0;

if (millis() - last_time_set < 60000) {  // Within 1 minute
    time_set_count++;
    if (time_set_count > 3) {
        writeErrFrame(ERR_CODE_ILLEGAL_ARG);
        MESH_DEBUG_PRINTLN("CMD_SET_DEVICE_TIME: Rate limit exceeded");
        return;
    }
} else {
    time_set_count = 0;
}
last_time_set = millis();
```

### Testing Plan

1. Test rejection of invalid timestamps:
   - `epochSeconds = 0` → should reject
   - `epochSeconds = 100` → should reject
   - `epochSeconds = 946684799` → should reject (just before min)
   - `epochSeconds = 2147483648` → should reject (just after max)

2. Test acceptance of valid timestamps:
   - `epochSeconds = 946684800` → should accept (Jan 1, 2000)
   - `epochSeconds = 1735257600` → should accept (Dec 27, 2024)
   - `epochSeconds = 2147483647` → should accept (Jan 19, 2038)

3. Test rate limiting (if implemented):
   - Send 4 time updates in 1 minute → 4th should be rejected

### References

- [Unix Epoch Time](https://en.wikipedia.org/wiki/Unix_time)
- [Y2038 Problem](https://en.wikipedia.org/wiki/Year_2038_problem)
- [CODE_REVIEW_REPORT.md - Section 2.3](CODE_REVIEW_REPORT.md)

---

**Total Issues**: 5 critical/high priority
**Estimated Effort**: 2-3 days for all fixes + testing
**Target Milestone**: v1.11.0 (blocking for production D1L deployment)
