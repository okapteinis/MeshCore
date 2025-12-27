/**
 * BLE Command Handler Tests
 *
 * Tests for buffer validation and protocol correctness in BLE command handlers.
 * These tests validate the validation logic without requiring full embedded environment.
 */

#include <unity.h>
#include <stdint.h>
#include <string.h>

// Protocol constants (from examples/simple_repeater/ProtocolCodes.h)
#define CMD_APP_START              1
#define CMD_SET_ADVERT_LATLON     13

#define RESP_CODE_OK                    0
#define ERR_CODE_ILLEGAL_ARG            2

#define MAX_FRAME_SIZE  172
#define MAX_LAT              90
#define MIN_LAT             -90
#define MAX_LON             180
#define MIN_LON            -180
#define COORDS_MULTIPLIER  1000000

// Test helper: Validate CMD_APP_START buffer size
// Returns: 0 on success, ERR_CODE_ILLEGAL_ARG if buffer too large
uint8_t validate_cmd_app_start_buffer(size_t len) {
  if (len >= MAX_FRAME_SIZE) {
    return ERR_CODE_ILLEGAL_ARG;
  }
  return RESP_CODE_OK;
}

// Test helper: Validate CMD_SET_ADVERT_LATLON coordinates
// Returns: 0 on success, ERR_CODE_ILLEGAL_ARG if coordinates invalid
uint8_t validate_coordinates(int32_t lat, int32_t lon) {
  // Check buffer bounds first (simulated - in real code this is checked before memcpy)
  // In the actual code: if (1 + 4 > len || 5 + 4 > len) return error

  // Validate coordinates are within reasonable bounds
  if (lat <= MAX_LAT * COORDS_MULTIPLIER && lat >= MIN_LAT * COORDS_MULTIPLIER &&
      lon <= MAX_LON * COORDS_MULTIPLIER && lon >= MIN_LON * COORDS_MULTIPLIER) {
    return RESP_CODE_OK;
  }

  return ERR_CODE_ILLEGAL_ARG;
}

// ============================================================================
// Test Cases for CMD_APP_START
// ============================================================================

void test_cmd_app_start_valid_frame_length(void) {
  // Test 1: Valid frame length within MAX_FRAME_SIZE
  // Expected: RESP_CODE_OK (validation passes)

  size_t valid_len = 8;  // Minimum valid length for CMD_APP_START
  uint8_t result = validate_cmd_app_start_buffer(valid_len);

  TEST_ASSERT_EQUAL_UINT8(RESP_CODE_OK, result);
}

void test_cmd_app_start_oversized_frame(void) {
  // Test 2: Oversized frame length >= MAX_FRAME_SIZE
  // Expected: ERR_CODE_ILLEGAL_ARG error

  size_t oversized_len = MAX_FRAME_SIZE;  // Exactly at limit (should fail)
  uint8_t result = validate_cmd_app_start_buffer(oversized_len);

  TEST_ASSERT_EQUAL_UINT8(ERR_CODE_ILLEGAL_ARG, result);
}

void test_cmd_app_start_way_oversized_frame(void) {
  // Test 3: Way over MAX_FRAME_SIZE
  // Expected: ERR_CODE_ILLEGAL_ARG error

  size_t way_oversized_len = MAX_FRAME_SIZE + 100;
  uint8_t result = validate_cmd_app_start_buffer(way_oversized_len);

  TEST_ASSERT_EQUAL_UINT8(ERR_CODE_ILLEGAL_ARG, result);
}

void test_cmd_app_start_max_safe_frame(void) {
  // Test 4: Largest safe frame (MAX_FRAME_SIZE - 1)
  // Expected: RESP_CODE_OK

  size_t max_safe_len = MAX_FRAME_SIZE - 1;
  uint8_t result = validate_cmd_app_start_buffer(max_safe_len);

  TEST_ASSERT_EQUAL_UINT8(RESP_CODE_OK, result);
}

// ============================================================================
// Test Cases for CMD_SET_ADVERT_LATLON
// ============================================================================

void test_cmd_set_advert_latlon_valid_coordinates(void) {
  // Test 1: Valid coordinates inside allowed range
  // Riga, Latvia: lat = 56.950266, lon = 24.132886
  // Expected: RESP_CODE_OK

  int32_t lat = (int32_t)(56.950266 * COORDS_MULTIPLIER);
  int32_t lon = (int32_t)(24.132886 * COORDS_MULTIPLIER);

  uint8_t result = validate_coordinates(lat, lon);

  TEST_ASSERT_EQUAL_UINT8(RESP_CODE_OK, result);
}

void test_cmd_set_advert_latlon_valid_negative_coordinates(void) {
  // Test 2: Valid negative coordinates (Southern/Western hemisphere)
  // Expected: RESP_CODE_OK

  int32_t lat = (int32_t)(-33.8688 * COORDS_MULTIPLIER);  // Sydney
  int32_t lon = (int32_t)(151.2093 * COORDS_MULTIPLIER);

  uint8_t result = validate_coordinates(lat, lon);

  TEST_ASSERT_EQUAL_UINT8(RESP_CODE_OK, result);
}

void test_cmd_set_advert_latlon_invalid_latitude_too_high(void) {
  // Test 3: Invalid latitude (lat > MAX_LAT * COORDS_MULTIPLIER)
  // Expected: ERR_CODE_ILLEGAL_ARG

  int32_t lat = (MAX_LAT + 1) * COORDS_MULTIPLIER;  // 91 degrees (invalid)
  int32_t lon = 0;

  uint8_t result = validate_coordinates(lat, lon);

  TEST_ASSERT_EQUAL_UINT8(ERR_CODE_ILLEGAL_ARG, result);
}

void test_cmd_set_advert_latlon_invalid_latitude_too_low(void) {
  // Test 4: Invalid latitude (lat < MIN_LAT * COORDS_MULTIPLIER)
  // Expected: ERR_CODE_ILLEGAL_ARG

  int32_t lat = (MIN_LAT - 1) * COORDS_MULTIPLIER;  // -91 degrees (invalid)
  int32_t lon = 0;

  uint8_t result = validate_coordinates(lat, lon);

  TEST_ASSERT_EQUAL_UINT8(ERR_CODE_ILLEGAL_ARG, result);
}

void test_cmd_set_advert_latlon_invalid_longitude_too_high(void) {
  // Test 5: Invalid longitude (lon > MAX_LON * COORDS_MULTIPLIER)
  // Expected: ERR_CODE_ILLEGAL_ARG

  int32_t lat = 0;
  int32_t lon = (MAX_LON + 1) * COORDS_MULTIPLIER;  // 181 degrees (invalid)

  uint8_t result = validate_coordinates(lat, lon);

  TEST_ASSERT_EQUAL_UINT8(ERR_CODE_ILLEGAL_ARG, result);
}

void test_cmd_set_advert_latlon_invalid_longitude_too_low(void) {
  // Test 6: Invalid longitude (lon < MIN_LON * COORDS_MULTIPLIER)
  // Expected: ERR_CODE_ILLEGAL_ARG

  int32_t lat = 0;
  int32_t lon = (MIN_LON - 1) * COORDS_MULTIPLIER;  // -181 degrees (invalid)

  uint8_t result = validate_coordinates(lat, lon);

  TEST_ASSERT_EQUAL_UINT8(ERR_CODE_ILLEGAL_ARG, result);
}

void test_cmd_set_advert_latlon_boundary_max_valid(void) {
  // Test 7: Boundary test - exactly at maximum valid values
  // Expected: RESP_CODE_OK

  int32_t lat = MAX_LAT * COORDS_MULTIPLIER;
  int32_t lon = MAX_LON * COORDS_MULTIPLIER;

  uint8_t result = validate_coordinates(lat, lon);

  TEST_ASSERT_EQUAL_UINT8(RESP_CODE_OK, result);
}

void test_cmd_set_advert_latlon_boundary_min_valid(void) {
  // Test 8: Boundary test - exactly at minimum valid values
  // Expected: RESP_CODE_OK

  int32_t lat = MIN_LAT * COORDS_MULTIPLIER;
  int32_t lon = MIN_LON * COORDS_MULTIPLIER;

  uint8_t result = validate_coordinates(lat, lon);

  TEST_ASSERT_EQUAL_UINT8(RESP_CODE_OK, result);
}

// ============================================================================
// Test Runner
// ============================================================================

void setUp(void) {
  // Setup before each test (if needed)
}

void tearDown(void) {
  // Cleanup after each test (if needed)
}

int main(int argc, char **argv) {
  UNITY_BEGIN();

  // CMD_APP_START tests
  RUN_TEST(test_cmd_app_start_valid_frame_length);
  RUN_TEST(test_cmd_app_start_oversized_frame);
  RUN_TEST(test_cmd_app_start_way_oversized_frame);
  RUN_TEST(test_cmd_app_start_max_safe_frame);

  // CMD_SET_ADVERT_LATLON tests
  RUN_TEST(test_cmd_set_advert_latlon_valid_coordinates);
  RUN_TEST(test_cmd_set_advert_latlon_valid_negative_coordinates);
  RUN_TEST(test_cmd_set_advert_latlon_invalid_latitude_too_high);
  RUN_TEST(test_cmd_set_advert_latlon_invalid_latitude_too_low);
  RUN_TEST(test_cmd_set_advert_latlon_invalid_longitude_too_high);
  RUN_TEST(test_cmd_set_advert_latlon_invalid_longitude_too_low);
  RUN_TEST(test_cmd_set_advert_latlon_boundary_max_valid);
  RUN_TEST(test_cmd_set_advert_latlon_boundary_min_valid);

  return UNITY_END();
}
