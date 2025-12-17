#include <Arduino.h>
#include "target.h"

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

// SX1262 Module configuration
// Pin definitions: (expander_pin | IO_EXPANDER) where IO_EXPANDER = 0x40
// These resolve to integers like 0x40, 0x41, 0x42, 0x43 which are NOT valid GPIO pins.
// Without IO expander HAL, RadioLib will attempt to use these as GPIO numbers and fail.
SX1262 radio = new Module(
  LORA_CS,      // (0 | 0x40) = 0x40 - NOT a valid GPIO, needs HAL
  LORA_DIO1,    // (3 | 0x40) = 0x43 - NOT a valid GPIO, needs HAL
  LORA_RST,     // (1 | 0x40) = 0x41 - NOT a valid GPIO, needs HAL
  LORA_BUSY,    // (2 | 0x40) = 0x42 - NOT a valid GPIO, needs HAL
  spi
);

RadioLibRadio<SX1262> radio_driver(radio, board);

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

bool radio_init() {
  // Initialize clocks
  fallback_clock.begin();
  rtc_clock.begin(Wire);

  // Initialize SPI bus with explicit pins
  // MOSI=48, MISO=47, SCK=41 (direct GPIO)
  spi.begin(LORA_SCK, LORA_MISO, LORA_MOSI);

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

  Serial.println("SX1262 initialized successfully");
  return true;
}

uint32_t radio_get_rng_seed() {
  // Try to get random seed from radio
  uint32_t seed = radio.random();
  if (seed == 0) {
    // Fallback to ESP32 hardware RNG
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
  // Generate identity from ESP32 MAC address
  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_WIFI_STA);

  // Combine MAC bytes to create seed
  uint64_t seed = 0;
  for (int i = 0; i < 6; i++) {
    seed = (seed << 8) | mac[i];
  }

  return mesh::LocalIdentity::generate_from_seed(seed);
}
