#pragma once

#include <Wire.h>
#include <Arduino.h>
#include <Mesh.h>

/**
 * Custom ESP32-LoRa board with:
 * - E22-400MM22S (SX1268) LoRa module
 * - 240x285 TFT display
 * - CW2015CHBD fuel gauge (I2C 0x62)
 * - BMP280 temperature/pressure sensor (I2C 0x76)
 * - LIS2DH12TR accelerometer (I2C 0x18)
 * - BM8563EMA RTC (I2C 0x51)
 * - Passive buzzer on IO3
 * - Two buttons: IO0 (G0) and IO45
 */

class CustomBoard : public mesh::MainBoard {
  uint8_t _startup_reason;
  bool _buzzer_on = false;

public:
  CustomBoard() {
    esp_reset_reason_t reason = esp_reset_reason();
    if (reason == ESP_RST_DEEPSLEEP) {
      _startup_reason = BD_STARTUP_RX_PACKET;
    } else {
      _startup_reason = BD_STARTUP_NORMAL;
    }
  }

  void begin() {
    // --- NOTE: GPIO1/GPIO3 on many ESP32-S3 boards are used for UART0 or USB.
    // --- Do NOT reconfigure them as GPIO until we know for certain they're free.
    // --- This conservative begin() just logs startup.

    Serial.printf("[CustomBoard] ESP32-S3 reset reason=%d\n", esp_reset_reason());

    // Initialize I2C bus for sensors - use standard 100kHz for reliability
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, 100000);
    Serial.printf("[CustomBoard] I2C: SDA=%d, SCL=%d @100kHz\n",
                  PIN_I2C_SDA, PIN_I2C_SCL);

    // Initialize buzzer pin - only drive LOW, no beeps at startup (prevents
    // triggering tone() on a pin that may not actually have a buzzer wired)
    pinMode(CUSTOM_BUZZER_PIN, OUTPUT);
    digitalWrite(CUSTOM_BUZZER_PIN, LOW);
    Serial.printf("[CustomBoard] Buzzer: pin=%d (PWM passive)\n", CUSTOM_BUZZER_PIN);

    Serial.println("[CustomBoard] Board init complete (conservative)");
  }

  /**
   * Simple buzzer control - uses tone() for passive buzzer.
   * @param duration_ms Duration in ms. If 0, beeps until stop() is called.
   * @param freq_hz Frequency in Hz (default 2000 Hz for passive buzzer)
   */
  void beep(unsigned long duration_ms = 0, unsigned int freq_hz = 2000) {
    if (duration_ms == 0) {
      tone(CUSTOM_BUZZER_PIN, freq_hz);
      _buzzer_on = true;
    } else {
      tone(CUSTOM_BUZZER_PIN, freq_hz, duration_ms);
    }
  }

  void stop_buzzer() {
    noTone(CUSTOM_BUZZER_PIN);
    digitalWrite(CUSTOM_BUZZER_PIN, LOW);
    _buzzer_on = false;
  }

  /**
   * Read battery voltage from CW2015CHBD fuel gauge.
   * CW2015 has VCELL register at 0x02-0x03 (16-bit, MSB first)
   * Resolution: 305.2 uV per LSB, full range 0V to ~20V
   *
   * @return Battery voltage in millivolts, or 0 if read fails
   */
  uint16_t getBattMilliVolts() override {
    Wire.beginTransmission(CW2015_I2C_ADDR);
    Wire.write(0x02);  // VCELL register start address
    if (Wire.endTransmission(false) != 0) {
      return 3300;  // fallback default
    }

    if (Wire.requestFrom((uint8_t)CW2015_I2C_ADDR, (uint8_t)2) != 2) {
      return 3300;
    }

    uint8_t msb = Wire.read();
    uint8_t lsb = Wire.read();
    uint16_t raw = ((uint16_t)msb << 8) | lsb;

    // CW2015 VCELL: 305.2 uV per bit
    // Voltage (mV) = raw * 305.2 / 1000
    uint32_t voltage_mv = (uint32_t)raw * 305UL / 1000UL;

    // Sanity check: typical LiPo is 3000-4200 mV
    if (voltage_mv < 2500 || voltage_mv > 5000) {
      return 3700;  // sensible default for ~half charged
    }
    return (uint16_t)voltage_mv;
  }

  /**
   * Read battery SoC (State of Charge) from CW2015 if available.
   * Register 0x04 is SOC (8-bit, units of 1/256 of full capacity)
   */
  uint8_t getBattPercent() {
    Wire.beginTransmission(CW2015_I2C_ADDR);
    Wire.write(0x04);  // SOC register
    if (Wire.endTransmission(false) != 0) {
      return 0;
    }
    if (Wire.requestFrom((uint8_t)CW2015_I2C_ADDR, (uint8_t)1) != 1) {
      return 0;
    }
    uint8_t soc = Wire.read();
    return soc * 100UL / 256UL;  // convert to 0-100%
  }

  bool setAdcMultiplier(float multiplier) override {
    (void)multiplier;
    return false;
  }

  float getAdcMultiplier() const override {
    return 0.0f;
  }

  const char* getManufacturerName() const override {
    return "ESP32-LoRa-E22";
  }

  void onBeforeTransmit() override {
    // Optional: short beep on transmit
  }

  void onAfterTransmit() override {
    // (empty)
  }

  void reboot() override {
    ESP.restart();
  }

  void powerOff() override {
    enterDeepSleep(0, -1);
  }

  uint32_t getGpio() override {
    return 0;
  }

  void setGpio(uint32_t values) override {
    (void)values;
  }

  uint8_t getStartupReason() const override {
    return _startup_reason;
  }

  bool startOTAUpdate(const char* id, char reply[]) override {
    (void)id;
    (void)reply;
    return false;
  }

  void enterDeepSleep(uint32_t secs, int pin_wake_btn) {
    if (pin_wake_btn >= 0) {
      esp_sleep_enable_ext1_wakeup((1ULL << pin_wake_btn), ESP_EXT1_WAKEUP_ANY_HIGH);
    }
    if (secs > 0) {
      esp_sleep_enable_timer_wakeup(secs * 1000000ULL);
    }
    Serial.println("Entering deep sleep...");
    Serial.flush();
    esp_deep_sleep_start();
  }
};