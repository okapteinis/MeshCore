#pragma once

#include <esp_log.h>
#include <ESP.h>
#include <helpers/ui/LGFXDisplay.h>

#define LGFX_USE_V1
#include <LovyanGFX.hpp>

#include <lgfx/v1/platforms/esp32s3/Panel_RGB.hpp>
#include <lgfx/v1/platforms/esp32s3/Bus_RGB.hpp>

#ifdef USE_CUSTOM_RADIOLIB_HAL
#include "hal/TCA9535_GPIO.h"
extern TCA9535_GPIO* gpio_expander;  // Shared I2C expander for radio + display
#endif

class LGFX : public lgfx::LGFX_Device
{
  lgfx::Panel_ST7701 _panel_instance;
  lgfx::Bus_RGB _bus_instance;
  lgfx::Light_PWM _light_instance;
  lgfx::Touch_FT5x06 _touch_instance;

public:
  const uint16_t screenWidth = 480;
  const uint16_t screenHeight = 480;

  bool hasButton(void) { return true; }

  LGFX(void)
  {
    Serial.println("[LGFX] Constructor start");
    Serial.flush();
    {
        Serial.println("[LGFX] Configuring panel basic settings...");
        Serial.flush();
        auto cfg = _panel_instance.config();
        cfg.memory_width = 480;
        cfg.memory_height = 480;
        cfg.panel_width = screenWidth;
        cfg.panel_height = screenHeight;
        cfg.offset_x = 0;
        cfg.offset_y = 0;
        cfg.offset_rotation = 1;
        _panel_instance.config(cfg);
        Serial.println("[LGFX] Panel basic config done");
        Serial.flush();
    }

    {
        Serial.println("[LGFX] Configuring panel detail (SPI pins)...");
        Serial.flush();
        auto cfg = _panel_instance.config_detail();
        cfg.pin_cs = -1;  // CS controlled via TCA9535 I2C expander, not direct GPIO
        cfg.pin_sclk = 41;
        cfg.pin_mosi = 48;

        Serial.println("[LGFX] Checking PSRAM...");
        Serial.flush();
        // PSRAM Runtime Detection
        size_t psram_size = ESP.getPsramSize();
        size_t psram_free = ESP.getFreePsram();

        if (psram_size == 0) {
          log_e("CRITICAL: PSRAM not detected!");
          log_e("Display framebuffer needs 460KB, internal RAM has 327KB");
          log_e("Using fallback: 240x240 resolution");

          cfg.use_psram = 0;

          auto panel_cfg = _panel_instance.config();
          panel_cfg.panel_width = 240;
          panel_cfg.panel_height = 240;
          panel_cfg.memory_width = 240;
          panel_cfg.memory_height = 240;
          _panel_instance.config(panel_cfg);

          log_w("Display: 240x240 fallback mode (no PSRAM)");
        } else {
          cfg.use_psram = 1;

          const size_t required = 460800;
          if (psram_free < required) {
            log_w("Low PSRAM: %u bytes free (need %u)", psram_free, required);
          } else {
            log_i("PSRAM OK: %u KB total, %u KB free",
                  psram_size / 1024, psram_free / 1024);
          }
        }

        _panel_instance.config_detail(cfg);
        Serial.println("[LGFX] Panel detail config done");
        Serial.flush();
    }

    {
        Serial.println("[LGFX] Configuring Bus_RGB...");
        Serial.flush();
        auto cfg = _bus_instance.config();
        cfg.panel = &_panel_instance;

        cfg.freq_write = 8000000;
        cfg.pin_henable = 18;

        cfg.pin_pclk = 21;
        cfg.pclk_active_neg = 0;
        cfg.pclk_idle_high = 0;
        cfg.de_idle_high = 1;

        cfg.pin_hsync = 16;
        cfg.hsync_polarity = 0;
        cfg.hsync_front_porch = 10;
        cfg.hsync_pulse_width = 8;
        cfg.hsync_back_porch = 50;

        cfg.pin_vsync = 17;
        cfg.vsync_polarity = 0;
        cfg.vsync_front_porch = 10;
        cfg.vsync_pulse_width = 8;
        cfg.vsync_back_porch = 20;

        cfg.pin_d0 = 15;
        cfg.pin_d1 = 14;
        cfg.pin_d2 = 13;
        cfg.pin_d3 = 12;
        cfg.pin_d4 = 11;
        cfg.pin_d5 = 10;
        cfg.pin_d6 = 9;
        cfg.pin_d7 = 8;
        cfg.pin_d8 = 7;
        cfg.pin_d9 = 6;
        cfg.pin_d10 = 5;
        cfg.pin_d11 = 4;
        cfg.pin_d12 = 3;
        cfg.pin_d13 = 2;
        cfg.pin_d14 = 1;
        cfg.pin_d15 = 0;

        _bus_instance.config(cfg);
        Serial.println("[LGFX] Bus_RGB config done");
        Serial.flush();
    }

    Serial.println("[LGFX] Setting bus...");
    Serial.flush();
    _panel_instance.setBus(&_bus_instance);
    Serial.println("[LGFX] Bus set");
    Serial.flush();

    {
        Serial.println("[LGFX] Configuring backlight...");
        Serial.flush();
        auto cfg = _light_instance.config();
        cfg.pin_bl = 45;
        _light_instance.config(cfg);
        Serial.println("[LGFX] Backlight config done");
        Serial.flush();
    }

    Serial.println("[LGFX] Setting light...");
    Serial.flush();
    _panel_instance.light(&_light_instance);
    Serial.println("[LGFX] Light set");
    Serial.flush();

    {
        Serial.println("[LGFX] Configuring touch...");
        Serial.flush();
        auto cfg = _touch_instance.config();
        cfg.pin_cs = GPIO_NUM_NC;
        cfg.x_min = 0;
        cfg.x_max = 479;
        cfg.y_min = 0;
        cfg.y_max = 479;
        cfg.pin_int = GPIO_NUM_NC;
        cfg.pin_rst = GPIO_NUM_NC;
        cfg.bus_shared = true;
        cfg.offset_rotation = 0;

        cfg.i2c_port = 0;
        cfg.i2c_addr = 0x48;
        cfg.pin_sda = 39;
        cfg.pin_scl = 40;
        cfg.freq = 400000;
        _touch_instance.config(cfg);
        Serial.println("[LGFX] Touch config done");
        Serial.flush();
        _panel_instance.setTouch(&_touch_instance);
        Serial.println("[LGFX] Touch set");
        Serial.flush();
    }

    Serial.println("[LGFX] Setting panel...");
    Serial.flush();
    setPanel(&_panel_instance);
    Serial.println("[LGFX] Constructor complete!");
    Serial.flush();
  }
};

class SCIndicatorDisplay : public LGFXDisplay {
  LGFX disp;
public:
  SCIndicatorDisplay() : LGFXDisplay(480, 480, disp) {}
};
