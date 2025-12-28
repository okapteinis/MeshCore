# CLAUDE.md - MeshCore AI Assistant Context Guide

## ⚠️ PROJECT STATUS: SenseCAP Indicator D1L Fork

**This document covers the D1L-focused fork of MeshCore (nightly branch).**

### Current D1L Implementation Status

- **Radio (LoRa RX/TX)**: ✅ **FULLY FUNCTIONAL**
  - SX1262 driver working reliably
  - EU Narrow mode (869.618 MHz) operational
  - Mesh networking proven stable
  - Repeater mode validated

- **Display/Screen (ILI9341)**: 🔄 **IN PROGRESS - NOT OPERATIONAL**
  - Currently non-functional (work in progress)
  - Display driver debugging required
  - Expected completion: **Q2 2026**

- **Development Timeline**: 
  - **Current**: Maintenance mode (Q4 2025)
  - **Q2 2026**: Full feature push - display support, optimizations, testing
  - **Nightly Branch**: Primary development branch for D1L

### What This Means for Contributors & Users

- ✅ **Ready to use as a LoRa radio/repeater**: Radio is production-stable
- ❌ **Not ready as a mobile companion app**: Display support needed for that
- 🌟 **Actively seeking contributors**: Display driver expertise welcome
- 📄 **See STATUS.md for full details**: Comprehensive feature breakdown included

---



This document provides comprehensive context for AI assistants, collaborators, and developers working on MeshCore. Refer to this guide when planning contributions, understanding architecture, or implementing new features.

---

## 1. Project Identity & Purpose

**MeshCore** is a lightweight, portable C++ library for embedded systems that enables **multi-hop packet routing** using LoRa radios and other packet-based wireless technologies.

### Core Vision
- Create **resilient, decentralized communication networks** that operate without internet infrastructure
- Support off-grid scenarios: emergency response, disaster recovery, tactical operations, remote sensor networks
- Balance simplicity with scalability—providing lower-level control than Meshtastic while being more accessible than Reticulum

### Key Differentiators
- **Lightweight**: Designed for constrained embedded systems (ESP32, nRF52, RP2040)
- **Portable**: Support for multiple platforms with clear HAL abstractions
- **No Central Authority**: Fully decentralized mesh routing
- **Battery-Friendly**: Low-power consumption via intermittent polling and efficient packet forwarding

### Target Use Cases
- Emergency communication when infrastructure fails
- Remote IoT sensor networks requiring multi-hop relay
- Outdoor/tactical scenarios (hiking, military, security)
- Off-grid community networks
- BLE/USB companion apps with wireless mesh backends

---

## 2. Repository Structure

```
MeshCore/
├── src/                      # Core mesh library (platform-agnostic)
│   ├── MeshCore.h            # Main public API
│   ├── Mesh.h/.cpp           # Mesh routing logic
│   ├── Dispatcher.h/.cpp     # Packet reception dispatcher
│   ├── Packet.h/.cpp         # Packet structure and serialization
│   ├── Identity.h/.cpp       # Node identity and cryptographic keys
│   ├── Utils.h/.cpp          # Utility functions
│   └── helpers/              # Platform-specific helpers
│       ├── ui/               # Display drivers (ST7735, E290, etc.)
│       ├── bridges/          # Bridge implementations (WiFi, BLE, etc.)
│       ├── radiolib/         # RadioLib helper classes
│       └── ESP32Board.h      # ESP32-specific utilities
│
├── variants/                 # Device-specific implementations (65+ supported boards)
│   ├── sensecap_indicator_d1l/   # SenseCAP Indicator D1L with custom HAL
│   ├── heltec_v3/            # Heltec LoRa v3
│   ├── ebyte_eora_s3/        # Ebyte EoRa S3
│   └── ... (many more)
│
├── examples/                 # Reference applications
│   ├── companion_radio/      # Phone app companion (350 contacts, 40 channels)
│   ├── simple_repeater/      # Standalone packet forwarding
│   ├── simple_room_server/   # Message server with ACL support
│   ├── simple_secure_chat/   # Terminal-based encrypted chat
│   └── simple_sensor/        # Remote sensor node template
│
├── docs/                     # Technical documentation
│   ├── packet_structure.md   # Wire format specification
│   ├── payloads.md           # Payload type definitions
│   ├── stats_binary_frames.md # Statistics protocol
│   ├── faq.md                # Frequently asked questions
│   └── hardware/             # Hardware implementation guides
│
├── boards/                   # PlatformIO board definitions
├── lib/                      # Third-party libraries (minimal)
├── arch/                     # Architecture-specific code (ESP32, nRF52, etc.)
├── .clang-format             # Code formatting rules
├── platformio.ini            # PlatformIO project configuration
├── library.json              # Arduino library metadata
└── license.txt               # MIT License
```

### Key Directories to Understand

- **src/** - Never modify core source arbitrarily; changes affect all variants
- **variants/sensecap_indicator_d1l/** - Active development area; contains custom HAL for IO expander
- **examples/** - Start here to understand how MeshCore is used
- **docs/packet_structure.md** - Essential reading for protocol understanding
- **.clang-format** - Specifies formatting for NEW code (do NOT retroactively reformat)

---

## 3. Critical Coding Standards (STRICT)

### Memory Management - CRITICAL
**RULE**: No dynamic memory allocation (`new`, `malloc`, `delete`, `free`) except during `setup()` and `begin()` initialization functions.

**Rationale**: Embedded systems have limited heap; malloc fragmentation causes crashes after days/weeks of operation. Use fixed-size buffers and stack allocation instead.

**Examples**:
```cpp
// WRONG - Dynamic allocation in runtime code
void onPacket() {
  uint8_t* buffer = new uint8_t[256];  // NEVER do this in handlers
  // ...
  delete buffer;
}

// CORRECT - Static/stack allocation
void onPacket() {
  uint8_t buffer[256];  // Stack allocation (if size is small)
  // or use pre-allocated member variable
}

// CORRECT - Dynamic allocation only in setup
void begin() {
  _buffer = new uint8_t[4096];  // Okay during initialization
}
```

### Code Style - DO NOT REFORMAT
1. Follow the existing brace/indentation style in the file you're editing
2. `.clang-format` exists for NEW files and NEW code only
3. **DO NOT retroactively reformat existing code** - this creates unnecessary diffs that obscure real changes
4. Indentation: 2 spaces (not tabs)
5. Column limit: 110 characters
6. Brace style: Attach (K&R style)

**Example of correct style**:
```cpp
class MyClass {
  int value;

public:
  MyClass() : value(0) {}

  void process() {
    if (value > 0) {
      // code here
    } else {
      // other code
    }
  }
};
```

### Embedded Systems Thinking
- Think about memory constraints: ESP32 has ~520KB free RAM after OS overhead
- Avoid string concatenation in runtime paths
- Use const references to avoid copies
- Cache results if computing expensive operations repeatedly
- Profile before optimizing; measure memory usage

### Other Standards
- Use meaningful variable names (avoid single letters except loop counters)
- Keep functions focused and small (< 50 lines preferred)
- Use Arduino style (`pinMode()`, `digitalWrite()`) for platform compatibility
- Use `#pragma once` for header guards (not `#ifndef`)
- Comment WHY, not WHAT (code shows what; comments explain reasoning)

---

## 4. Current Development Focus (Nightly Branch)

The **nightly** branch is the active development branch. PR base should be **dev** (not main).

### SenseCAP Indicator D1L (PRIMARY FOCUS)

**Status**: Implementation underway for companion radio and repeater support

**Key Features**:
- 480x480 capacitive touch TFT display
- ESP32-S3 dual-core MCU
- 8MB Flash + 8MB OPI PSRAM
- Semtech SX1262 LoRa radio
- TCA9535 IO expander for radio control pins
- Optional RP2040 sensor coprocessor

**Completed Work**:
1. ✅ Custom HAL Implementation
   - `hal/TCA9535_GPIO.h` - GPIO wrapper for IO expander
   - `hal/CustomRadioLibHal.h` - Full RadioLib GODMODE HAL
   - FreeRTOS polling task for interrupt handling (1ms latency)
   - Virtual pin routing: 100-199 maps to TCA9535, 0-99 maps to ESP32 GPIO

2. ✅ EU NARROW Mode Migration
   - Frequency: 869.618 MHz
   - Bandwidth: 62.5 kHz
   - Spreading Factor: 8
   - Coding Rate: 8/5
   - Ready for apraide.lv network integration

3. ✅ Security Improvements
   - Removed hardcoded credentials
   - Runtime configuration via Serial CLI
   - Persistent SPIFFS storage for settings
   - Parameter validation for all radio operations

4. ✅ Hardware Fixes (15 critical/high/medium priority)
   - I2C bus mutex (FreeRTOS RAII wrapper)
   - PSRAM runtime detection with fallback
   - Serial initialization guard
   - Buffer overflow fixes
   - Global static initialization order fixes
   - 5-second timeout on radio.begin()
   - Printf format string vulnerabilities closed

5. ✅ **Security Hardening (December 2025)** ⚠️ **NEW**
   - **Comprehensive Code Review**: 10,000+ word security analysis covering logic, bugs, vulnerabilities, code quality
   - **5 Critical Vulnerabilities Fixed**:
     - **Race Conditions in CustomRadioLibHal** - Added virtual_int_mutex to protect virtualInterrupts[] array
     - **Buffer Overflow in Packet::readFrom()** - Comprehensive bounds checking before all memcpy operations
     - **TRACE Packet Buffer Overread** - Added payload_len >= 9 validation before parsing
     - **Private Key Exposure** - Wrapped debug output with #ifdef ENABLE_PRIVATE_KEY_EXPORT
     - **Time Manipulation Attack** - Timestamp validation (year 2000-2038) in CMD_SET_DEVICE_TIME
   - **Documentation Created**:
     - `CODE_REVIEW_REPORT.md` - 755 lines of security analysis
     - `CRITICAL_ISSUES_GITHUB.md` - Detailed GitHub issue templates
     - `REMAINING_ISSUES.md` - 3 HIGH/MEDIUM + 10+ LOW priority issues to address
   - **Branch**: `security/critical-fixes-5-vulns` (PR pending to nightly)
   - **Status**: 62.5% of HIGH+ priority issues resolved, backward compatible

6. ✅ **I2C Mutex Deadlock Fix (December 2025)** ⚠️ **CRITICAL**
   - **Issue**: Complete RX failure (recv:0) due to recursive mutex deadlock in D1L I2C bus protection
   - **Root Cause**: `CustomRadioLibHal::attachInterrupt()` → `TCA9535_GPIO::digitalRead()` nested locking with regular mutex
   - **Solution**: Converted `d1l_i2c_mutex` to recursive mutex (`xSemaphoreCreateRecursiveMutex()`)
   - **Result**: RX fully functional, neighbors discovered, ping working (14.75dB SNR)
   - **Files Changed**:
     - `variants/sensecap_indicator_d1l/hal/freertos_util.h` - Added RecursiveSemaphoreLockGuard
     - `variants/sensecap_indicator_d1l/hal/CustomRadioLibHal.cpp` - Changed mutex creation + 3 usages
     - `variants/sensecap_indicator_d1l/hal/TCA9535_GPIO.cpp` - Changed 5 mutex usages
   - **Documentation**: `docs/hardware/D1L_GUIDE.md` - Complete flashing guide + post-mortem
   - **Branch**: `feature/d1l-rx-deadlock-fix`
   - **Status**: ✅ Verified working on hardware, ready for merge
   - **IMPORTANT**: D1L REQUIRES recursive mutex for d1l_i2c_mutex due to TCA9535 I/O expander architecture

**In Progress**:
- **TCA9535 INT Pin Optimization** (December 2025) 🚀 **NEW**
  - **Goal**: Replace 5ms polling with hardware interrupt-driven approach
  - **Status**: Phase D.1 Complete (current implementation analyzed)
  - **Design**: ESP32 GPIO 42 ISR + FreeRTOS deferred task for I2C reads
  - **Benefits**: 95-99% reduction in I2C bus load, lower power consumption
  - **Documentation**: `docs/hardware/d1l_int_pin_optimization.md`
  - **Next**: Phase D.2 (finalize IRQ-assisted design) → D.3-D.5 (implementation + testing)
- Hardware testing on physical devices
- Sensor integration (RP2040 coprocessor)
- OTA update support

**Known Limitations**:
- Untested on hardware (development constraints)
- RP2040 sensor readings not yet implemented
- GPS support not implemented
- Touch screen uses polling mode (not interrupt mode)

**Build Commands**:
```bash
# Companion radio with USB
pio run -e SenseCapIndicator-D1L_comp_radio_usb -t upload --upload-port /dev/cu.usbserial-XXXX

# Repeater mode (recommended)
pio run -e SenseCapIndicator-D1L_repeater -t upload --upload-port /dev/cu.usbserial-XXXX

# Note: Replace /dev/cu.usbserial-XXXX with actual port (use: ls /dev/cu.usbserial-*)
```

**Monitoring Tools**:
```bash
# Standard PlatformIO monitor
pio device monitor --port /dev/cu.usbserial-XXXX --baud 115200

# D1L logger (recommended - more reliable, with heartbeat and event filtering)
# With explicit port
python3 tools/d1l_logger.py --port /dev/cu.usbserial-XXXX

# Auto-detect port (macOS/Linux)
python3 tools/d1l_logger.py

# Custom log directory
python3 tools/d1l_logger.py --port /dev/cu.usbserial-XXXX --log-dir ./custom_logs
```

### Supporting Hardware (Secondary)
- Heltec v3/v4 boards (mature)
- Ebyte EoRa S3 (mature)
- RAK Wireless boards (various stages)
- Generic E22 LoRa modules (basic support)

---

## 5. Protocol & Network Architecture

### Packet Structure (Wire Format)

**Maximum Packet Size**: 255 bytes (including header)

```
Packet Layout:
[Header(1)] [TransportCodes(4-opt)] [PathLen(1)] [Path(0-64)] [Payload(0-184)]
```

**Header Format** (8 bits):
- Bits 0-1: Route Type (2 bits)
- Bits 2-5: Payload Type (4 bits)
- Bits 6-7: Payload Version (2 bits)

### Route Types
- `ROUTE_TYPE_TRANSPORT_FLOOD` (0x00): Flood routing + transport codes for zoning/filtering
- `ROUTE_TYPE_FLOOD` (0x01): Flood with dynamic path building
- `ROUTE_TYPE_DIRECT` (0x02): Direct route (path provided)
- `ROUTE_TYPE_TRANSPORT_DIRECT` (0x03): Direct route + transport codes

### Payload Types
- `PAYLOAD_TYPE_REQ` (0x00): Request with dest/src hashes and MAC
- `PAYLOAD_TYPE_RESPONSE` (0x01): Response to request
- `PAYLOAD_TYPE_TXT_MSG` (0x02): Plain text message
- `PAYLOAD_TYPE_ACK` (0x03): Simple acknowledgment
- `PAYLOAD_TYPE_ADVERT` (0x04): Node advertisement (identity)
- `PAYLOAD_TYPE_GRP_TXT` (0x05): Group text (unencrypted)
- `PAYLOAD_TYPE_GRP_DATA` (0x06): Group datagram
- `PAYLOAD_TYPE_ANON_REQ` (0x07): Anonymous request
- `PAYLOAD_TYPE_MULTIPART` (0x0A): Multi-packet message
- `PAYLOAD_TYPE_CONTROL` (0x0B): Control/discovery packets
- `PAYLOAD_TYPE_RAW_CUSTOM` (0x0F): Raw application-defined data

### Routing Architecture

1. **Flood Mode**: Packets propagate across all nodes, building up the path as they hop
2. **Direct Mode**: Sender provides the path; no flooding needed
3. **Repeater Role**: Companion nodes DON'T retransmit (to prevent routing loops)
4. **Transport Codes**: Optional zoning/filtering mechanism for large networks

**Hop Limiting**:
- Configurable maximum hops
- Prevents infinite loops
- Default: ~15 hops (configurable per build)

### Identity & Encryption

**Node Identity**:
- 32-byte public key (ECDH/Curve25519)
- 32-byte private key (kept secure on device)
- Node hash: First byte of SHA256(public_key)
- Unique identity enables peer-to-peer messaging

**Encryption**:
- AES-256-CTR for payload encryption
- ECDH key agreement for peer-specific secrets
- HMAC for message authentication
- Ephemeral keys support for anonymous requests

---

## 6. Dependencies

### Core Dependencies (library.json)

| Library | Version | Purpose |
|---------|---------|---------|
| **RadioLib** | ^7.3.0 | LoRa/SX1262 radio control (CRITICAL) |
| **Crypto** (rweather) | ^0.4.0 | AES-256, ECDH encryption |
| **RTClib** (Adafruit) | ^2.1.3 | Real-time clock/timestamp |
| **Melopero RV3028** | ^1.1.0 | RTC device driver |
| **CayenneLPP** | 1.6.1 | Sensor data encoding |

### Framework-Specific
- **Arduino Framework** (PlatformIO): Standard for ESP32/nRF52/RP2040
- **FreeRTOS**: Multitasking (ESP32)
- **SPIFFS**: File storage for config (ESP32)

### Variant-Specific (SenseCAP D1L)
- **LovyanGFX**: Display driver (TFT/touchscreen support)
- Custom TCA9535 IO expander driver (included)
- Custom RadioLib HAL for GPIO expander routing

### Build System
- **PlatformIO Core** (command line) or **PlatformIO IDE** (VS Code)
- Python 3.x for build scripts
- Git for version control

---

## 7. Known Issues & Limitations

### SenseCAP Indicator D1L
1. **Untested on Hardware**: Variant implemented but not yet validated on physical device
2. **Sensor Integration Incomplete**: RP2040 coprocessor communication not yet implemented
3. **Touch Screen Polling**: Uses I2C polling instead of interrupt (inherited design)
4. **OTA Updates**: Not enabled by default (can be enabled with build flags)
5. **IO Expander Latency**: I2C-based GPIO access slower than direct GPIO (acceptable for LoRa timing)

### Network Protocol
1. **Path Hashing (V2)**: Path verification not yet implemented (V2 spec in discussion)
2. **Multipart Handling**: Large messages still experimental
3. **Compression**: LZW compression planned but not implemented
4. **Dynamic Coding Rate**: No automatic adjustment based on link quality (V2 roadmap)

### Platform Limits
1. **Companion Radio**: Max 350 contacts, 40 groups (firmware design limit)
2. **Max Hops**: ~15 hops default (tunable but increases latency)
3. **Payload Size**: Max 184 bytes of actual payload (255 total - headers)
4. **Storage**: SPIFFS file system limited (32MB on ESP32)

### Stability
1. **Long-running Devices**: Occasionally need reboot after weeks (investigate heap fragmentation)
2. **High Traffic**: May exceed heap under extreme loads (40+ msg/sec)
3. **WiFi Bridge**: OTA updates can interrupt mesh (switch to LTE/4G in future)

---

## 8. Testing Requirements & Contributing Workflow

### Before Submitting a PR

1. **Code Quality Checks**
   - No new dynamic allocations in runtime paths
   - Follow existing code style (no retroactive reformatting)
   - Compile without warnings (`-Wall -Wextra` enabled)
   - All included headers are used

2. **Functional Testing**
   - For **core library changes**: Test on at least 2 different boards
   - For **variant-specific changes**: Test on target board if available
   - For **UI changes**: Verify display output and touch responsiveness
   - For **radio changes**: Transmit/receive test with other nodes

3. **Memory Testing** (if adding features)
   - Use PlatformIO's memory profiler: `pio run --verbose`
   - Ensure heap usage is stable over time (no fragmentation)
   - Check stack depth of any new tasks

4. **Documentation**
   - Update relevant .md files in `/docs/`
   - Add comments to public APIs
   - Update BUGFIXES.md or README for user-facing changes

### Pull Request Workflow

1. **Branch**: Create feature branch from `dev` (not `main`)
2. **Small PRs**: Aim for <500 lines changed per PR
3. **Clear Commit Messages**:
   - First line: brief summary (< 60 chars)
   - Blank line
   - Detailed explanation (wrap at 80 chars)
4. **Reference Issues**: Link to GitHub issues in description
5. **Base Branch**: Always use `dev` as base, NOT `main`

### Example PR Description
```
## Summary
Add memory-efficient message queuing for high-traffic repeaters

## Changes
- Implemented ring buffer (no dynamic allocation)
- Reduced memory footprint by 40%
- Maintains order for multipart messages

## Testing
- Tested on Heltec v3 and Ebyte EoRa S3
- Forwarded 10K packets without memory growth
- Verified no message loss up to 50 msg/sec

## Checklist
- [x] No dynamic allocation in runtime
- [x] Code follows project style
- [x] Compiled without warnings
- [x] Tested on 2+ boards
```

### Contribution Philosophy

From README:
> **Keep it simple.** Think embedded, not high-level programming. Keep code concise without unnecessary layers. Favor clarity and correctness over cleverness.

---

## 9. Important Files to Reference (Task-Specific)

### Understanding the Architecture
- **Start Here**: `src/MeshCore.h` - Public API entry point
- **Packet Handling**: `src/Dispatcher.h` → `src/Mesh.h` (inheritance chain)
- **Protocol**: `docs/packet_structure.md`, `docs/payloads.md`
- **Example Usage**: `examples/simple_repeater/simple_repeater.cpp`

### Radio Configuration
- **RadioLib Integration**: `src/helpers/radiolib/` directory
- **Default Settings**: `platformio.ini` (build flags `LORA_FREQ`, `LORA_BW`, `LORA_SF`)
- **Custom HAL**: `variants/sensecap_indicator_d1l/hal/CustomRadioLibHal.h` (reference implementation)

### Display & UI
- **Display Drivers**: `src/helpers/ui/` (ST7735, ST7789, SH1106, E290, etc.)
- **SenseCAP Display**: `variants/sensecap_indicator_d1l/hal/SCIndicatorDisplay.h`
- **Button Handling**: `src/helpers/ui/MomentaryButton.h`

### Identity & Encryption
- **Node Setup**: `src/Identity.h/.cpp` (key generation, storage)
- **Crypto Library**: Uses rweather/Crypto (AES, ECDH, HMAC)
- **Key Persistence**: Check variant's SPIFFS integration

### Security
- **Removed Features**: Hardcoded credentials were removed (see BUGFIXES.md)
- **Config Storage**: SPIFFS (ESP32) - see variants for implementation
- **Serial CLI**: Example in SenseCAP repeater variant

### Memory Management
- **ESP32 Heap**: Check `esp_get_free_heap()` in debug builds
- **Stack Depth**: Use FreeRTOS `uxTaskGetStackHighWaterMark()`
- **Buffers**: Defined as macros in `src/MeshCore.h` (`MAX_PACKET_PAYLOAD`, `MAX_PATH_SIZE`)

### Testing & Debugging
- **Serial Output**: 115200 baud (configured in platformio.ini)
- **Debug Builds**: Add `build_type = debug` to platformio.ini section
- **Radio Debugging**: RadioLib has debug output (disabled by default)

---

## 10. Security Considerations & Communication Channels

### Security Implementation

**Strengths**:
- End-to-end encryption with AES-256-CTR
- ECDH key agreement prevents MITM attacks
- HMAC prevents tampering
- No plaintext node identifiers (hashed)

**Weaknesses & Mitigations**:
- No centralized PKI (distributed trust - appropriate for P2P)
- Flooding topology reveals network topology (acceptable for tactical use)
- DoS possible with spam messages (implement rate limiting if needed)
- No perfect forward secrecy (acceptable for non-real-time systems)

**Removed Insecurities** (see BUGFIXES.md):
- ❌ Hardcoded WiFi credentials (removed)
- ❌ Unencrypted serial protocol (secured in variants)
- ❌ Fixed default encryption keys (now per-node)
- ❌ Printf format string vulnerabilities (fixed)

### Security for Contributors

**Public Communication**:
- GitHub Issues: Bug reports, feature requests (public)
- GitHub Discussions: Architecture questions (public)
- Discord: Real-time community chat (invitation-only, ask in issues)

**Private Security Issues**:
- Do NOT post security vulnerabilities in public issues
- Contact maintainer (Scott Powell) via email or Discord DM
- Allow 30 days for patch before public disclosure

### Operational Security (Users)

1. **Network Privacy**: Assume local traffic may be observed (use encryption for sensitive data)
2. **Key Management**: Keep private keys safe; loss of key = loss of identity
3. **Firmware Integrity**: Verify signatures before flashing (feature planned)
4. **Access Control**: Repeaters and Room Servers support ACL (implement if needed)

### Dependencies Security

- **RadioLib**: Regularly updated, maintained by jgromes
- **Crypto Library**: Audited implementation (rweather)
- **LovyanGFX**: Community-maintained, no critical issues
- **Review Dependencies**: Check advisories before major releases

---

## 11. Roadmap & Future Directions

### Near-term (Nightly Branch)
- [ ] Hardware testing on SenseCAP D1L
- [ ] Sensor integration (RP2040 coprocessor)
- [ ] Stability testing (long-running operation)
- [ ] Performance optimization (high-traffic scenarios)

### Mid-term (V1.x)
- [ ] V2 Protocol spec (path hashes, improved encryption)
- [ ] Dynamic Coding Rate (adapt to signal quality)
- [ ] LZW message compression
- [ ] Multiple virtual nodes per device
- [ ] Bridge mode standardization

### Long-term (V2.x)
- [ ] Sub-mesh support (off-grid client repeat mode)
- [ ] Cross-band bridges (LoRa ↔ WiFi/cellular)
- [ ] Advanced path finding algorithms
- [ ] Hardware security module integration
- [ ] Firmware signature verification

---

## 12. Getting Help & Reporting Issues

### Resources

1. **Documentation**: `/docs/` directory - FAQ, protocol, hardware guides
2. **Examples**: `/examples/` - Reference implementations for your use case
3. **Variant READMEs**: `/variants/*/README.md` - Board-specific info

### Reporting Issues

**Expected Information**:
- Board model (e.g., "Heltec v3", "SenseCAP Indicator D1L")
- MeshCore version (git commit hash)
- Steps to reproduce
- Expected vs actual behavior
- Serial output/logs (if applicable)

**Where to Report**:
- **Bugs**: GitHub Issues (if you can public)
- **Security**: Maintainer email (see above)
- **Questions**: GitHub Discussions or Discord

### Community Channels

- **GitHub**: https://github.com/ripplebiz/MeshCore
- **Discord**: https://discord.gg/BMwCtwHj5V (ask for invite)
- **Website**: https://buymeacoffee.com/ripplebiz
- **Network**: apraide.lv (community testing network)

---

## 13. Quick Reference for AI Assistants

### When Asked to...

**Add a New Feature**
1. Check roadmap and existing issues (don't duplicate)
2. Discuss scope first (open an issue)
3. Create feature branch from `dev`
4. No dynamic allocation in runtime paths
5. Test on 2+ boards if possible
6. Update docs and submit PR to `dev`

**Fix a Bug**
1. Reproduce on hardware or identify minimal test case
2. Trace through Dispatcher → Mesh → handlers
3. Check memory/stack usage (embedded systems!)
4. Write test if possible
5. Verify fix doesn't break other boards

**Optimize Performance**
1. Measure first: memory usage, latency, CPU
2. Identify bottleneck with data, not guesses
3. Stack allocation > heap allocation > dynamic
4. Profile after change; verify improvement
5. Document tradeoffs (speed vs memory vs complexity)

**Support a New Board**
1. Create `/variants/board_name/` directory
2. Implement `target.cpp` with radio/display/storage init
3. Update `platformio.ini` with build flags
4. Test compiles without errors
5. Document known issues (untested, if applicable)

**Write Documentation**
1. Markdown format, clear examples
2. Include code snippets from actual source
3. Explain WHY, not just WHAT
4. Add cross-references to related docs
5. Target both expert and beginner readers

### Critical Checks Before Suggesting Code

- ✅ No `new`/`malloc`/`delete` in loop/handler code
- ✅ Respects existing code style (no retroactive reformatting)
- ✅ Compiles with `-Wall -Wextra`
- ✅ Header includes are actually used
- ✅ Comments explain reasoning (not obvious code)
- ✅ Tested on relevant hardware (or clearly marked untested)
- ✅ Updated docs if adding public APIs

---

## Document Metadata

- **Last Updated**: December 27, 2025
- **MeshCore Version**: 1.10.0
- **Primary Focus**: SenseCAP Indicator D1L support (Nightly branch)
- **Maintainer**: Scott Powell / rippleradios.com
- **License**: MIT (see license.txt)
- **For Questions**: Contact via GitHub Issues or Discord

---

**Remember**: MeshCore is designed for constrained embedded systems. When in doubt, choose simplicity, correctness, and efficiency over cleverness. Think like an embedded engineer, not a high-level programmer.
