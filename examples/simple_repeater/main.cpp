#include <Arduino.h>   // needed for PlatformIO
#include <Mesh.h>
#include <time.h>
#include <sys/time.h>
#include <WiFi.h>

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
 * Synchronize system time via WiFi NTP
 *
 * The D1L lacks a battery-backed RTC, so on every boot the system time
 * defaults to epoch (1970). This function connects to WiFi, fetches accurate
 * time from an NTP server, and then disconnects to free resources.
 *
 * WiFi credentials are configured via build flags in platformio.ini.
 * Timezone is set for Riga, Latvia (EET: UTC+2 winter, UTC+3 summer DST).
 */
void syncTimeViaNTP() {
  Serial.println("\n=== NTP Time Synchronization ===");
  Serial.flush();

  // Check if WiFi credentials are configured
  #ifndef WIFI_SSID
    Serial.println("ERROR: WIFI_SSID not defined in platformio.ini");
    Serial.println("Skipping NTP sync - time will remain at epoch");
    Serial.flush();
    return;
  #endif

  const char* ssid = WIFI_SSID;
  const char* password = WIFI_PASSWORD;
  const char* ntpServer = NTP_SERVER;
  const long gmtOffset_sec = GMT_OFFSET_SEC;
  const int daylightOffset_sec = DAYLIGHT_OFFSET_SEC;

  // Connect to WiFi
  Serial.print("Connecting to WiFi: ");
  Serial.print(ssid);
  Serial.print("... ");
  Serial.flush();

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  // Wait for connection with 10 second timeout
  unsigned long startAttemptTime = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 10000) {
    delay(500);
    Serial.print(".");
    Serial.flush();
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println(" FAILED");
    Serial.println("ERROR: Could not connect to WiFi");
    Serial.println("Continuing without time sync - timestamps will be incorrect!");
    Serial.flush();
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    return;
  }

  Serial.println(" CONNECTED");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());
  Serial.flush();

  // Configure time with NTP server and timezone
  Serial.print("Fetching time from NTP server: ");
  Serial.print(ntpServer);
  Serial.print("... ");
  Serial.flush();

  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);

  // Wait for time to be set (tm_year > 70 means year > 1970)
  struct tm timeinfo;
  int retry = 0;
  while (!getLocalTime(&timeinfo) && retry < 10) {
    delay(500);
    retry++;
    Serial.print(".");
    Serial.flush();
  }

  if (timeinfo.tm_year <= 70) {
    Serial.println(" FAILED");
    Serial.println("ERROR: Could not fetch time from NTP server");
    Serial.println("Continuing without time sync - timestamps will be incorrect!");
    Serial.flush();
  } else {
    Serial.println(" SUCCESS");
    Serial.print("Current time: ");
    Serial.print(&timeinfo, "%A, %B %d %Y %H:%M:%S");
    Serial.println();
    Serial.print("Timezone: EET (UTC+2/+3 with DST)");
    Serial.println();
    Serial.flush();
  }

  // Disconnect WiFi to free resources and reduce interference
  Serial.println("Disconnecting WiFi...");
  Serial.flush();
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  delay(100);

  Serial.println("=== NTP Sync Complete ===\n");
  Serial.flush();
}

static char command[160];

void setup() {
  // Initialize Serial
  Serial.begin(115200);
  delay(1000);

  // Synchronize system time via WiFi NTP
  syncTimeViaNTP();

  Serial.println("=== MeshCore D1L Repeater Boot ===");
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
