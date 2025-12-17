#include <Arduino.h>
#include "target.h"

// SenseCAP Indicator D1L Implementation
// Author: MeshCore Community / Latvian community at apraide.lv

ESP32Board board;

// SPI instance for LoRa radio
static SPIClass spi;

// SX1262 Module configuration
// NOTE: The pins CS, DIO1, RST, and BUSY are on the IO Expander
// This requires HAL support for IO expander GPIO operations
// Pin format: (expander_pin | IO_EXPANDER) where IO_EXPANDER is the I2C address
SX1262 radio = new Module(
  LORA_CS,      // CS   - IO Expander pin 0
  LORA_DIO1,    // DIO1 - IO Expander pin 3 (IRQ)
  LORA_RST,     // RST  - IO Expander pin 1
  LORA_BUSY,    // BUSY - IO Expander pin 2
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

  Serial.println("Initializing SX1262 on SenseCAP Indicator D1L...");
  Serial.printf("  SPI: SCK=%d, MISO=%d, MOSI=%d\n", LORA_SCK, LORA_MISO, LORA_MOSI);
  Serial.printf("  CS=%d, DIO1=%d, RST=%d, BUSY=%d\n", LORA_CS, LORA_DIO1, LORA_RST, LORA_BUSY);
  Serial.printf("  IO_EXPANDER=0x%02X, IRQ=%d\n", IO_EXPANDER, IO_EXPANDER_IRQ);

  // Initialize radio using RadioLibRadio wrapper
  // This will call radio.begin() internally
  int state = radio.begin(
    LORA_FREQ,  // frequency in MHz
    LORA_BW,    // bandwidth in kHz
    LORA_SF,    // spreading factor
    LORA_CR,    // coding rate
    0x12,       // sync word
    22          // output power in dBm
  );

  if (state != RADIOLIB_ERR_NONE) {
    Serial.printf("SX1262 init failed: %d\n", state);
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
