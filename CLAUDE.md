# MeshCore Project Context for Claude Code

## Project Identity
MeshCore is a lightweight, portable C++ library for embedded systems that enables multi-hop packet routing using LoRa and other packet radios. It is designed for developers building resilient, decentralized communication networks that work without internet infrastructure.

## Core Purpose
- Enable wireless mesh networks for off-grid communication
- Balance simplicity with scalability for custom embedded solutions
- Provide lower-level control than Meshtastic while being more accessible than Reticulum
- Support battery-powered, solar-powered devices with low power consumption

## Repository Structure

### Core Source Code (src/)
- Mesh.cpp / Mesh.h - Core mesh networking logic, routing algorithms, packet forwarding
- Dispatcher.cpp / Dispatcher.h - Message dispatching and event handling
- Packet.cpp / Packet.h - Packet structure definitions and serialization
- Identity.cpp / Identity.h - Node identity management and addressing
- Utils.cpp / Utils.h - Utility functions for the library
- helpers/ - Additional helper modules

### Hardware Variants (variants/)
Each subdirectory contains device-specific configurations:
- target.h - Pin definitions and hardware configuration
- target.cpp - Hardware initialization code
- platformio.ini - Build configuration for specific device
- Display drivers when applicable

Current variants include various ESP32-based LoRa devices, with active development on SenseCAP Indicator D1L support.

### Example Applications (examples/)
- companion_radio - For use with external chat apps over BLE, USB, or WiFi
- simple_repeater - Extends network coverage by relaying messages
- simple_room_server - Simple BBS server for shared posts
- simple_secure_chat - Secure terminal-based text communication

### Documentation (docs/)
- faq.md - Comprehensive FAQ covering usage, hardware, troubleshooting
- packet_structure.md - Protocol specification for packet format
- payloads.md - Payload type definitions and structures
- stats_binary_frames.md - Statistics frame format documentation
- hardware/ - Hardware-specific documentation

### Build System
- platformio.ini - Main PlatformIO configuration
- build.sh - Build script
- boards/ - Custom board definitions
- arch/ - Architecture-specific code

## Critical Coding Standards

### Memory Management Rules (STRICT)
- NO dynamic memory allocation (new, malloc, calloc) except during setup/begin functions
- All buffers must be statically allocated or stack-allocated
- Think embedded systems - memory is constrained
- Avoid C++ features that use hidden dynamic allocation (std::vector, std::string in loops)

### Code Style Rules (STRICT)
- Follow existing brace and indentation style in core source modules
- A .clang-format file exists - use it for NEW code only
- DO NOT reformat existing code retroactively - this creates unnecessary diffs that make finding bugs harder
- Keep code concise without unnecessary abstraction layers
- Don't think like a high-level language programmer - think embedded

### Code Organization
- Use consistent naming conventions matching existing code
- Keep header files clean with forward declarations where possible
- Minimize dependencies between modules
- Document hardware-specific behavior in comments

## Current Development Focus (Nightly Branch)

### SenseCAP Indicator D1L Support
This is the primary active development area on the nightly branch:

#### What's Been Done
- Pin configuration research completed (documented in docs/hardware/d1l_pin_research.md)
- Variant structure created in variants/sensecap_indicator_d1l/
- CHANGELOG.md tracks all D1L-specific changes
- Security improvements: removed hardcoded credentials, added scanning workflow
- Migration from EU WIDE mode to EU NARROW mode (869.618 MHz, BW=62.5 kHz, SF=8)

#### Critical Outstanding Issue - IO Expander Architecture
The D1L uses a TCA9535 IO Expander for SX1262 radio control pins instead of direct GPIO. This means:
- Control pins (CS, RST, BUSY, DIO1) are accessed via I2C at address 0x40
- Standard RadioLib expects direct GPIO pin numbers
- The variant uses pattern (pin | IO_EXPANDER) but this creates invalid GPIO numbers
- Current implementation is NON-FUNCTIONAL without custom HAL

#### Three Potential Solutions
Option A (Recommended): Use Meshtastic's custom Arduino-ESP32 framework fork that includes TCA9535 HAL support
Option B: Implement custom RadioLib HAL using RADIOLIB_GODMODE to intercept GPIO operations
Option C: Direct implementation with manual TCA9535 initialization and wrapper functions

#### D1L Pin Configuration (for reference)
SPI Bus (direct GPIO):
- LORA_MOSI: 48
- LORA_MISO: 47
- LORA_SCK: 41

Control pins (via IO Expander at 0x40):
- LORA_CS: 0 | IO_EXPANDER
- LORA_RST: 1 | IO_EXPANDER
- LORA_BUSY: 2 | IO_EXPANDER
- LORA_DIO1: 3 | IO_EXPANDER

IO Expander:
- I2C Address: 0x40
- IRQ Pin: GPIO 42
- I2C SDA: GPIO 39
- I2C SCL: GPIO 40

Display (ILI9341):
- Managed by LovyanGFX with IO expander support

## Protocol and Network Architecture

### Packet Structure
Packets have a specific binary format documented in docs/packet_structure.md:
- Header with routing information
- Source and destination addresses
- Hop count and TTL
- Payload with type identification
- CRC for integrity

### Routing Behavior
- Multi-hop forwarding with configurable hop limits
- "Companion" nodes do not repeat messages (prevents adverse routing)
- Repeater nodes forward packets to extend range
- Self-healing network topology

### Radio Configuration (Current)
- EU NARROW mode: 869.618 MHz, BW=62.5 kHz, SF=8, CR=8
- Previous EU WIDE mode (869.525 MHz, BW=250, SF=11) deprecated
- TCXO voltage: 2.4V for SX1262
- DIO2 as RF switch enabled

## Dependencies and Libraries

### Key External Libraries
- RadioLib ^7.3.0 - LoRa radio control with RADIOLIB_GODMODE=1 enabled
- LovyanGFX - Display driver with IO expander support
- RTClib - Real-time clock (currently required by simple_repeater but D1L has no RTC)

### Platform
- PlatformIO on Espressif32 platform (version 6.11.0)
- ESP32-S3 for D1L variant
- Arduino framework

## Known Issues and Limitations

### D1L Variant Specific
- Non-functional without HAL implementation for IO expander
- RTClib dependency is harmless but unnecessary (D1L has no RTC chip)
- Testing checklist incomplete in CHANGELOG.md

### General
- No dynamic memory allocation constraint can complicate some algorithms
- LoRa timing sensitive to I2C overhead when using IO expander
- Interrupt handling through IO expander adds complexity

## Testing Requirements

### Before Committing D1L Changes
- Must compile successfully on macOS and Linux
- CI workflow validates build
- Security scanning for credential leaks
- Physical hardware testing required for functionality verification

### Testing Checklist for D1L
- Builds successfully
- Flashes to physical D1L hardware
- BLE advertising visible
- Can connect via MeshCore app
- Radio transmits on 869.618 MHz
- Can communicate with other NARROW mode nodes
- Display shows correct status
- Admin password changeable via CLI

## Contributing Workflow

### Branch Strategy
- Submit ALL pull requests to 'dev' branch (NOT master)
- 'nightly' branch for active D1L development
- Never commit directly to master

### Before Major Changes
- Open an Issue first for discussion
- Reach consensus on approach
- Consider impact on structure and architecture
- Discuss with community on Discord if architectural

### For Minor Changes
- Submit PR directly to dev branch
- Provide clear description of changes
- Ensure code follows existing style
- Test on hardware if possible

### PR Requirements
- Clear commit messages
- No reformatting of existing code
- Follows memory management rules
- Includes documentation updates if needed
- Passes CI workflows

## Road-Map Status

### Completed
- Companion radio UI redesign
- Repeater + Room Server ACL support
- Bridge mode standardization
- Enhanced zero-hop neighbor discovery

### In Progress
- D1L hardware support (blocked on IO expander HAL)

### Planned
- Repeater/Bridge transport codes for zoning/filtering
- Round-trip manual path support
- Multiple sub-meshes support
- LZW message compression
- Dynamic coding rate for weak vs strong hops
- Multiple virtual nodes framework
- V2 protocol specification

## Important Files to Reference

### When Working on Core Networking
- src/Mesh.cpp - Routing logic
- src/Dispatcher.cpp - Message handling
- docs/packet_structure.md - Protocol spec

### When Adding Hardware Support
- variants/*/target.h - Pin configuration patterns
- variants/*/platformio.ini - Build settings patterns
- docs/hardware/d1l_pin_research.md - D1L hardware analysis example

### When Modifying Examples
- examples/companion_radio - Reference for BLE/WiFi/USB implementation
- examples/simple_repeater - Reference for repeater logic

### When Updating Documentation
- CHANGELOG.md - Maintain Keep a Changelog format
- docs/faq.md - User-facing documentation
- README.md - High-level project overview

## Security Considerations

### Recent Security Improvements
- Default admin password now documented standard (123456)
- CI workflow scans for credential leaks
- Removed hardcoded credentials from build files
- Added .gitignore patterns for secrets

### When Adding Features
- Never hardcode passwords or API keys
- Use environment variables or runtime configuration
- Document default values clearly
- Scan code for credential leaks before commit

## Communication Channels

### Getting Help
- GitHub Issues for bugs and feature requests
- Discord server: https://discord.gg/BMwCtwHj5V
- Developer site: https://buymeacoffee.com/ripplebiz

### Reporting Issues
- Use GitHub Issues
- Provide hardware details for device-specific issues
- Include Serial output logs when relevant
- Specify firmware version and variant

## Build and Flash Instructions

### Web Flasher (Easiest)
- Visit https://flasher.meshcore.co.uk
- Select device
- Choose firmware type (Companion, Repeater, Room Server)
- Flash via browser

### Development Build
- Install PlatformIO in Visual Studio Code
- Clone repository
- Open in VS Code
- Select environment in platformio.ini
- Build and upload

### For D1L Development
- Use environment: d1l-companion or d1l-repeater
- Note: Currently non-functional pending HAL implementation
- Requires physical hardware for testing

## File Organization Expectations

### Core Library Files
Place in src/ with corresponding .h in src/
Keep focused - one class/module per file pair
Minimize cross-dependencies

### Hardware Variants
Create subdirectory in variants/
Include: target.h, target.cpp, platformio.ini, README.md
Follow naming convention: devicename_variant

### Documentation
Technical docs in docs/
Hardware-specific in docs/hardware/
Keep markdown files well-structured
Use relative links between docs

### Build Scripts
Root level for top-level scripts
bin/ for helper utilities
Use .sh for shell scripts, .py for Python

## Code Review Expectations

### What Reviewers Look For
- Memory allocation compliance
- Code style consistency
- No unnecessary complexity
- Proper error handling
- Documentation completeness
- Hardware-specific comments
- Breaking change awareness

### Common Rejection Reasons
- Dynamic memory allocation in wrong places
- Reformatting existing code
- Overly complex abstractions
- Missing documentation
- Hardcoded secrets
- Breaking changes without discussion

## Current Repository Status

### Nightly Branch State
- Active D1L development
- Contains docs/hardware/d1l_pin_research.md with comprehensive hardware analysis
- CHANGELOG.md tracks D1L changes
- D1L variant structure created but non-functional
- Awaiting HAL implementation decision

### Recent Changes
- Security hardening completed
- EU NARROW mode migration completed
- Documentation improvements ongoing
- CI workflows added for D1L validation

### Next Steps Needed
- Decide on IO expander HAL approach
- Implement chosen solution
- Complete D1L testing checklist
- Update documentation with results
- Merge to dev when stable
