# SenseCAP Indicator D1L - Complete Guide

**Device:** SenseCAP Indicator D1L
**Chipset:** ESP32-S3
**Radio:** Semtech SX1262 LoRa (via TCA9535 I/O expander)
**Status:** Production-ready with recursive mutex fix (2025-12-28)

---

## Part 1: Flashing Guide

### Prerequisites

1. **PlatformIO** installed (CLI or VS Code extension)
   - CLI: `pip install platformio`
   - VS Code: Install PlatformIO IDE extension

2. **USB Cable** - USB-C cable for D1L connection

3. **macOS Serial Port Detection**
   - D1L appears as `/dev/cu.usbserial-XXXX` (e.g., `/dev/cu.usbserial-1110`)
   - Use `ls /dev/cu.usb*` to find your device

### Flashing with PlatformIO CLI

#### Step 1: Clone Repository
```bash
git clone https://github.com/ripplebiz/MeshCore.git
cd MeshCore
```

#### Step 2: Connect D1L via USB
Plug in the D1L and verify the serial port:
```bash
ls /dev/cu.usbserial-*
# Expected output: /dev/cu.usbserial-1110 (or similar)
```

#### Step 3: Flash Repeater Firmware
```bash
pio run -e SenseCapIndicator-D1L_repeater -t upload --upload-port /dev/cu.usbserial-1110
```

**Replace `/dev/cu.usbserial-1110` with your actual serial port.**

#### Step 4: Verify Flash Success
Look for:
```
Writing at 0x001aaa38... (100 %)
Wrote 1703616 bytes (1095636 compressed)
Hash of data verified.
Hard resetting via RTS pin...
========================= [SUCCESS] Took XX.XX seconds =========================
```

#### Step 5: Monitor Serial Output
```bash
pio device monitor --port /dev/cu.usbserial-1110 --baud 115200
```

Or use the dedicated logger:
```bash
python3 tools/d1l_logger.py
```

### Flashing with VS Code (PlatformIO IDE)

#### Step 1: Open Project
1. Open VS Code
2. File → Open Folder → Select `MeshCore` directory
3. PlatformIO should auto-detect the project

#### Step 2: Select Environment
1. Click PlatformIO icon in sidebar
2. Expand `SenseCapIndicator-D1L_repeater`
3. Click "Upload" (or "Upload and Monitor")

#### Step 3: Specify Upload Port (if needed)
If upload fails with "no port found":
1. Open `platformio.ini`
2. Add to the D1L environment section:
   ```ini
   upload_port = /dev/cu.usbserial-1110
   ```
3. Retry upload

### Available Firmware Types

#### Repeater (Recommended)
```bash
pio run -e SenseCapIndicator-D1L_repeater -t upload --upload-port /dev/cu.usbserial-XXXX
```
- Standalone mesh repeater
- Configuration via Serial CLI or web config tool
- No display output (headless mode)

#### Companion Radio (Future)
```bash
pio run -e SenseCapIndicator-D1L_comp_radio_usb -t upload --upload-port /dev/cu.usbserial-XXXX
```
- Connects to mobile app via USB/BLE
- Supports messaging, contacts, channels
- Display support planned

### Serial CLI Commands

After flashing, connect via serial and use these commands:

```bash
stats-packets      # Packet statistics (recv, sent, flood)
stats-radio        # Radio stats (RSSI, SNR, noise floor)
neighbors          # List discovered neighbors
clock              # Show current time
advert             # Send advertisement packet
help               # List all commands
```

### Troubleshooting

#### "Device not found" error
```bash
ls -la /dev/cu.usb*
# If no device appears:
# - Try different USB cable
# - Check cable supports data (not charge-only)
# - Try different USB port
```

#### "Permission denied" error
On macOS, this should not occur. On Linux:
```bash
sudo usermod -a -G dialout $USER
# Then log out and log back in
```

#### Flash succeeds but device doesn't boot
1. Check serial monitor for boot messages
2. Look for panic/crash messages
3. Try reflashing with `--erase-all` flag:
   ```bash
   pio run -e SenseCapIndicator-D1L_repeater -t erase
   pio run -e SenseCapIndicator-D1L_repeater -t upload
   ```

#### Receiver not working (recv:0)
If you flashed firmware before 2025-12-28 and experience:
- `recv: 0` packets received
- `neighbors: -none-`
- `[CRITICAL] SemaphoreLockGuard: Failed to acquire mutex` errors

**Solution:** Reflash with the latest firmware containing the recursive mutex fix (see Part 2 below).

---

## Part 2: RX Deadlock Post-Mortem (2025-12-28)

### Executive Summary

**Issue:** SenseCAP Indicator D1L receiver completely non-functional (`recv:0`) despite transmitter working perfectly.

**Root Cause:** I2C mutex deadlock in nested function calls between `CustomRadioLibHal::attachInterrupt()` and `TCA9535_GPIO::digitalRead()`.

**Solution:** Converted `d1l_i2c_mutex` from regular to recursive mutex.

**Status:** ✅ Fixed and verified (2025-12-28)

---

### The Symptoms

#### Initial Observations
- **Receiver completely dead**: `recv: 0` packets received despite hours of runtime
- **No neighbors discovered**: `neighbors: -none-` even with active companion device physically adjacent
- **No air time**: `rx_air_secs: 0` (receiver never activated)
- **Timeouts on all transmissions**: Every TX packet timed out waiting for ACK
- **Device invisible on mesh**: Not appearing on meshlog.lv despite TX working

#### Misleading Evidence
- **Transmitter working perfectly**: Advertisements sent successfully, visible on network
- **CAD (Channel Activity Detection) working**: Radio detecting channel activity correctly
- **No obvious errors in logs**: Standard operation except for recv:0
- **Recent code changes**: INT pin optimization added just before symptoms appeared

#### Initial Hypothesis (WRONG)
Based on recv:0 persisting across firmware reflashes, initial conclusion was **hardware failure** (faulty SX1262 receiver, antenna issue, or IO expander fault).

**Critical User Correction:**
> "Your conclusion that it is a hardware issue is almost certainly WRONG. This device was working perfectly before we started optimizing the INT pin handling code."

This shifted the investigation from hardware to **software regression analysis**.

---

### The Root Cause

#### Recursive Mutex Deadlock

**Location:** I2C bus mutex (`d1l_i2c_mutex`) shared between `CustomRadioLibHal` and `TCA9535_GPIO`

**Mechanism:**
```cpp
// Thread execution during radio initialization:

CustomRadioLibHal::attachInterrupt() (line 413)
  ↓
  Takes d1l_i2c_mutex with SemaphoreLockGuard
  ↓
  Calls ioExpander->digitalRead(interruptNum) (line 418)
  ↓
  TCA9535_GPIO::digitalRead() (line 205)
  ↓
  Attempts to take d1l_i2c_mutex AGAIN
  ↓
  ❌ DEADLOCK: Regular FreeRTOS mutex doesn't support recursion
  ↓
  5-second timeout expires
  ↓
  [CRITICAL] SemaphoreLockGuard: Failed to acquire mutex within 5s timeout!
  ↓
  attachInterrupt() returns without configuring interrupt
  ↓
  Polling task cannot read TCA9535 inputs (mutex blocked)
  ↓
  RX interrupts never detected
  ↓
  recv stays at 0
```

**Why It Happened:**
1. `d1l_i2c_mutex` created as **regular mutex** (`xSemaphoreCreateMutex()`) at line 226
2. Regular mutexes **cannot be recursively acquired** by the same thread
3. Nested function calls (`attachInterrupt` → `digitalRead`) both tried to lock the same mutex
4. No compiler warning - this is a runtime deadlock condition

**Impact:**
- Complete RX failure (no packets received)
- Polling task blocked (could not read TCA9535 interrupt status)
- Virtual interrupts never triggered
- Device appeared "deaf" to the mesh network

---

### The Solution

#### Implementation: RecursiveSemaphoreLockGuard

**Strategy:** Convert `d1l_i2c_mutex` to a **recursive mutex** that allows the same thread to acquire it multiple times.

#### Changes Made:

**1. Added Recursive Lock Guard Class**
File: `variants/sensecap_indicator_d1l/hal/freertos_util.h`

```cpp
class RecursiveSemaphoreLockGuard {
private:
    SemaphoreHandle_t mutex;
    bool locked;

public:
    explicit RecursiveSemaphoreLockGuard(SemaphoreHandle_t m) : mutex(m), locked(false) {
        if (mutex != NULL) {
            if (xSemaphoreTakeRecursive(mutex, pdMS_TO_TICKS(5000)) == pdTRUE) {
                locked = true;
            } else {
                Serial.println("[CRITICAL] RecursiveSemaphoreLockGuard: Failed to acquire mutex!");
            }
        }
    }

    ~RecursiveSemaphoreLockGuard() {
        if (locked) {
            xSemaphoreGiveRecursive(mutex);
        }
    }

    bool isLocked() const { return locked; }
};
```

**2. Changed Mutex Creation**
File: `variants/sensecap_indicator_d1l/hal/CustomRadioLibHal.cpp` (line 226)

```cpp
// Before:
d1l_i2c_mutex = xSemaphoreCreateMutex();

// After:
d1l_i2c_mutex = xSemaphoreCreateRecursiveMutex();
```

**3. Updated All Lock Usages**

**CustomRadioLibHal.cpp** (3 locations):
- Line 91: INT handler task
- Line 129: Polling task
- Line 413: attachInterrupt method

**TCA9535_GPIO.cpp** (5 locations):
- Line 51: begin() method
- Line 125: pinMode() method
- Line 167: digitalWrite() method
- Line 205: digitalRead() method
- Line 230: readAllInputs() method

All changed from:
```cpp
SemaphoreLockGuard lock(d1l_i2c_mutex);
```

To:
```cpp
RecursiveSemaphoreLockGuard lock(d1l_i2c_mutex);
```

---

### Verification & Results

#### Before Fix:
```json
{
  "recv": 0,
  "neighbors": "-none-",
  "rx_air_secs": 0,
  "last_rssi": -124,
  "last_snr": -13.25
}
```

**Critical errors:**
```
[CRITICAL] SemaphoreLockGuard: Failed to acquire mutex within 5s timeout!
[CRITICAL] Possible deadlock or priority inversion detected!
```

#### After Fix (15:23 EET, 2025-12-28):
```json
{
  "recv": 10,
  "sent": 7,
  "flood_tx": 7,
  "flood_rx": 10,
  "neighbors": "B5DFEC9D:500:27",
  "rx_air_secs": 2,
  "noise_floor": -111,
  "last_rssi": -104,
  "last_snr": 6
}
```

✅ **NO CRITICAL ERRORS**
✅ **Virtual interrupts triggering:** `[CustomHAL] Virtual interrupt triggered on pin 103 (HIGH)`
✅ **Packets received:** 10+ packets in first 12 minutes
✅ **Neighbor discovered:** B5DFEC9D (likely another mesh node)
✅ **RX air time accumulating:** 2 seconds of receive activity
✅ **Ping working:** 14.75dB SNR, 893ms round-trip (excellent signal quality)

---

### Files Changed

```
variants/sensecap_indicator_d1l/hal/freertos_util.h
variants/sensecap_indicator_d1l/hal/CustomRadioLibHal.cpp
variants/sensecap_indicator_d1l/hal/TCA9535_GPIO.cpp
```

#### Git Commit Message:
```
Fix: I2C mutex deadlock causing complete RX failure on D1L

Changed d1l_i2c_mutex from regular to recursive mutex to support
nested locking in CustomRadioLibHal::attachInterrupt() ->
TCA9535_GPIO::digitalRead() call chain.

Before: recv:0, neighbors:-none-, CRITICAL mutex timeout errors
After: RX working, neighbors discovered, no errors

- Added RecursiveSemaphoreLockGuard class to freertos_util.h
- Changed xSemaphoreCreateMutex() to xSemaphoreCreateRecursiveMutex()
- Updated all 8 lock usages to use recursive guard

Fixes complete RX failure on D1L hardware.
```

---

### Lessons Learned

#### What Went Right:
1. **User intervention prevented wrong path**: Hardware debugging would have wasted hours
2. **Systematic analysis**: Reading full call chains revealed nested locking pattern
3. **Proper RAII**: SemaphoreLockGuard made it easy to swap mutex types without changing logic
4. **Good timeout**: 5-second timeout on mutex prevented infinite hang, revealed deadlock quickly

#### What Could Be Improved:
1. **Testing recursive calls**: Unit tests for nested mutex scenarios
2. **Static analysis**: Tools like ThreadSanitizer could detect potential deadlocks at compile time
3. **Documentation**: Mark which mutexes support recursion in code comments
4. **Code review**: Nested locking patterns should trigger review discussion

#### Prevention for Future:
- Add comment to `d1l_i2c_mutex` declaration: `// RECURSIVE mutex - supports nested locking`
- Document call chains that require recursion
- Consider lock-free alternatives where possible
- Add assertion: verify mutex type matches usage pattern in debug builds

---

### Technical Details: Why D1L Needs Recursive Mutex

The SenseCAP Indicator D1L uses a **TCA9535 I/O expander** to control the SX1262 LoRa radio pins via I2C. This architectural choice requires:

1. **Shared I2C Bus Protection**: All I2C operations must be mutex-protected to prevent concurrent access
2. **Nested Function Calls**: RadioLib HAL methods call into TCA9535_GPIO methods, both needing I2C access
3. **Single Thread Context**: All calls happen on the same FreeRTOS task during radio initialization

**Therefore, `d1l_i2c_mutex` MUST be a recursive mutex.**

This is unique to the D1L variant - other boards with direct GPIO access to LoRa radios do not require recursive mutexes.

---

### Session Summary

**Total Time:** ~6 hours (overnight monitoring + debugging session)
**Root Cause Identified:** Recursive mutex deadlock in I2C bus protection
**Solution Implemented:** RecursiveSemaphoreLockGuard + recursive mutex creation
**Verification:** Complete RX functionality restored, neighbors discovered, mesh operational

**Status:** ✅ **D1L (Avotiela ⚓) fully operational as mesh repeater**

---

## Additional Resources

- **Hardware Overview**: `docs/hardware/SENSECAP_INDICATOR_IMPLEMENTATION.md`
- **INT Pin Optimization**: `docs/hardware/d1l_int_pin_optimization.md`
- **MeshCore FAQ**: `docs/faq.md`
- **Web Flasher**: https://flasher.meshcore.co.uk (for pre-built binaries)
- **Web Config Tool**: https://config.meshcore.dev (for repeater setup)

---

**Document Version:** 1.0
**Last Updated:** 2025-12-28
**Maintainer:** MeshCore Community
