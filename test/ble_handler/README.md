# BLE Command Handler Tests

Automated tests for BLE protocol command handler validation logic.

## Purpose

These tests validate the correctness of:
- Buffer size validation (preventing overflow attacks)
- Coordinate validation (GPS lat/lon bounds checking)
- Protocol error code responses

## Test Coverage

### CMD_APP_START
- Valid frame length (within `MAX_FRAME_SIZE`)
- Oversized frame detection (exactly at limit)
- Way oversized frame detection (far beyond limit)
- Maximum safe frame size (MAX_FRAME_SIZE - 1)

### CMD_SET_ADVERT_LATLON
- Valid coordinates (Riga, Latvia example)
- Valid negative coordinates (Southern/Western hemisphere)
- Invalid latitude (too high: > 90°)
- Invalid latitude (too low: < -90°)
- Invalid longitude (too high: > 180°)
- Invalid longitude (too low: < -180°)
- Boundary conditions (exactly at min/max valid values)

## Running Tests

### Method 1: PlatformIO (Native Platform)

Add this environment to your `platformio.ini` at the root of the project:

```ini
[env:native_test]
platform = native
test_framework = unity
build_flags =
  -D UNIT_TEST
  -std=c++11
test_build_src = yes
```

Then run:
```bash
# Run all tests
pio test -e native_test

# Run specific test
pio test -e native_test -f test_ble_commands
```

### Method 2: Direct Compilation

```bash
# Compile tests directly with g++
cd tests/ble_handler
g++ -o test_runner test_ble_commands.cpp -I/path/to/unity -DUNITY_INCLUDE_CONFIG_H -std=c++11
./test_runner
```

## Test Architecture

The tests use **extracted validation functions** rather than testing the full `MyMesh::handleCmdFrame()` method. This approach:

1. **Isolates validation logic** - Tests focus on the core validation rules without Arduino/embedded dependencies
2. **Runs on host PC** - No need for embedded hardware or simulator
3. **Fast execution** - Hundreds of tests can run in milliseconds
4. **Easy to extend** - Add new test cases by calling validation functions

### Limitations

This scaffolding tests **validation logic only**, not:
- Actual BLE frame parsing
- Serial interface integration
- Response frame generation
- Full `MyMesh` object lifecycle

Future work could:
- Mock `MyMesh` to test full command flow
- Test response frame correctness
- Add integration tests with simulated BLE interface

## Constants

All constants are defined inline in the test file to avoid Arduino dependencies:

- `MAX_FRAME_SIZE = 172` (from `BaseSerialInterface.h`)
- `MAX_LAT = 90`, `MIN_LAT = -90` (degrees)
- `MAX_LON = 180`, `MIN_LON = -180` (degrees)
- `COORDS_MULTIPLIER = 1000000` (converts float to int32)

## Expected Output

```
test_ble_commands.cpp:64:test_cmd_app_start_valid_frame_length:PASS
test_ble_commands.cpp:73:test_cmd_app_start_oversized_frame:PASS
test_ble_commands.cpp:82:test_cmd_app_start_way_oversized_frame:PASS
test_ble_commands.cpp:91:test_cmd_app_start_max_safe_frame:PASS
test_ble_commands.cpp:106:test_cmd_set_advert_latlon_valid_coordinates:PASS
test_ble_commands.cpp:118:test_cmd_set_advert_latlon_valid_negative_coordinates:PASS
test_ble_commands.cpp:130:test_cmd_set_advert_latlon_invalid_latitude_too_high:PASS
test_ble_commands.cpp:142:test_cmd_set_advert_latlon_invalid_latitude_too_low:PASS
test_ble_commands.cpp:154:test_cmd_set_advert_latlon_invalid_longitude_too_high:PASS
test_ble_commands.cpp:166:test_cmd_set_advert_latlon_invalid_longitude_too_low:PASS
test_ble_commands.cpp:178:test_cmd_set_advert_latlon_boundary_max_valid:PASS
test_ble_commands.cpp:189:test_cmd_set_advert_latlon_boundary_min_valid:PASS

-----------------------
12 Tests 0 Failures 0 Ignored
OK
```

## Next Steps

1. Add test environment to `platformio.ini`
2. Run `pio test -e native_test` to verify all tests pass
3. Integrate into CI/CD pipeline (GitHub Actions, etc.)
4. Extend coverage to other BLE commands (CMD_SET_RADIO_PARAMS, etc.)
5. Consider mocking `MyMesh` for full integration tests
