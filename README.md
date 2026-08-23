# MeshCore — SenseCAP Indicator D1L fork

> **Specialised fork of [MeshCore](https://github.com/meshcore-dev/MeshCore) focused on SenseCAP Indicator D1L support.**
> - **Radio (LoRa RX/TX):** fully functional — SX1262 driver stable, EU NARROW (869.618 MHz), repeater mode validated on hardware.
> - **Display (ILI9341 / touch):** work in progress — not yet operational.
> - **Primary branch:** `nightly` (all D1L-specific code and fixes live here).

MeshCore is a lightweight, portable C++ library for embedded systems that enables **multi-hop packet routing** over LoRa and other packet radios. It targets resilient, decentralised networks that work without internet infrastructure — off-grid, emergency, tactical, and remote-sensor scenarios. Compared to Meshtastic it gives lower-level control; compared to Reticulum it stays simpler and lighter, aimed at constrained embedded targets (ESP32, nRF52, RP2040).

This fork adds and maintains board support beyond upstream — most notably the **SenseCAP Indicator D1L** (with a custom IO-expander HAL) plus additional ThinkNode and RAK variants.

---

## Software

The "software" side is the platform-agnostic mesh library plus the host-side tooling used to build, flash, and observe devices.

### Core library (`src/`)

Platform-independent mesh logic, portable across all supported boards:

| File | Responsibility |
|------|----------------|
| `MeshCore.h` | Main public API + buffer-size macros (`MAX_PACKET_PAYLOAD`, `MAX_PATH_SIZE`) |
| `Mesh.h/.cpp` | Multi-hop routing logic |
| `Dispatcher.h/.cpp` | Packet reception + dispatch |
| `Packet.h/.cpp` | Wire-format packet structure + (de)serialisation |
| `Identity.h/.cpp` | Node identity + Curve25519 keys |
| `helpers/` | Platform helpers — `ui/` display drivers, `bridges/`, `radiolib/`, per-arch board classes |

Selected `helpers/` building blocks worth knowing about: `CommonCLI.h/.cpp` (the serial/BLE command parser shared by every firmware role — admin password, radio params, node config, `erase`/`reboot`), `ClientACL.h/.cpp` (per-contact access control for repeaters/room servers), `IdentityStore.h/.cpp` (private-key persistence), `BaseChatMesh.h/.cpp` (shared companion/room-server messaging logic), and the per-architecture board classes (`ESP32Board`, `NRF52Board`) that each variant's HAL builds on.

**Testing:** a `native_test` PlatformIO environment (`platform = native`, Unity framework, host-compiled — no hardware needed) is scaffolded for host-side coverage of BLE command-validation logic (`test/ble_handler/test_ble_commands.cpp`, see its own `README.md` for the 12 covered cases). **Currently not runnable as committed** — `pio test -e native_test` errors with `Nothing to build` (live-verified 2026-08-23); the test's own README documents a `test_build_src = yes` line that isn't present in the root `platformio.ini`'s `[env:native_test]` block, and that alone may not be sufficient to fix PlatformIO's test discovery. Not yet wired into CI.

**Protocol at a glance** (full spec in [`docs/packet_structure.md`](./docs/packet_structure.md) / [`docs/payloads.md`](./docs/payloads.md)):

- **Max packet:** 255 bytes total (≤184 bytes payload). Layout: `[Header(1)][TransportCodes(4,opt)][PathLen(1)][Path(0–64)][Payload]`.
- **Route types:** flood, flood+transport-codes, direct, direct+transport-codes. Companion nodes never re-transmit (loop prevention). Configurable hop limit (~15 default).
- **Security:** AES-256-CTR payload encryption, ECDH (Curve25519) key agreement, HMAC authentication; node IDs are hashed (no plaintext identifiers). Distributed trust, no central PKI.

### Host tooling

| Tool | Purpose |
|------|---------|
| `tools/d1l_logger.py` | Serial logger with heartbeat + event filtering and VID-based auto-detection of the D1L serial port. More reliable than the raw monitor for long captures. |
| PlatformIO | Build system for every firmware target (`pio run -e <env>`). `build.sh` wraps common multi-target builds. |
| `pio device monitor` | Standard serial console (115200 baud). |

```bash
# Recommended D1L monitor (auto-detect port, macOS/Linux):
python3 tools/d1l_logger.py
# or an explicit port + custom log dir:
python3 tools/d1l_logger.py --port /dev/cu.usbserial-XXXX --log-dir ./logs
```

### Dependencies

| Library | Version | Purpose |
|---------|---------|---------|
| RadioLib | ^7.3.0 | LoRa / SX1262 control (critical) |
| Crypto (rweather) | ^0.4.0 | AES-256, ECDH |
| RTClib (Adafruit) | ^2.1.3 | RTC / timestamps |
| Melopero RV3028 | ^1.1.0 | RTC device driver |
| CayenneLPP | 1.6.1 | Sensor-data encoding |
| LovyanGFX | — | Display driver (D1L variant) |

Framework: Arduino (PlatformIO) + FreeRTOS (ESP32) + SPIFFS config storage.

---

## Firmware

The "firmware" side is what actually runs on a device: a role-specific application built for a specific board variant.

### Firmware roles

Each role is a separate buildable application in `examples/`, sharing the same `src/` mesh library:

| Role | Source | Description |
|------|--------|--------------|
| **Companion radio** | (per-board environments) | Pairs with a phone/desktop client over BLE, USB, or WiFi — messaging, contacts, channels. |
| **Repeater** | `examples/simple_repeater/` | Standalone packet forwarding to extend coverage. The validated role on D1L. |
| **Room server** | `examples/simple_room_server/` | Simple message/BBS server with `ClientACL`-based access control. |
| **Secure chat** | `examples/simple_secure_chat/` | Reference end-to-end encrypted chat implementation. |
| **Sensor node** | `examples/simple_sensor/` | Reads and advertises sensor data (CayenneLPP-encoded) over the mesh. |

### Supported boards

- **`sensecap_indicator_d1l` — primary, this fork's focus.** ESP32-S3, 8 MB flash + 8 MB OPI PSRAM, Semtech SX1262, TCA9535 IO expander for radio-control pins, 480×480 touch TFT, optional RP2040 sensor co-processor.
- **`thinknode_m3`, `thinknode_m6`** (nRF52), **`rak11310`** (RP2040), **`nibble_screen_connect`** — additional variants added by this fork.
- **63 board variants total** live under `variants/` (this fork's additions plus everything inherited from upstream) — ESP32 (Heltec v2/v3/v4, LilyGo T-Beam/T-Deck/T-Echo/T-Lora, XIAO, WioTracker, Ebyte EoRa S3, generic E22, and more), nRF52 (RAK4631, XIAO nRF52, ikoka handheld/nano/stick), and RP2040 (Waveshare, RAK11310, Raspberry Pi Pico W) targets. Each variant carries its own `platformio.ini` defining its buildable role×radio environment combinations (`pio run -e <variant>_<role>` — see that variant's directory for the exact environment names, e.g. `SenseCapIndicator-D1L_repeater`).

### SenseCAP Indicator D1L — status

**Working:**
- Custom HAL: `hal/TCA9535_GPIO` (IO-expander GPIO), `hal/CustomRadioLibHal` (RadioLib GODMODE HAL), virtual-pin routing (100–199 → TCA9535, 0–99 → ESP32 GPIO), FreeRTOS interrupt-servicing task.
- EU NARROW radio profile: 869.618 MHz, BW 62.5 kHz, SF 8, CR 8/5.
- Runtime configuration via serial CLI with SPIFFS persistence (no hardcoded credentials).
- Hardware-stability fixes, including a **recursive** `d1l_i2c_mutex` — required because `CustomRadioLibHal::attachInterrupt()` nests into `TCA9535_GPIO::digitalRead()`; a non-recursive mutex deadlocks and kills RX. **Do not change this mutex to non-recursive.**

**In progress / not yet done:** ILI9341 display driver, RP2040 sensor readout, TCA9535 interrupt-driven (vs polled) INT handling, on-hardware validation of secondary variants, OTA updates.

### Build & flash

```bash
# D1L repeater (validated target):
pio run -e SenseCapIndicator-D1L_repeater
pio run -e SenseCapIndicator-D1L_repeater -t upload --upload-port /dev/cu.usbserial-XXXX
# List target port: ls /dev/cu.usbserial-*
```

Prebuilt upstream firmware for many other boards is available via the [MeshCore Flasher](https://flasher.meshcore.co.uk) and clients at [app.meshcore.nz](https://app.meshcore.nz) (config tool: [config.meshcore.dev](https://config.meshcore.dev)).

**Full D1L walkthrough — flashing, upload troubleshooting (including a transient port-reenumeration race), Serial CLI reference, and admin-password setup/recovery — see [`docs/hardware/D1L_GUIDE.md`](./docs/hardware/D1L_GUIDE.md).**

---

## Contributing & coding standards

This fork follows upstream's embedded discipline — read before submitting a PR:

- **No dynamic memory allocation** (`new`/`malloc`/`delete`/`free`) outside `setup()`/`begin()`. Use fixed/stack buffers; heap fragmentation crashes long-running nodes.
- **Do not retroactively reformat existing code.** `.clang-format` is for *new* code only; reformatting existing files creates noise diffs that hide real changes. Match the surrounding brace/indent style (2-space, K&R, 110-col, `#pragma once`).
- Compile clean under `-Wall -Wextra`; every included header must be used.
- Keep it embedded-simple: no unnecessary layers, comment *why* not *what*, measure before optimising.
- PRs on this fork base off `nightly`.

---

## License & credits

MIT License (see [`license.txt`](./license.txt)). MeshCore is created and maintained upstream at [meshcore-dev/MeshCore](https://github.com/meshcore-dev/MeshCore); this is a downstream fork adding SenseCAP Indicator D1L and related board support. Community: [MeshCore Discord](https://discord.gg/BMwCtwHj5V).
