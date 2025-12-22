# Changelog

All notable changes to the MeshCore D1L support will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added - SenseCAP Indicator D1L Support
- Support for SenseCAP Indicator D1L hardware (ESP32-S3 + SX1262 + ILI9341)
- Custom Hardware Abstraction Layer (HAL) for TCA9535 I2C GPIO expander
- Unified pin interface supporting both native ESP32 GPIO (0-99) and TCA9535 pins (100-199)
- EU Narrow mode LoRa configuration (869.618 MHz, BW=62.5 kHz, SF=8, CR=8)
- D1L companion and repeater firmware variants
- Thread-safe I2C access with FreeRTOS mutex protection (RAII pattern)
- Comprehensive HAL documentation with thread safety and known limitations
- GitHub Actions workflows for automated D1L build validation
- Security scanning CI workflow to prevent credential leaks

### Fixed
- Removed hard-coded credentials from build configuration
- Removed location-specific device naming from firmware defaults
- Verified RadioLib version pinning to prevent breaking changes from upstream updates
- Documented RTC dependency limitation (D1L has no RTC chip, but RTClib inclusion is harmless)

### Security
- Changed default admin password to documented standard value (123456)
- Added security scanning CI workflow to prevent credential leaks
- Removed exposed admin password from git history
- Added .gitignore patterns for local configs and secrets

### Changed
- Migrated from EU WIDE mode (869.525 MHz, BW=250, SF=11) to EU NARROW mode
- Default repeater password is now runtime-configurable, not compile-time

### Infrastructure
- Added GitHub Actions workflow for automated D1L build validation
- Added .gitignore patterns for local configuration files
- Enhanced HAL documentation with thread safety and architecture details

## [1.0.0] - Initial D1L Support

### Notes
This is the initial release of SenseCAP Indicator D1L support for MeshCore.

**Breaking Changes:**
- Existing D1L devices on WIDE mode must be reflashed to NARROW mode
- Admin password must be reconfigured after flashing (default: 123456)
- Device names must be set via BLE app or CLI (not in firmware)

**Migration Guide:**
1. Flash D1L with new repeater firmware
2. Connect via BLE using MeshCore app
3. Set admin password (default is 123456)
4. Configure device name
5. Verify radio settings: 869.618 MHz, BW=62.5, SF=8

**Known Limitations:**
- RTClib dependency remains (examples/simple_repeater requirement)
- D1L has no RTC chip but library is harmless if present
- Can be addressed in future upstream simple_repeater refactor

**Testing Checklist:**
- [x] Builds successfully on macOS
- [x] Builds successfully on Linux (verified via CI)
- [ ] Flashed to physical D1L hardware
- [ ] Radio initialization completes
- [ ] BLE advertising visible from phone
- [ ] Can connect via MeshCore mobile app
- [ ] Radio transmits on correct frequency (869.618 MHz)
- [ ] Can communicate with other NARROW mode MeshCore nodes
- [ ] Display shows status correctly
- [ ] Touch interface responds
- [ ] Admin password can be changed via CLI
- [ ] Device name can be set via BLE app
- [ ] Power consumption is acceptable
- [ ] Standby mode works correctly

**BLOCKING ISSUE:** D1L variant is non-functional due to IO Expander HAL requirement. See docs/hardware/d1l_pin_research.md for details.
