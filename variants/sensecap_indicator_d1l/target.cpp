#include <Arduino.h>
#include "target.h"
#include <SPIFFS.h>
#include <helpers/radiolib/CustomSX1262.h>

// HAL Implementation Includes
#ifdef USE_CUSTOM_RADIOLIB_HAL
#include "hal/TCA9535_GPIO.h"
#include "hal/CustomRadioLibHal.h"
#include "hal/freertos_util.h"
#endif

// RNG includes
#include <helpers/ArduinoHelpers.h>
#include <helpers/radiolib/RadioLibWrappers.h>

// Configuration Constants (Replace Magic Numbers)
namespace Config {
  // Serial
  constexpr uint32_t SERIAL_BAUD_RATE = 115200;
  constexpr uint32_t SERIAL_INIT_DELAY_MS = 100;
  constexpr uint32_t SERIAL_CONNECT_TIMEOUT_MS = 2000;

  // Radio
  constexpr uint32_t RADIO_INIT_TIMEOUT_MS = 5000;
  constexpr uint32_t RADIO_RETRY_DELAY_MS = 100;
  constexpr uint8_t LORA_SYNC_WORD_PRIVATE = 0x12;
  constexpr int8_t LORA_MAX_TX_POWER_DBM = 22;
  constexpr int8_t LORA_MIN_TX_POWER_DBM = -9;

  // LoRa Limits
  constexpr float LORA_MIN_FREQ_MHZ = 150.0;
  constexpr float LORA_MAX_FREQ_MHZ = 960.0;
  constexpr uint8_t LORA_MIN_SF = 6;
  constexpr uint8_t LORA_MAX_SF = 12;
  constexpr uint8_t LORA_MIN_CR = 5;
  constexpr uint8_t LORA_MAX_CR = 8;

  // I2C
  constexpr uint32_t I2C_MUTEX_TIMEOUT_MS = 1000;

  // SPIFFS
  constexpr size_t JPEG_HEADER_SIZE = 3;
}

// JPEG Constants
static const uint8_t JPEG_MAGIC_HEADER[] = {0xFF, 0xD8, 0xFF};
static const size_t JPEG_MAGIC_HEADER_SIZE = sizeof(JPEG_MAGIC_HEADER);

// Global Objects (converted to pointers for safe init)
ESP32Board board;
static SPIClass spi;

// HAL Objects (initialized in radio_init)
#ifdef USE_CUSTOM_RADIOLIB_HAL
TCA9535_GPIO* gpio_expander = nullptr;  // Now globally accessible (extern in target.h)
static CustomRadioLibHal* custom_hal = nullptr;
#endif

// Radio Objects (initialized in radio_init)
static CustomSX1262* radio = nullptr;
static Module* radio_module = nullptr;

// Create a dummy radio wrapper that will be properly initialized in radio_init
// This is needed because radio_driver is declared as extern in target.h (not a pointer)
static CustomSX1262 dummy_radio(new Module(0, 0, 0, 0, spi));
CustomSX1262Wrapper radio_driver(dummy_radio, board);

// I2C Bus Mutex
static SemaphoreHandle_t i2c_bus_mutex = nullptr;

// I2C Mutex Helper Class (RAII)
class I2C_Lock {
private:
  bool locked;
public:
  I2C_Lock(uint32_t timeout_ms = Config::I2C_MUTEX_TIMEOUT_MS) : locked(false) {
    if (i2c_bus_mutex != nullptr) {
      locked = xSemaphoreTake(i2c_bus_mutex, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
    }
    if (!locked) {
      log_e("Failed to acquire I2C lock!");
    }
  }

  ~I2C_Lock() {
    if (locked && i2c_bus_mutex != nullptr) {
      xSemaphoreGive(i2c_bus_mutex);
    }
  }

  bool isLocked() const { return locked; }
};

// Cleanup Functions
void cleanup_all_resources(const char* reason) {
  log_e("Cleaning up resources: %s", reason);

  // SPIFFS.end() returns void, so just call it
  SPIFFS.end();
  log_i("SPIFFS unmounted");

  spi.end();
  Wire.end();

#ifdef USE_CUSTOM_RADIOLIB_HAL
  if (custom_hal != nullptr) {
    delete custom_hal;
    custom_hal = nullptr;
  }
  if (gpio_expander != nullptr) {
    delete gpio_expander;
    gpio_expander = nullptr;
  }
#endif

  if (radio != nullptr) {
    delete radio;
    radio = nullptr;
  }
  if (radio_module != nullptr) {
    delete radio_module;
    radio_module = nullptr;
  }

  if (i2c_bus_mutex != nullptr) {
    vSemaphoreDelete(i2c_bus_mutex);
    i2c_bus_mutex = nullptr;
  }
}

bool fail_and_cleanup(const char* error_msg) {
  Serial.printf("FATAL: %s\n", error_msg);
  cleanup_all_resources(error_msg);
  Serial.println("BOOT FAILED\n");
  return false;
}

// Other globals (keep as-is)
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

bool initStorage() {
  Serial.println("\nSPIFFS Initialization");

  if (SPIFFS.begin(false)) {
    Serial.println("SPIFFS mounted (existing filesystem)");

    size_t totalBytes = SPIFFS.totalBytes();
    size_t usedBytes = SPIFFS.usedBytes();
    size_t freeBytes = totalBytes - usedBytes;

    Serial.printf("Total: %zu bytes (%.2f MB)\n", totalBytes, totalBytes / 1024.0 / 1024.0);
    Serial.printf("Used:  %zu bytes (%.2f MB)\n", usedBytes, usedBytes / 1024.0 / 1024.0);
    Serial.printf("Free:  %zu bytes (%.2f MB)\n", freeBytes, freeBytes / 1024.0 / 1024.0);
    Serial.println();
    return true;
  }

  Serial.println("SPIFFS mount failed");

#ifdef SPIFFS_AUTO_FORMAT
  Serial.println("Auto-format enabled - formatting...");
  if (!SPIFFS.begin(true)) {
    Serial.println("Format failed!");
    Serial.println();
    return false;
  }
  Serial.println("SPIFFS formatted and mounted");
#else
  Serial.println("Auto-format disabled");
  Serial.println("To enable: add -D SPIFFS_AUTO_FORMAT=1");
  Serial.println();
  return false;
#endif

  Serial.println();
  return true;
}

bool testMapAccess() {
  Serial.println("\nMap File Access Test");

  const char* mapPath = "/maps/baltic_region.jpg";

  if (!SPIFFS.exists(mapPath)) {
    Serial.print("Map file not found: ");
    Serial.println(mapPath);
    Serial.println("This is expected if you haven't uploaded filesystem yet.");
    Serial.println("Run: pio run -t uploadfs");
    Serial.println();
    return false;
  }

  File mapFile = SPIFFS.open(mapPath, "r");
  if (!mapFile) {
    Serial.print("Failed to open map file: ");
    Serial.println(mapPath);
    Serial.println();
    return false;
  }

  size_t fileSize = mapFile.size();
  Serial.print("Map file found: ");
  Serial.println(mapPath);
  Serial.printf("File size: %zu bytes (%.2f KB)\n", fileSize, fileSize / 1024.0);

  // Read first few bytes to verify it's a JPEG
  uint8_t header[JPEG_MAGIC_HEADER_SIZE];
  memset(header, 0, sizeof(header));
  size_t bytesRead = mapFile.readBytes((char*)header, JPEG_MAGIC_HEADER_SIZE);

  if (bytesRead > JPEG_MAGIC_HEADER_SIZE) {
    Serial.printf("Read overflow: %zu bytes (max %zu)\n", bytesRead, JPEG_MAGIC_HEADER_SIZE);
    mapFile.close();
    return false;
  }

  mapFile.close();

  if (bytesRead == JPEG_MAGIC_HEADER_SIZE && memcmp(header, JPEG_MAGIC_HEADER, JPEG_MAGIC_HEADER_SIZE) == 0) {
    Serial.println("Valid JPEG header detected");
    Serial.println();
    return true;
  } else {
    if (bytesRead < JPEG_MAGIC_HEADER_SIZE) {
      Serial.printf("File too small: only %zu bytes read (expected %zu)\n", bytesRead, JPEG_MAGIC_HEADER_SIZE);
    } else {
      Serial.printf("Invalid JPEG header: %02X %02X %02X\n", header[0], header[1], header[2]);
    }
    Serial.println("Expected: FF D8 FF");
    Serial.println();
    return false;
  }
}

// ============================================================================
// Radio Functions
// ============================================================================

bool radio_init() {
  // Serial is already initialized in main.cpp before calling this function
  // No need to reinitialize here - it would break the UART0 pin mapping

  // Serial.println("========================================");  // REMOVED
  // Serial.println("RADIO INIT START");  // REMOVED
  // Serial.println("========================================");  // REMOVED

  /* PSRAM checks disabled for D1L
  Serial.printf("PSRAM size: %u bytes\n", ESP.getPsramSize());
  Serial.printf("PSRAM free: %u bytes\n", ESP.getFreePsram());

  if (ESP.getPsramSize() == 0) {
    Serial.println("CRITICAL: PSRAM NOT DETECTED!");
    Serial.println("Display may fail or use fallback mode!");
  } else {
    Serial.printf("PSRAM: %.2f MB total, %.2f MB free\n",
                  ESP.getPsramSize() / 1024.0 / 1024.0,
                  ESP.getFreePsram() / 1024.0 / 1024.0);
  }
  */

  // Serial.printf("CPU: %u MHz\n", ESP.getCpuFreqMHz());  // REMOVED
  // Serial.printf("Flash: %u bytes\n", ESP.getFlashChipSize());  // REMOVED
  // Serial.println("========================================\n");  // REMOVED

  // Initialize I2C Mutex
  if (i2c_bus_mutex == nullptr) {
    i2c_bus_mutex = xSemaphoreCreateMutex();
    if (i2c_bus_mutex == nullptr) {
      return fail_and_cleanup("Failed to create I2C mutex");
    }
    // Serial.println("I2C bus mutex created");  // REMOVED
  }

  // SPIFFS Storage Test
  // Serial.println("\n========================================");  // REMOVED
  // Serial.println("Phase 0A: SPIFFS Storage Test");  // REMOVED
  // Serial.println("========================================");  // REMOVED
  if (!initStorage()) {
    // Serial.println("SPIFFS init failed - continuing anyway");  // REMOVED
  } else {
    // Serial.println("SPIFFS initialized");  // REMOVED
    testMapAccess();
  }
  // Serial.println("========================================\n");  // REMOVED

  // Initialize Clocks
  fallback_clock.begin();
  rtc_clock.begin(Wire);

#if RADIO_DRIVER_AVAILABLE
  // Initialize HAL
#ifdef USE_CUSTOM_RADIOLIB_HAL
  // Serial.println("\nInitializing TCA9535 HAL");  // REMOVED - causes crash

  {
    I2C_Lock lock;
    if (!lock.isLocked()) {
      return fail_and_cleanup("I2C mutex lock failed");
    }

    if (!Wire.begin(PIN_BOARD_SDA, PIN_BOARD_SCL, TCA9535_I2C_FREQ)) {
      return fail_and_cleanup("I2C initialization failed");
    }
  }
  // Serial.printf("I2C: SDA=%d, SCL=%d, Freq=%u Hz\n",
  //               PIN_BOARD_SDA, PIN_BOARD_SCL, TCA9535_I2C_FREQ);  // REMOVED

  // STEP 1: Create TCA9535_GPIO object (just construct, don't call begin() yet)
  gpio_expander = new TCA9535_GPIO(TCA9535_I2C_ADDR);

  // Initialize SPI (needed before HAL creation)
  // Serial.println("\nInitializing SPI Bus");  // REMOVED
  spi.begin(LORA_SCK, LORA_MISO, LORA_MOSI);
  // Serial.printf("SPI: SCK=%d, MISO=%d, MOSI=%d\n", LORA_SCK, LORA_MISO, LORA_MOSI);  // REMOVED

  // STEP 2: Create CustomRadioLibHal (this creates the global I2C mutex)
  SPISettings spiSettings(2000000, MSBFIRST, SPI_MODE0);
  custom_hal = new CustomRadioLibHal(gpio_expander, spi, spiSettings);
  custom_hal->init();
  // Serial.println("HAL initialized with SPI settings");  // REMOVED

  // STEP 3: NOW initialize TCA9535 (mutex exists now!)
  {
    I2C_Lock lock;
    if (!lock.isLocked() || !gpio_expander->begin(&Wire)) {
      delete gpio_expander;
      gpio_expander = nullptr;
      delete custom_hal;
      custom_hal = nullptr;
      return fail_and_cleanup("TCA9535 init failed");
    }
  }
  // Serial.printf("TCA9535 at I2C 0x%02X\n", TCA9535_I2C_ADDR);  // REMOVED
  // Serial.println();  // REMOVED

#else
  // Serial.println("WARNING: USE_CUSTOM_RADIOLIB_HAL not defined!");  // REMOVED
  // Serial.println("Radio will likely fail without HAL!\n");  // REMOVED
#endif

  // Initialize Radio
  // Serial.println("\nInitializing SX1262 Radio");  // REMOVED
  // Serial.printf("  SPI pins (direct): SCK=%d, MISO=%d, MOSI=%d\n",
  //               LORA_SCK, LORA_MISO, LORA_MOSI);  // REMOVED
  // Serial.printf("  Control pins (virtual): CS=%d, DIO1=%d, RST=%d, BUSY=%d\n",
  //               TCA9535_GPIO::LORA_NSS, TCA9535_GPIO::LORA_DIO1,
  //               TCA9535_GPIO::LORA_RESET, TCA9535_GPIO::LORA_DIO0);  // REMOVED
#ifdef USE_CUSTOM_RADIOLIB_HAL
  // Serial.printf("  TCA9535 I2C: 0x%02X\n", TCA9535_I2C_ADDR);  // REMOVED
#endif

  if (radio == nullptr) {
#ifdef USE_CUSTOM_RADIOLIB_HAL
    // Create module with custom HAL (HAL pointer is first argument!)
    radio_module = new Module(
      custom_hal,
      TCA9535_GPIO::LORA_NSS,
      TCA9535_GPIO::LORA_DIO1,
      TCA9535_GPIO::LORA_RESET,
      TCA9535_GPIO::LORA_DIO0
    );
#else
    // Create module with standard HAL
    radio_module = new Module(
      TCA9535_GPIO::LORA_NSS,
      TCA9535_GPIO::LORA_DIO1,
      TCA9535_GPIO::LORA_RESET,
      TCA9535_GPIO::LORA_DIO0,
      spi
    );
#endif
    radio = new CustomSX1262(radio_module);
    Serial.println("Radio module created");

    // Initialize the radio_driver wrapper with the actual radio
    radio_driver = CustomSX1262Wrapper(*radio, board);
    Serial.println("Radio driver initialized");
  }

  Serial.println("Attempting radio.begin() with timeout...");
  int state = RADIOLIB_ERR_UNKNOWN;
  uint32_t start_time = millis();

  while (true) {
    state = radio->begin(
      LORA_FREQ,
      LORA_BW,
      LORA_SF,
      LORA_CR,
      Config::LORA_SYNC_WORD_PRIVATE,
      Config::LORA_MAX_TX_POWER_DBM
    );

    if (state == RADIOLIB_ERR_NONE) break;

    if (millis() - start_time > Config::RADIO_INIT_TIMEOUT_MS) {
      Serial.printf("Radio init timeout after %u ms\n", Config::RADIO_INIT_TIMEOUT_MS);
      return fail_and_cleanup("Radio initialization timeout");
    }

    delay(Config::RADIO_RETRY_DELAY_MS);
    Serial.print(".");
  }
  Serial.println();

  if (state != RADIOLIB_ERR_NONE) {
    Serial.printf("Radio init failed: %d\n", state);
    return fail_and_cleanup("Radio initialization failed");
  }

#ifdef SX126X_DIO2_AS_RF_SWITCH
  radio->setDio2AsRfSwitch(true);
#endif
#ifdef SX126X_DIO3_TCXO_VOLTAGE
  radio->setTCXO(SX126X_DIO3_TCXO_VOLTAGE);
#endif
#ifdef SX126X_CURRENT_LIMIT
  radio->setCurrentLimit(SX126X_CURRENT_LIMIT);
#endif

  Serial.println("SX1262 initialized successfully");
  Serial.println("BOOT COMPLETE\n");
#endif // RADIO_DRIVER_AVAILABLE
  return true;
}

uint32_t radio_get_rng_seed() {
  if (radio == nullptr) return esp_random();

  uint32_t seed = radio->random(0x7FFFFFFF);
  if (seed == 0) {
    seed = esp_random();
  }
  return seed;
}

void radio_set_params(float freq, float bw, uint8_t sf, uint8_t cr) {
  if (radio == nullptr) {
    log_e("Radio not initialized!");
    return;
  }

  if (freq < Config::LORA_MIN_FREQ_MHZ || freq > Config::LORA_MAX_FREQ_MHZ) {
    log_e("Invalid frequency: %.2f MHz (range: %.0f-%.0f)",
          freq, Config::LORA_MIN_FREQ_MHZ, Config::LORA_MAX_FREQ_MHZ);
    return;
  }
  if (sf < Config::LORA_MIN_SF || sf > Config::LORA_MAX_SF) {
    log_e("Invalid SF: %d (range: %d-%d)", sf, Config::LORA_MIN_SF, Config::LORA_MAX_SF);
    return;
  }
  if (cr < Config::LORA_MIN_CR || cr > Config::LORA_MAX_CR) {
    log_e("Invalid CR: %d (range: %d-%d)", cr, Config::LORA_MIN_CR, Config::LORA_MAX_CR);
    return;
  }

  radio->setFrequency(freq);
  radio->setBandwidth(bw);
  radio->setSpreadingFactor(sf);
  radio->setCodingRate(cr);

  Serial.printf("Radio config: %.3f MHz, BW=%.1f kHz, SF=%d, CR=4/%d\n",
                freq, bw, sf, cr);
}

void radio_set_tx_power(uint8_t dbm) {
  if (radio == nullptr) {
    log_e("Radio not initialized!");
    return;
  }

  if (dbm > Config::LORA_MAX_TX_POWER_DBM) {
    log_w("TX power %d dBm exceeds limit, clamping to %d dBm",
          dbm, Config::LORA_MAX_TX_POWER_DBM);
    dbm = Config::LORA_MAX_TX_POWER_DBM;
  }

  radio->setOutputPower(dbm);
  Serial.printf("TX power: %d dBm\n", dbm);
}

mesh::LocalIdentity radio_new_identity() {
  if (radio == nullptr) {
    log_w("Radio not initialized, using fallback RNG");
    StdRNG rng;
    rng.begin(esp_random());
    return mesh::LocalIdentity(&rng);
  }

  RadioNoiseListener rng(*radio);
  return mesh::LocalIdentity(&rng);
}
