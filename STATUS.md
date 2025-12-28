# MeshCore D1L Development Status

## Project Overview

This is a specialized fork of MeshCore dedicated to supporting the **SenseCAP Indicator D1L** device, an ESP32-S3 based LoRa radio with integrated display and I/O expansion.

## Hardware Specifications

- **Device**: SenseCAP Indicator D1L
- **Microcontroller**: ESP32-S3
- **LoRa Radio**: SX1262
- **I/O Expansion**: TCA9535 I2C expander
- **Display**: ILI9341 (currently non-functional)
- **Primary Interface**: Serial CLI / USB

## Feature Status

### ✅ Fully Functional

- **LoRa Radio (RX/TX)**: Fully operational
- **Mesh Networking**: All core routing and packet handling
- **I2C Bus**: Communication with TCA9535 I/O expander
- **Repeater Mode**: Network extension and relay capabilities
- **Serial CLI**: Command-line interface for device management
- **Recursive Mutex**: Fixed deadlock issues in threading
- **Logger**: Portable logging system (recently refactored)

### 🔄 In Progress / Not Currently Operational

- **Display/Screen (ILI9341)**: Requires further debugging and implementation
  - Currently not rendering correctly
  - Needs driver optimization for D1L variant
  - Expected completion: Q2 2026

### ❌ Not Applicable for D1L

- **BLE Companion Mode**: This D1L fork focuses on repeater/router use cases; BLE companion functionality is not a priority

## Development Timeline

### Current Status: Maintenance Mode (Q4 2025)

- Radio functionality is stable and production-ready
- Bug fixes and optimizations ongoing
- Display support is on hold

### Q2 2026: Full Feature Push

- Full display support implementation and testing
- Performance optimizations
- Enhanced documentation
- Community contributions welcome

## Known Limitations

1. **Display Output**: The ILI9341 display driver requires significant work. This is not blocking radio/mesh functionality.
2. **Timeline**: Active feature development resumes Q2 2026. Until then, the focus is on maintaining radio stability.
3. **Testing**: Device is currently on shelf (not actively deployed). Community testing feedback is valuable.

## How to Contribute

Contributions are welcome! Please see [CONTRIBUTING.md](CONTRIBUTING.md) for guidelines.

### Areas Where Help is Needed

1. **Display Driver Development**: If you have expertise with ILI9341 or ESP32-S3 display drivers, help is needed.
2. **Testing**: Test scenarios for repeater mode, mesh routing, and stability.
3. **Documentation**: Improve setup guides and API documentation.
4. **Performance**: Profile and optimize radio/mesh performance.

## Repository Structure

- **`nightly`** branch: Main development branch containing all D1L-specific code and fixes
- `examples/`: Example implementations (repeater, room server, companion radio)
- `src/`: Core MeshCore library code
- `boards/`: Device-specific board definitions and configurations
- `docs/`: Additional documentation

## Support & Communication

- Report issues via GitHub Issues
- Discuss via GitHub Discussions
- Join [MeshCore Discord](https://discord.gg/BMwCtwHj5V) for community support

## License

This project is licensed under the MIT License. See [LICENSE.txt](LICENSE.txt) for details.

---

**Last Updated**: December 2025
**Focus**: SenseCAP Indicator D1L Support
**Branch**: nightly
