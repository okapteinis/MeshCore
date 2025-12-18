#include <Arduino.h>
#include "target.h"
#include <SPIFFS.h>
#include <helpers/radiolib/CustomSX1262.h>

// JPEG file format constants
static const uint8_t JPEG_MAGIC_HEADER[] = {0xFF, 0xD8, 0xFF};
static const size_t JPEG_MAGIC_HEADER_SIZE = sizeof(JPEG_MAGIC_HEADER);

// SenseCAP Indicator D1L Implementation
// Author: MeshCore Community / Latvian community at apraide.lv
//
// ⚠️ CRITICAL WARNING ⚠️
// This variant is NON-FUNCTIONAL without custom IO expander HAL implementation.
// The SX1262 control pins (CS, RST, BUSY, DIO1) are connected via TCA9535 IO expander
// at I2C address 0x40, NOT direct GPIO pins. Standard RadioLib will interpret these
// as invalid GPIO numbers and fail initialization.
//
// REQUIRED TO MAKE THIS WORK:
// 1. Implement custom Arduino HAL with TCA9535 support (like Meshtastic does), OR
// 2. Implement RadioLib custom HAL to intercept pinMode/digitalWrite/digitalRead
//    and route IO expander pins through I2C, OR
// 3. Use Meshtastic's custom Arduino framework fork
//
// This code serves as a REFERENCE IMPLEMENTATION documenting the correct pin
// configuration. Actual functionality requires the HAL work described above.
//
// See PIN_RESEARCH.md for details.

ESP32Board board;

// SPI instance for LoRa radio
static SPIClass spi;

// CustomSX1262 Module configuration
// Pin definitions: LORA_CS, LORA_RST, LORA_BUSY, LORA_DIO1
// These are virtual pins (100-103) mapped to TCA9535 I/O expander pins
CustomSX1262 radio = new Module(
  LORA_CS,      // Virtual pin 100 -> TCA9535 pin 0
  LORA_DIO1,    // Virtual pin 103 -> TCA9535 pin 3
  LORA_RST,     // Virtual pin 101 -> TCA9535 pin 1
  LORA_BUSY,    // Virtual pin 102 -> TCA9535 pin 2
  spi
);

CustomSX1262Wrapper radio_driver(radio, board);

ESP32RTCClock fallback_clock;
AutoDiscoverRTCClock rtc_clock(fallback_clock);
EnvironmentSensorManager sensors;

#ifdef DISPLAY_CLASS
DISPLAY_CLASS display;
MomentaryButton user_btn(PIN_USER_BTN, INPUT_PULLUP, true);
#endif

#ifndef LORA_CR
#define LORA_CR 5
#endif

// ============================================================================
// SPIFFS Storage Functions (Phase 0A - Storage Infrastructure)
// ============================================================================

/**
 * Initialize SPIFFS filesystem
 *
 * @return true if SPIFFS mounted successfully, false otherwise
 */
bool initStorage() {
  Serial.println("\n===== SPIFFS Initialization =====");

  if (!SPIFFS.begin(true)) {  // true = format if mount fails
    Serial.println("❌ SPIFFS mount failed!");
    return false;
  }

  Serial.println("✅ SPIFFS mounted successfully");

  // Get filesystem info
  size_t totalBytes = SPIFFS.totalBytes();
  size_t usedBytes = SPIFFS.usedBytes();
  size_t freeBytes = totalBytes - usedBytes;

  Serial.printf("Total space: %zu bytes (%.2f MB)\n", totalBytes, totalBytes / 1024.0 / 1024.0);
  Serial.printf("Used space:  %zu bytes (%.2f MB)\n", usedBytes, usedBytes / 1024.0 / 1024.0);
  Serial.printf("Free space:  %zu bytes (%.2f MB)\n", freeBytes, freeBytes / 1024.0 / 1024.0);
  Serial.println("=================================\n");

  return true;
}

/**
 * Test map file access from SPIFFS
 *
 * Tests if we can open and read the Baltic region map file.
 * Map should be at /maps/baltic_region.jpg (~500KB)
 *
 * @return true if map file exists and can be read, false otherwise
 */
bool testMapAccess() {
  Serial.println("\n===== Map File Access Test =====");

  const char* mapPath = "/maps/baltic_region.jpg";

  if (!SPIFFS.exists(mapPath)) {
    Serial.printf("⚠️  Map file not found: %s\n", mapPath);
    Serial.println("This is expected if you haven't uploaded filesystem yet.");
    Serial.println("Run: pio run -t uploadfs");
    Serial.println("================================\n");
    return false;
  }

  File mapFile = SPIFFS.open(mapPath, "r");
  if (!mapFile) {
    Serial.printf("❌ Failed to open map file: %s\n", mapPath);
    Serial.println("================================\n");
    return false;
  }

  size_t fileSize = mapFile.size();
  Serial.printf("✅ Map file found: %s\n", mapPath);
  Serial.printf("File size: %zu bytes (%.2f KB)\n", fileSize, fileSize / 1024.0);

  // Read first few bytes to verify it's a JPEG
  uint8_t header[JPEG_MAGIC_HEADER_SIZE];
  size_t bytesRead = mapFile.read(header, sizeof(header));
  mapFile.close();

  if (bytesRead == JPEG_MAGIC_HEADER_SIZE && memcmp(header, JPEG_MAGIC_HEADER, JPEG_MAGIC_HEADER_SIZE) == 0) {
    Serial.println("✅ Valid JPEG header detected");
    Serial.println("================================\n");
    return true;
  } else {
    if (bytesRead < JPEG_MAGIC_HEADER_SIZE) {
      Serial.printf("⚠️  File too small: only %zu bytes read (expected %zu)\n", bytesRead, JPEG_MAGIC_HEADER_SIZE);
    } else {
      Serial.printf("⚠️  Invalid JPEG header: %02X %02X %02X\n", header[0], header[1], header[2]);
    }
    Serial.println("Expected: FF D8 FF");
    Serial.println("================================\n");
    return false;
  }
}

// ============================================================================
// Radio Functions
// ============================================================================

bool radio_init() {
  // ===== EARLY BOOT DIAGNOSTICS =====
  Serial.begin(115200);
  delay(100);
  Serial.println("\n\n========================================");
  Serial.println("=== SENSECAP D1L BOOT DIAGNOSTICS ===");
  Serial.println("========================================");
  Serial.printf("Free heap: %u bytes\n", ESP.getFreeHeap());
  Serial.printf("PSRAM size: %u bytes\n", ESP.getPsramSize());
  Serial.printf("PSRAM free: %u bytes\n", ESP.getFreePsram());

  if (ESP.getPsramSize() == 0) {
    Serial.println("❌ CRITICAL ERROR: PSRAM NOT DETECTED!");
    Serial.println("Display initialization will FAIL!");
    Serial.println("Check platformio.ini PSRAM flags!");
  } else {
    Serial.printf("✅ PSRAM detected: %.2f MB\n", ESP.getPsramSize() / 1024.0 / 1024.0);
  }

  Serial.printf("CPU Freq: %u MHz\n", ESP.getCpuFreqMHz());
  Serial.printf("Flash size: %u bytes\n", ESP.getFlashChipSize());
  Serial.println("========================================\n");
  // ===== END EARLY DIAGNOSTICS =====

  // ===== Phase 0A: Storage Infrastructure Test =====
  Serial.println("\n========================================");
  Serial.println("Phase 0A: SPIFFS Storage Test");
  Serial.println("========================================");

  if (!initStorage()) {
    Serial.println("⚠️  SPIFFS initialization failed - continuing anyway");
  } else {
    Serial.println("✅ SPIFFS initialized successfully");
    testMapAccess();  // Test map file (expected to fail until filesystem is uploaded)
  }

  Serial.println("========================================\n");
  // ==================================================

  // Initialize clocks
  fallback_clock.begin();
  rtc_clock.begin(Wire);

  // Initialize SPI bus with explicit pins
  // MOSI=48, MISO=47, SCK=41 (direct GPIO)
  Serial.println("\n=== Initializing SPI Bus ===");
  spi.begin(LORA_SCK, LORA_MISO, LORA_MOSI);
  Serial.printf("✅ SPI initialized: SCK=%d, MISO=%d, MOSI=%d\n", LORA_SCK, LORA_MISO, LORA_MOSI);

  Serial.print(R"(
==================================================
⚠️  SenseCAP Indicator D1L - IO EXPANDER VARIANT
==================================================
WARNING: This variant requires IO expander HAL support!
The SX1262 control pins are NOT direct GPIO pins.
They are TCA9535 IO expander pins at I2C 0x20.

Expected behavior: Radio init will FAIL unless you have
implemented custom HAL for IO expander support.

See PIN_RESEARCH.md and README.md for details.
==================================================

)");

  Serial.println("Initializing SX1262 on SenseCAP Indicator D1L...");
  Serial.printf("  SPI: SCK=%d, MISO=%d, MOSI=%d (direct GPIO)\n", LORA_SCK, LORA_MISO, LORA_MOSI);
  Serial.printf("  CS=0x%02X, DIO1=0x%02X, RST=0x%02X, BUSY=0x%02X (IO expander - NOT GPIO!)\n",
                LORA_CS, LORA_DIO1, LORA_RST, LORA_BUSY);
#ifdef USE_CUSTOM_RADIOLIB_HAL
  Serial.printf("  TCA9535 I2C address=0x%02X\n", TCA9535_I2C_ADDR);
#else
  Serial.printf("  IO_EXPANDER=0x%02X (I2C address), IRQ=%d\n", IO_EXPANDER, IO_EXPANDER_IRQ);
#endif
  Serial.println("");

  // Initialize radio using RadioLibRadio wrapper
  // ⚠️ THIS WILL FAIL without IO expander HAL support
  // RadioLib will try to use CS=0x40, RST=0x41, etc. as GPIO pin numbers,
  // which don't exist on ESP32-S3, causing pinMode() to fail.
  Serial.println("\n=== Initializing SX1262 Radio ===");
  Serial.println("Attempting radio.begin() (will likely fail without HAL)...");
  int state = radio.begin(
    LORA_FREQ,  // frequency in MHz
    LORA_BW,    // bandwidth in kHz
    LORA_SF,    // spreading factor
    LORA_CR,    // coding rate
    0x12,       // sync word
    22          // output power in dBm
  );

  if (state != RADIOLIB_ERR_NONE) {
    Serial.printf("❌ SX1262 init failed: %d (EXPECTED without HAL)\n", state);
    Serial.print(R"(This is expected behavior. To fix:
1. Implement TCA9535 IO expander HAL, OR
2. Use Meshtastic's custom Arduino framework
)");
    Serial.println("=== BOOT FAILED ===\n");
    return false;
  }

  // Configure SX1262-specific settings
  #ifdef SX126X_DIO2_AS_RF_SWITCH
  radio.setDio2AsRfSwitch(true);
  #endif

  #ifdef SX126X_DIO3_TCXO_VOLTAGE
  radio.setTCXO(SX126X_DIO3_TCXO_VOLTAGE);
  #endif

  #ifdef SX126X_CURRENT_LIMIT
  radio.setCurrentLimit(SX126X_CURRENT_LIMIT);
  #endif

  Serial.println("✅ SX1262 initialized successfully");
  Serial.println("=== BOOT COMPLETE ===\n");
  return true;
}

uint32_t radio_get_rng_seed() {
  uint32_t seed = radio.random(0x7FFFFFFF);
  // Fallback to esp_random() if radio returns 0 (problematic for RNG systems)
  if (seed == 0) {
    seed = esp_random();
  }
  return seed;
}

void radio_set_params(float freq, float bw, uint8_t sf, uint8_t cr) {
  radio.setFrequency(freq);
  radio.setBandwidth(bw);
  radio.setSpreadingFactor(sf);
  radio.setCodingRate(cr);
}

void radio_set_tx_power(uint8_t dbm) {
  radio.setOutputPower(dbm);
}

mesh::LocalIdentity radio_new_identity() {
  RadioNoiseListener rng(radio);
  return mesh::LocalIdentity(&rng);  // create new random identity
}
