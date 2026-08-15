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
}

// JPEG Constants
static const uint8_t JPEG_MAGIC_HEADER[] = {0xFF, 0xD8, 0xFF};
static const size_t JPEG_MAGIC_HEADER_SIZE = sizeof(JPEG_MAGIC_HEADER);

#if RADIO_DRIVER_AVAILABLE
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
#if RADIO_DRIVER_AVAILABLE
static CustomSX1262 dummy_radio(new Module(0, 0, 0, 0, spi));
CustomSX1262Wrapper radio_driver(dummy_radio, board);
#endif

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
  log_e("FATAL: %s", error_msg);
  cleanup_all_resources(error_msg);
  log_e("BOOT FAILED");
  return false;
}

// Other globals (keep as-is)
ESP32RTCClock fallback_clock;
#ifdef USE_RTC
AutoDiscoverRTCClock rtc_clock(fallback_clock);
#endif
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
  log_i("SPIFFS Initialization");

  if (SPIFFS.begin(false)) {
    log_i("SPIFFS mounted (existing filesystem)");

    size_t totalBytes = SPIFFS.totalBytes();
    size_t usedBytes = SPIFFS.usedBytes();
    size_t freeBytes = totalBytes - usedBytes;

    log_i("Total: %zu bytes (%.2f MB)", totalBytes, totalBytes / 1024.0 / 1024.0);
    log_i("Used:  %zu bytes (%.2f MB)", usedBytes, usedBytes / 1024.0 / 1024.0);
    log_i("Free:  %zu bytes (%.2f MB)", freeBytes, freeBytes / 1024.0 / 1024.0);
    return true;
  }

  log_w("SPIFFS mount failed");

#ifdef SPIFFS_AUTO_FORMAT
  log_i("Auto-format enabled - formatting...");
  if (!SPIFFS.begin(true)) {
    log_e("Format failed!");
    return false;
  }
  log_i("SPIFFS formatted and mounted");
#else
  log_w("Auto-format disabled");
  log_i("To enable: add -D SPIFFS_AUTO_FORMAT=1");
  return false;
#endif

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

  // NOTE: PSRAM diagnostics intentionally disabled for D1L

  // Initialize I2C Mutex
  if (i2c_bus_mutex == nullptr) {
    i2c_bus_mutex = xSemaphoreCreateMutex();
    if (i2c_bus_mutex == nullptr) {
      return fail_and_cleanup("Failed to create I2C mutex");
    }
  }

  // SPIFFS Storage Test (non-fatal: map features degrade gracefully if unavailable)
  if (initStorage()) {
    testMapAccess();
  }

  // Initialize Clocks
  fallback_clock.begin();
#ifdef USE_RTC
  rtc_clock.begin(Wire);
#endif

#if RADIO_DRIVER_AVAILABLE
  // Initialize HAL
#ifdef USE_CUSTOM_RADIOLIB_HAL
  // NOTE: do not emit Serial output in this window — early prints here crash the D1L

  {
    I2C_Lock lock;
    if (!lock.isLocked()) {
      return fail_and_cleanup("I2C mutex lock failed");
    }

    if (!Wire.begin(PIN_BOARD_SDA, PIN_BOARD_SCL, TCA9535_I2C_FREQ)) {
      return fail_and_cleanup("I2C initialization failed");
    }
  }

  // STEP 1: Create TCA9535_GPIO object (just construct, don't call begin() yet)
  gpio_expander = new TCA9535_GPIO(TCA9535_I2C_ADDR);

  // Initialize SPI (needed before HAL creation)
  spi.begin(LORA_SCK, LORA_MISO, LORA_MOSI);

  // STEP 2: Create CustomRadioLibHal (this creates the global I2C mutex)
  SPISettings spiSettings(2000000, MSBFIRST, SPI_MODE0);
  custom_hal = new CustomRadioLibHal(gpio_expander, spi, spiSettings);
  custom_hal->init();

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

#else
  // (no custom HAL compiled in: radio init uses the standard RadioLib HAL)
#endif

  // Initialize Radio
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

  // CRITICAL FIX: RadioLib bug workaround for interrupt pin
  // RadioLib's SX126x implementation may not properly preserve the Module's IRQ pin
  // when using custom HAL. Verify and fix if necessary.
  Serial.printf("DEBUG: Checking Module IRQ pin after radio.begin()...\n");
  Serial.printf("DEBUG: Module IRQ pin = %d (expected: %d)\n",
                radio->mod->irqPin, TCA9535_GPIO::LORA_DIO1);

  if (radio->mod->irqPin != TCA9535_GPIO::LORA_DIO1) {
    Serial.println("WARNING: Module IRQ pin was reset! Fixing...");
    radio->mod->irqPin = TCA9535_GPIO::LORA_DIO1;
    Serial.printf("DEBUG: Module IRQ pin corrected to %d\n", radio->mod->irqPin);
  } else {
    Serial.println("DEBUG: Module IRQ pin is correct");
  }
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
#endif
