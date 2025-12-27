# MeshCore D1L - Next Steps After Gemini Fixes

## Context

The MeshCore D1L branches (`nightly` and `feature/d1l-official-repeater`) have all Gemini-reported critical issues fixed and build successfully for `SenseCapIndicator-D1L_repeater`. The following work items are follow-up improvements, **not blockers**.

**Current Status**:
- ✅ All security issues fixed (admin password, no secrets)
- ✅ All buffer safety issues fixed
- ✅ BLE protocol handlers complete with validation
- ✅ Custom HAL with mutex protection implemented
- ✅ DIO1 interrupt wiring correct
- ✅ Documentation created (CLAUDE.md, 624 lines)
- ✅ Build successful: Flash 50.6%, RAM 24.5%

---

## Phase A – Merge Documentation to Nightly

### Goal
Merge CLAUDE.md and documentation reorganization from `feature/d1l-official-repeater` to `nightly` branch.

### Repository Information
- **Path**: `~/Documents/Kods/MeshCore`
- **Source branch**: `feature/d1l-official-repeater` (where CLAUDE.md and docs changes were made)
- **Target branch**: `nightly`

### Steps

1. **Checkout nightly and update**:
   ```bash
   cd ~/Documents/Kods/MeshCore
   git checkout nightly
   git pull origin nightly
   ```

2. **Merge documentation changes from feature/d1l-official-repeater**:
   ```bash
   git merge feature/d1l-official-repeater
   ```

3. **Resolve conflicts if any ONLY in**:
   - `CLAUDE.md`
   - `docs/*`
   - `variants/sensecap_indicator_d1l/*` (README / comments only)

   **Important**: Do not change code in `src/` or HAL files as part of this merge unless required to resolve a clean textual conflict.

4. **After merge, verify build**:
   ```bash
   pio run -e SenseCapIndicator-D1L_repeater
   ```

   Confirm build still succeeds without new warnings.

5. **Commit merge and push**:
   ```bash
   git push origin nightly
   ```

### Deliverables
Report in PR or commit message:
- Whether merge was clean or needed manual conflict resolution
- Build result for `SenseCapIndicator-D1L_repeater`
- List of files merged (CLAUDE.md, docs/hardware/d1l_pin_research.md, etc.)

---

## Phase B – Make RTClib Optional for Variants Without RTC

### Goal
Allow building variants (like D1L) without requiring a physical RTC chip, while keeping RTClib support where actually used.

### Background
The D1L repeater includes RTClib as a dependency but has no RTC hardware. This creates unnecessary binary bloat and confusing dependencies.

### Steps

#### 1. Discover Current RTClib Usage

Search in repository:
```bash
# Find RTClib includes
grep -r "RTClib.h" --include="*.h" --include="*.cpp"

# Find RTC object instantiations
grep -r "RTC_" --include="*.h" --include="*.cpp"
```

Identify:
- Which examples or variants genuinely require RTC (e.g., timekeeping, scheduled tasks)
- Which ones only include RTClib but do not really need it (D1L repeater is one example)

#### 2. Introduce Compile-Time Guard

Define a feature macro:
```cpp
USE_RTC  // or USE_RTCLIB
```

**Default behavior**:
- For boards that actually have hardware RTC, define `USE_RTC` in their `platformio.ini`:
  ```ini
  build_flags =
    -D USE_RTC
  ```
- For D1L and other RTC-less boards, do NOT define `USE_RTC`

**In code**, wrap RTClib includes and RTC-related code:
```cpp
#ifdef USE_RTC
  #include <RTClib.h>
  // RTC-specific code
#endif
```

Ensure that when `USE_RTC` is not defined:
- Code still compiles
- Any RTC-dependent features are either:
  - Disabled with graceful degradation, OR
  - Replaced with safe fallback (e.g., `millis()`-based uptime, or "no RTC" status)

#### 3. Update Affected Files

Likely files to modify:
- `examples/simple_repeater/MyMesh.h` - Check RTC includes
- `examples/simple_repeater/MyMesh.cpp` - Wrap RTC initialization and usage
- `variants/sensecap_indicator_d1l/platformio.ini` - Do NOT define `USE_RTC`
- `variants/*/platformio.ini` - Define `USE_RTC` for boards with RTC chips

#### 4. Validate Builds

Test across at least:
- `SenseCapIndicator-D1L_repeater` (no `USE_RTC` defined)
  ```bash
  pio run -e SenseCapIndicator-D1L_repeater
  ```
- One board that does have RTC (whichever currently uses RTClib)
  ```bash
  # Example: if Heltec v3 uses RTC
  pio run -e heltec_v3_repeater
  ```

Fix any conditional compilation errors found.

#### 5. Commit and Report

Commit message:
```
feat: make RTClib optional for RTC-less variants

- Add USE_RTC compile-time guard
- D1L and RTC-less variants no longer require RTClib
- Boards with RTC chips define USE_RTC in platformio.ini
- Fallback to millis()-based uptime when RTC unavailable
```

**Report should list**:
- Which environments were built
- Where `USE_RTC` is defined (list of variants)
- Any behavior changes for D1L (e.g., no RTC time, only uptime)
- Binary size reduction (if measurable)

---

## Phase C – Automated BLE Protocol Handler Tests (Scaffolding)

### Goal
Create initial scaffolding for automated tests of the BLE command handler logic in `examples/simple_repeater/MyMesh.*` without changing its behavior.

### Background
Current BLE handlers are tested manually. Automated tests would catch regressions in:
- Buffer safety
- Protocol validation
- Error code responses

### Steps

#### 1. Test Harness Design

Create a new test directory:
```bash
mkdir -p tests/ble_handler
```

Implement a minimal C++ test harness that:
- Can construct a `MyMesh`-like object in a host (non-embedded) build, OR
- Factor out the BLE command parsing logic into a testable helper module with no Arduino dependencies (preferred)

**Constraint**: Do not introduce dynamic allocation outside of test-only code.

#### 2. Minimal Test Cases (First Step, Not Exhaustive)

Implement tests for:

**Test: CMD_APP_START**
```cpp
// Test 1: Valid frame length within MAX_FRAME_SIZE
// Expected: RESP_CODE_SELF_INFO response

// Test 2: Oversized frame length > MAX_FRAME_SIZE
// Expected: ERR_CODE_ILLEGAL_ARG error
```

**Test: CMD_SET_ADVERT_LATLON**
```cpp
// Test 1: Valid coordinates inside allowed range
// lat = 56.950266 * COORDS_MULTIPLIER
// lon = 24.132886 * COORDS_MULTIPLIER
// Expected: RESP_CODE_OK

// Test 2: Invalid latitude (lat > MAX_LAT * COORDS_MULTIPLIER)
// Expected: ERR_CODE_ILLEGAL_ARG

// Test 3: Invalid longitude (lon > MAX_LON * COORDS_MULTIPLIER)
// Expected: ERR_CODE_ILLEGAL_ARG
```

#### 3. Build Integration

Add test build target using PlatformIO or CMake (whichever fits current repo layout best).

Example PlatformIO native test:
```ini
[env:native_test]
platform = native
test_framework = unity
build_flags =
  -D UNIT_TEST
  -I examples/simple_repeater
  -I src/helpers
```

Ensure tests can:
- Run on host (PC) if possible, OR
- At least cross-compile for sanity check

#### 4. Test Execution

```bash
# Run tests
pio test -e native_test

# Or if using CMake
mkdir build && cd build
cmake ..
make test
```

#### 5. Commit and Report

Commit message:
```
test: add initial scaffolding for BLE command handler tests

- Created tests/ble_handler/ directory
- Implemented tests for CMD_APP_START buffer validation
- Implemented tests for CMD_SET_ADVERT_LATLON coordinate validation
- Uses Unity test framework
- Tests run on native platform (host PC)
```

**Report should specify**:
- How to run the tests
- Which commands are covered by the first tests
- Any limitations (e.g., partially mocked environment, no actual radio)
- Test results (passed/failed)

---

## Phase D – Hardware INT Pin Optimization for TCA9535 (Optional, Exploratory)

### Goal
Investigate reducing reliance on 1 ms polling by making better use of the IO expander interrupt pin, without breaking current behavior.

### Background
Current implementation uses FreeRTOS polling task (1 ms interval) for virtual interrupts. The TCA9535 has a hardware INT pin that could reduce CPU usage and improve interrupt latency.

### Steps

#### 1. Analysis

**Identify TCA9535 IRQ pin wiring**:
- Check `docs/hardware/d1l_pin_research.md`
- Check `variants/sensecap_indicator_d1l/target.h` and `target.cpp`
- Look for `IO_EXPANDER_IRQ` define (currently GPIO 42)

**Review current implementation**:
- `variants/sensecap_indicator_d1l/hal/CustomRadioLibHal.h`
- `variants/sensecap_indicator_d1l/hal/TCA9535_GPIO.h`

**Questions to answer**:
- Is the IRQ pin currently used, or only virtual polling?
- How is TCA9535 configured for interrupts?
- What is the IRQ pin behavior (active low, edge-triggered, etc.)?

#### 2. Design Proposal

Propose (as comments or documentation) a design to:

**Option A: Interrupt-triggered polling**
```cpp
// ESP32 GPIO 42 IRQ handler
// When TCA9535 asserts INT (any pin changes):
// 1. Wake polling task or set event flag
// 2. Task reads all virtual interrupt pins
// 3. Trigger callbacks as needed
```

Benefits:
- Polling only happens when needed (interrupt-driven)
- Can increase polling interval from 1ms to 10ms or disable when idle

**Option B: Hybrid approach**
- Use INT pin to wake from sleep
- Keep 1ms polling when active
- Enter low-power mode when no interrupts pending

#### 3. Implementation (Only If Clearly Safe)

If the design is straightforward, implement a minimal change:

```cpp
// In CustomRadioLibHal::init()
pinMode(IO_EXPANDER_IRQ, INPUT_PULLUP);
attachInterrupt(digitalPinToInterrupt(IO_EXPANDER_IRQ),
                ioExpanderISR, FALLING);

// ISR sets flag for polling task
static void IRAM_ATTR ioExpanderISR() {
  // Set event flag - DO NOT call I2C in ISR
  xTaskNotifyFromISR(pollTaskHandle, 1, eSetBits, NULL);
}

// Polling task waits for notification
void pollTask(void* parameter) {
  while (true) {
    // Wait for IRQ or timeout
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(10));
    hal->pollVirtualInterruptsInternal();
  }
}
```

**Important constraints**:
- No I2C operations inside ISR (only set flag)
- Keep existing polling as fallback
- Ensure no busy-wait loops

#### 4. Build and Test

```bash
pio run -e SenseCapIndicator-D1L_repeater
```

Document any change in:
- Serial logging (interrupt events)
- CPU usage (if measurable)
- Interrupt latency (if measurable)

#### 5. Commit and Report

If implemented, commit with:
```
feat: use IO expander IRQ pin to reduce polling load

- Attach interrupt handler to GPIO 42 (TCA9535 INT pin)
- Polling task now event-driven instead of continuous 1ms
- Falls back to 10ms timeout if no interrupts
- Reduces CPU usage while maintaining responsiveness
```

**Report**:
- Whether polling interval changed
- Any measured or observed behavioral difference
- Serial log output showing IRQ events
- Note: **Hardware testing required** to verify proper operation

**If NOT implemented**:
Document the design in `CLAUDE.md` or `docs/hardware/` for future reference.

---

## Phase E – savePrefs() Error Reporting (Design Only, No Refactor Yet)

### Goal
Prepare a design note for future refactoring of `savePrefs()` so it can report errors, but **DO NOT change existing behavior now**.

### Background
Current `savePrefs()` returns `void`, so SPIFFS write failures cannot be detected. This is documented but not ideal for production firmware.

### Steps

#### 1. Locate savePrefs Implementation

Find where `savePrefs()` is defined:
```bash
grep -r "void savePrefs" --include="*.cpp" --include="*.h"
```

Expected locations:
- `examples/simple_repeater/MyMesh.cpp`
- `examples/simple_room_server/MyMesh.cpp`
- Others?

Analyze what it currently does:
- Filesystem writes (SPIFFS, LittleFS, etc.)
- What errors could occur? (filesystem full, corrupted, not mounted)

#### 2. Design Options (Document in CLAUDE.md or docs/)

**Option A: Change savePrefs() signature to return bool**
```cpp
// Before:
void savePrefs();

// After:
bool savePrefs();  // Returns true on success, false on error
```

**Pros**:
- Simple, idiomatic C++ error handling
- Clear success/failure indication

**Cons**:
- Requires changes to all call sites across all examples
- Breaking change for existing code

---

**Option B: Introduce new function, keep legacy wrapper**
```cpp
// New function with error reporting
bool savePrefsWithStatus();

// Legacy wrapper for backward compatibility
void savePrefs() {
  (void)savePrefsWithStatus();  // Ignore return value
}
```

**Pros**:
- Backward compatible - existing code still works
- Allows gradual migration

**Cons**:
- Two functions doing same thing (confusing)
- Legacy code still can't detect errors

---

**Option C: Add global "lastSaveError" variable**
```cpp
extern int lastSaveError;  // 0 = success, -1 = error

void savePrefs() {
  // ... do save ...
  if (error) {
    lastSaveError = -1;
  } else {
    lastSaveError = 0;
  }
}

// Callers can check:
savePrefs();
if (lastSaveError != 0) {
  // Handle error
}
```

**Pros**:
- No signature change
- Optional error checking

**Cons**:
- Global mutable state (not thread-safe)
- Easy to forget to check error
- Not idiomatic C++

---

#### 3. Document Trade-Offs

Create document: `docs/design/saveprefs_error_handling.md`

**Content should include**:

1. **Current Situation**
   - `savePrefs()` returns void
   - Documented in comments at call sites
   - No way to detect SPIFFS write failures

2. **Desired Behavior**
   - Detect and report filesystem errors
   - Allow caller to retry or warn user
   - Maintain backward compatibility if possible

3. **Design Options** (A, B, C from above)

4. **Recommended Approach**
   - Suggest Option A or B with justification
   - List affected files (all call sites)
   - Migration strategy

5. **Required Changes**
   If Option A (bool return value):
   ```
   Files to modify:
   - examples/simple_repeater/MyMesh.cpp (3 call sites)
   - examples/simple_room_server/MyMesh.cpp (? call sites)
   - examples/simple_sensor/SensorMesh.cpp (? call sites)
   - Any other examples using savePrefs()

   Change pattern:
   savePrefs();  // Before

   if (!savePrefs()) {  // After
     writeErrFrame(ERR_CODE_ILLEGAL_ARG);
     MESH_DEBUG_PRINTLN("ERROR: Failed to save preferences");
   }
   ```

6. **Potential Risks**
   - Breaking changes for existing variants
   - Need comprehensive testing across all boards
   - May expose previously hidden filesystem issues

#### 4. No Code Changes Yet

This phase is **purely documentation/design**.

Do NOT implement any code changes - just prepare the design document.

#### 5. Commit and Report

Commit message:
```
docs: add design document for savePrefs() error reporting

- Documents current limitations (no error return)
- Proposes three design options (A, B, C)
- Recommends Option A with migration strategy
- Lists all affected files and call sites
- Notes: No code changes in this commit, design only
```

**Report should include**:
- Number of `savePrefs()` call sites found
- Recommended option (A, B, or C) with reasoning
- Estimated effort for implementation (file count, test coverage needed)

---

## Execution Rules

### Order of Execution
Perform phases **in order**: A → B → C → D → E

### Build Validation
After each phase that changes code, run at least one D1L repeater build:
```bash
pio run -e SenseCapIndicator-D1L_repeater
```

If build fails, capture full error output and stop.

### Error Handling
If any build fails or a design assumption turns out to be wrong:
- **STOP** - do not proceed to next phase
- **REPORT** the specific issue with:
  - Full error message
  - File and line number
  - Current git diff
- Do not guess a workaround without investigation

### Testing Requirements
For phases B, C, D that modify code:
- Test at minimum: `SenseCapIndicator-D1L_repeater`
- Test recommended: One other variant (e.g., `heltec_v3_repeater`)
- Report build status for both

### Git Workflow
Each phase should result in **one clean commit**:
- Descriptive commit message following conventional commits format
- Only changes related to that phase
- No reformatting of unrelated code

### Documentation
Update `CLAUDE.md` with results from each phase:
- Phase completion status
- Design decisions made
- Any limitations discovered

---

## Success Criteria

### Phase A
- ✅ `CLAUDE.md` merged to nightly
- ✅ Documentation reorganization merged
- ✅ Build succeeds on nightly branch
- ✅ No new warnings introduced

### Phase B
- ✅ D1L builds without RTClib dependency
- ✅ Variants with RTC still work correctly
- ✅ Binary size reduction measured and documented
- ✅ Graceful degradation when RTC unavailable

### Phase C
- ✅ Test framework integrated
- ✅ At least 2 BLE commands have automated tests
- ✅ Tests run successfully on host (native)
- ✅ Documentation for running tests

### Phase D
- ✅ Design documented (minimum requirement)
- ✅ If implemented: Interrupt-driven polling working
- ✅ No increase in interrupt latency
- ✅ Builds successfully

### Phase E
- ✅ Design document created
- ✅ All call sites identified
- ✅ Recommended approach documented with justification
- ✅ Migration strategy defined

---

## Notes

- **Hardware Testing Required**: Phases B and D changes should be validated on actual D1L hardware when available
- **Backward Compatibility**: Maintain compatibility with existing variants whenever possible
- **Code Quality**: Follow existing MeshCore coding standards (no dynamic allocation, embedded thinking)
- **Documentation**: Update `CLAUDE.md` and relevant docs after each phase

---

## Related Files

- `CLAUDE.md` - Main project context for AI assistants
- `docs/hardware/d1l_pin_research.md` - Hardware analysis for D1L
- `examples/simple_repeater/MyMesh.cpp` - BLE command handlers
- `examples/simple_repeater/ProtocolCodes.h` - Protocol constants
- `variants/sensecap_indicator_d1l/hal/CustomRadioLibHal.h` - Custom HAL implementation
- `variants/sensecap_indicator_d1l/hal/TCA9535_GPIO.h` - IO expander wrapper

---

**Last Updated**: 2025-12-27
**Status**: Ready for execution
**Assigned**: Claude Code agents or contributors
