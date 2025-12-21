#pragma once

// SenseCAP Indicator D1L Variant
// Author: MeshCore Community / Latvian community at apraide.lv
// Based on research from Seeed SDK and Meshtastic firmware
//
// IMPORTANT: This variant uses an IO Expander for SX1262 control pins
// The IO expander is at I2C address 0x40 and requires HAL support
// See PIN_RESEARCH.md for details

#define RADIOLIB_STATIC_ONLY 1
#include <RadioLib.h>
#include <helpers/ESP32Board.h>
#include <helpers/radiolib/RadioLibWrappers.h>
#include <helpers/radiolib/CustomSX1262Wrapper.h>
#include <helpers/AutoDiscoverRTCClock.h>
#include <helpers/SensorManager.h>
#include <helpers/sensors/EnvironmentSensorManager.h>

#ifdef DISPLAY_CLASS
#include "SCIndicatorDisplay.h"
#include <helpers/ui/MomentaryButton.h>
#endif

// Global objects
extern ESP32Board board;
extern WRAPPER_CLASS radio_driver;
extern ESP32RTCClock fallback_clock;
extern AutoDiscoverRTCClock rtc_clock;
extern EnvironmentSensorManager sensors;

#ifdef USE_CUSTOM_RADIOLIB_HAL
class TCA9535_GPIO;  // Forward declaration
extern TCA9535_GPIO* gpio_expander;  // Shared I2C expander for radio + display
#endif

#ifdef DISPLAY_CLASS
extern DISPLAY_CLASS display;
extern MomentaryButton user_btn;
#endif

// Storage interface functions (Phase 0A)
bool initStorage();
bool testMapAccess();

// Radio interface functions
bool radio_init();
uint32_t radio_get_rng_seed();
void radio_set_params(float freq, float bw, uint8_t sf, uint8_t cr);
void radio_set_tx_power(uint8_t dbm);
mesh::LocalIdentity radio_new_identity();
