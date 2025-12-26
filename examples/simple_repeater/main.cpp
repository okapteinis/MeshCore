#include <Arduino.h>   // needed for PlatformIO
#include <Mesh.h>
#include <time.h>
#include <sys/time.h>

#include "MyMesh.h"

#ifdef BLE_PIN_CODE
  #include <helpers/esp32/SerialBLEInterface.h>
  #ifndef BLE_NAME_PREFIX
    #define BLE_NAME_PREFIX "MeshCore-"
  #endif
  SerialBLEInterface ble_interface;
#endif

#ifdef DISPLAY_CLASS
  #include "UITask.h"
  static UITask ui_task(display);
#endif

StdRNG fast_rng;
SimpleMeshTables tables;

MyMesh the_mesh(board, radio_driver, *new ArduinoMillis(), fast_rng, rtc_clock, tables);

void halt() {
  while (1) ;
}

/**
 * Set ESP32 system time to firmware compilation time (fallback)
 *
 * The D1L lacks a battery-backed RTC, so on every boot the system time
 * defaults to epoch (1970). This sets it to the firmware build time as a
 * reasonable fallback until time is synced via mesh (CMD_SET_DEVICE_TIME).
 */
void setTimeFromCompile() {
  // Parse __DATE__ and __TIME__ macros
  // __DATE__ format: "Dec 25 2025"
  // __TIME__ format: "14:30:00"

  const char* date_str = __DATE__;
  const char* time_str = __TIME__;

  struct tm compile_time = {0};

  // Parse month
  const char* months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                          "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
  for (int i = 0; i < 12; i++) {
    if (strncmp(date_str, months[i], 3) == 0) {
      compile_time.tm_mon = i;
      break;
    }
  }

  // Parse day and year
  sscanf(date_str + 4, "%d %d", &compile_time.tm_mday, &compile_time.tm_year);
  compile_time.tm_year -= 1900; // tm_year is years since 1900

  // Parse time
  sscanf(time_str, "%d:%d:%d", &compile_time.tm_hour, &compile_time.tm_min, &compile_time.tm_sec);

  // Convert to epoch time
  time_t epoch_time = mktime(&compile_time);

  // Set system time
  struct timeval tv = {0};
  tv.tv_sec = epoch_time;
  tv.tv_usec = 0;
  settimeofday(&tv, NULL);

  Serial.printf("System time set to compile time: %s %s\n", date_str, time_str);
  Serial.println("Waiting for mesh time sync from companion...");
}

static char command[160];

void setup() {
  // Initialize Serial
  Serial.begin(115200);
  delay(1000);

  Serial.println("\n=== MeshCore D1L Repeater Boot ===");

  // Set system time to compile time (fallback until mesh sync)
  setTimeFromCompile();

  Serial.println();
  Serial.printf("Free heap: %u bytes\n", ESP.getFreeHeap());
  Serial.printf("CPU: %u MHz\n", ESP.getCpuFreqMHz());
  Serial.flush();

  board.begin();

#ifdef DISPLAY_CLASS
  if (display.begin()) {
    display.startFrame();
    display.setCursor(0, 0);
    display.print("Please wait...");
    display.endFrame();
  }
#endif

  if (!radio_init()) {
    halt();
  }

  fast_rng.begin(radio_get_rng_seed());

  FILESYSTEM* fs;
#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  InternalFS.begin();
  fs = &InternalFS;
  IdentityStore store(InternalFS, "");
#elif defined(ESP32)
  SPIFFS.begin(true);
  fs = &SPIFFS;
  IdentityStore store(SPIFFS, "/identity");
#elif defined(RP2040_PLATFORM)
  LittleFS.begin();
  fs = &LittleFS;
  IdentityStore store(LittleFS, "/identity");
  store.begin();
#else
  #error "need to define filesystem"
#endif
  if (!store.load("_main", the_mesh.self_id)) {
    MESH_DEBUG_PRINTLN("Generating new keypair");
    the_mesh.self_id = radio_new_identity();   // create new random identity
    int count = 0;
    while (count < 10 && (the_mesh.self_id.pub_key[0] == 0x00 || the_mesh.self_id.pub_key[0] == 0xFF)) {  // reserved id hashes
      the_mesh.self_id = radio_new_identity(); count++;
    }
    store.save("_main", the_mesh.self_id);
  }

  Serial.print("Repeater ID: ");
  mesh::Utils::printHex(Serial, the_mesh.self_id.pub_key, PUB_KEY_SIZE); Serial.println();

  command[0] = 0;

  sensors.begin();

  the_mesh.begin(fs);

#ifdef DISPLAY_CLASS
  ui_task.begin(the_mesh.getNodePrefs(), FIRMWARE_BUILD_DATE, FIRMWARE_VERSION);
#endif

#ifdef BLE_PIN_CODE
  // Initialize BLE for remote management
  char ble_name[48];
  snprintf(ble_name, sizeof(ble_name), "%s%s", BLE_NAME_PREFIX, the_mesh.getNodeName());
  ble_interface.begin(ble_name, BLE_PIN_CODE);
  the_mesh.startInterface(ble_interface);
  Serial.print("BLE advertising as: "); Serial.println(ble_name);
#endif

  // send out initial Advertisement to the mesh
  the_mesh.sendSelfAdvertisement(16000);
}

void loop() {
  int len = strlen(command);
  while (Serial.available() && len < sizeof(command)-1) {
    char c = Serial.read();
    if (c != '\n') {
      command[len++] = c;
      command[len] = 0;
      Serial.print(c);
    }
    if (c == '\r') break;
  }
  if (len == sizeof(command)-1) {  // command buffer full
    command[sizeof(command)-1] = '\r';
  }

  if (len > 0 && command[len - 1] == '\r') {  // received complete line
    Serial.print('\n');
    command[len - 1] = 0;  // replace newline with C string null terminator
    char reply[160];
    the_mesh.handleCommand(0, command, reply);  // NOTE: there is no sender_timestamp via serial!
    if (reply[0]) {
      Serial.print("  -> "); Serial.println(reply);
    }

    command[0] = 0;  // reset command buffer
  }

  // BLE interface is now handled inside the_mesh.loop()
  the_mesh.loop();
  sensors.loop();
#ifdef DISPLAY_CLASS
  ui_task.loop();
#endif
  rtc_clock.tick();
}
