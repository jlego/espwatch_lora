#include <Arduino.h>
#include <Wire.h>
#include "target.h"

BoardType board;

static SPIClass spi(FSPI);  // LoRa 独占 FSPI (SPI2_HOST)，与 LCD 的 HSPI (SPI3_HOST) 完全独立
RADIO_CLASS radio = new Module(P_LORA_NSS, P_LORA_DIO_1, P_LORA_RESET, P_LORA_BUSY, spi, SPISettings());

#if defined(SX126X_RXEN)
  static const int RXEN_PIN = SX126X_RXEN;
#elif defined(P_LORA_RXEN)
  static const int RXEN_PIN = P_LORA_RXEN;
#else
  static const int RXEN_PIN = -1;
#endif

#if defined(SX126X_TXEN)
  static const int TXEN_PIN = SX126X_TXEN;
#elif defined(P_LORA_TXEN)
  static const int TXEN_PIN = P_LORA_TXEN;
#else
  static const int TXEN_PIN = -1;
#endif

WRAPPER_CLASS radio_driver(radio, board);

ESP32RTCClock fallback_clock;
AutoDiscoverRTCClock rtc_clock(fallback_clock);

// ===========================================================================
// CustomSensorManager (CUSTOM_BOARD mode): LIS2DH12
// ===========================================================================
#ifdef CUSTOM_BOARD

CustomSensorManager sensors;

// --- LIS2DH12 I2C helpers ---
void CustomSensorManager::_lis2dh_write_reg(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(LIS2DH12_I2C_ADDR);
  Wire.write(reg);
  Wire.write(val);
  Wire.endTransmission();
}

void CustomSensorManager::_lis2dh_read_regs(uint8_t reg, uint8_t *buf, uint8_t len) {
  Wire.beginTransmission(LIS2DH12_I2C_ADDR);
  Wire.write(reg | 0x80);  // auto-increment bit
  Wire.endTransmission(false);
  Wire.requestFrom((uint8_t)LIS2DH12_I2C_ADDR, len);
  for (uint8_t i = 0; i < len; i++) {
    buf[i] = Wire.read();
  }
}

bool CustomSensorManager::_lis2dh_init() {
  uint8_t chip_id = 0;
  _lis2dh_read_regs(0x0F, &chip_id, 1);
  if (chip_id != 0x33) {
    Serial.printf("[LIS2DH12] Wrong chip ID: 0x%02X (expected 0x33)\n", chip_id);
    return false;
  }
  Serial.printf("[LIS2DH12] Chip ID: 0x%02X OK\n", chip_id);

  // CTRL_REG1 (0x20): ODR=10Hz (0010), LPen=0 (normal mode), XYZ enabled
  _lis2dh_write_reg(0x20, 0b00010111);

  // CTRL_REG4 (0x23): BDU=1, BLE=0 (little endian), FS=+/-2g (00), HR=1 (high-res)
  _lis2dh_write_reg(0x23, 0b10001000);

  delay(100);
  Serial.println("[LIS2DH12] Initialized (+/-2g, 10Hz, high-res)");
  return true;
}

void CustomSensorManager::_lis2dh_read() {
  uint8_t raw[6];
  _lis2dh_read_regs(0x28, raw, 6);

  int16_t x = (int16_t)((raw[1] << 8) | raw[0]);
  int16_t y = (int16_t)((raw[3] << 8) | raw[2]);
  int16_t z = (int16_t)((raw[5] << 8) | raw[4]);

  // In high-res mode, 12-bit values. Sensitivity = 1mg/LSB for +/-2g
  // => 1 LSB = 0.001 g
  _accel_x = x * 0.001f;
  _accel_y = y * 0.001f;
  _accel_z = z * 0.001f;
}

// --- Public API ---
bool CustomSensorManager::begin() {
  Serial.println("[Sensors] CustomSensorManager::begin() - starting LIS2DH12");

  _lis2dh_ok = _lis2dh_init();

  if (_lis2dh_ok) {
    Serial.println("[Sensors] Active: LIS2DH12=YES");
    // Do initial read
    loop();
    return true;
  }
  Serial.println("[Sensors] WARNING: No sensors could be initialized");
  return false;
}

float CustomSensorManager::getAltitudeMeters() const {
  return 0.0f;
}

bool CustomSensorManager::querySensors(uint8_t requester_permissions, CayenneLPP& telemetry) {
  if (_lis2dh_ok) {
    telemetry.addAccelerometer(1, _accel_x, _accel_y, _accel_z);
  }
  return true;
}

void CustomSensorManager::loop() {
  if (_lis2dh_ok) {
    _lis2dh_read();
  }
}

#else  // !CUSTOM_BOARD - legacy GPS path

#ifdef HAS_GPS
static MicroNMEALocationProvider location_provider(Serial1, &rtc_clock);
CardputerSensorManager sensors(location_provider);

void CardputerSensorManager::start_gps() {
  Serial.println("[GPS] Starting GPS...");
  if (!gps_active) {
    Serial1.setPins(GPS_RX_PIN, GPS_TX_PIN);
    Serial1.begin(115200, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);
    gps_active = true;
  }
  _location->begin();
}

void CardputerSensorManager::stop_gps() {
  if (gps_active) {
    gps_active = false;
  }
  _location->stop();
}

bool CardputerSensorManager::begin() { return true; }

bool CardputerSensorManager::querySensors(uint8_t requester_permissions, CayenneLPP& telemetry) {
  if (requester_permissions & TELEM_PERM_LOCATION) {
    telemetry.addGPS(TELEM_CHANNEL_SELF, node_lat, node_lon, node_altitude);
  }
  return true;
}

void CardputerSensorManager::loop() {
  static long next_gps_update = 0;
  if (!gps_active) return;
  _location->loop();
  if (millis() > next_gps_update) {
    if (_location->isValid()) {
      node_lat = ((double)_location->getLatitude()) / 1000000.;
      node_lon = ((double)_location->getLongitude()) / 1000000.;
      node_altitude = ((double)_location->getAltitude()) / 1000.0;
    }
    next_gps_update = millis() + 180000;
  }
}

int CardputerSensorManager::getNumSettings() const { return 1; }
const char* CardputerSensorManager::getSettingName(int i) const { return i == 0 ? "gps" : NULL; }
const char* CardputerSensorManager::getSettingValue(int i) const {
  return i == 0 ? (gps_active ? "1" : "0") : NULL;
}
bool CardputerSensorManager::setSettingValue(const char* name, const char* value) {
  if (strcmp(name, "gps") == 0) {
    bool should_enable = (strcmp(value, "0") != 0);
    if (should_enable) { start_gps(); } else { stop_gps(); }
    return true;
  }
  return false;
}
#else
SensorManager sensors;
#endif

#endif  // CUSTOM_BOARD

// ===========================================================================
// Display & buttons
// ===========================================================================
#ifdef DISPLAY_CLASS
  DISPLAY_CLASS display;
  MomentaryButton user_btn(PIN_USER_BTN, 1000, true);
  MomentaryButton user_btn2(PIN_BTN_2, 1000, true);
#endif

// ===========================================================================
// Radio helpers
// ===========================================================================
#ifndef LORA_CR
  #define LORA_CR      5
#endif

bool radio_init() {
  fallback_clock.begin();
  rtc_clock.begin(Wire);

  // ============================================================
  // [1] Probe BUSY pin BEFORE touching SPI - this is the key
  //     RadioLib error -707 = RADIOLIB_ERR_SPI_CMD_FAILED
  // ============================================================
  Serial.printf("[LoRa] Probing BUSY pin (GPIO%d)...\n", P_LORA_BUSY);
  pinMode(P_LORA_BUSY, INPUT);
  delay(10);
  int busy_before = digitalRead(P_LORA_BUSY);
  Serial.printf("[LoRa] BUSY level before init: %s\n",
                busy_before == HIGH ? "HIGH (BAD - module busy or floating)" : "LOW (OK)");

  // If BUSY is floating HIGH, enable internal pull-down
  if (busy_before == HIGH) {
    Serial.println("[LoRa] BUSY is HIGH - enabling internal pulldown...");
    pinMode(P_LORA_BUSY, INPUT_PULLDOWN);
    delay(10);
    if (digitalRead(P_LORA_BUSY) == HIGH) {
      Serial.println("[LoRa] WARNING: BUSY still HIGH after pulldown - module may be in bad state");
    } else {
      Serial.println("[LoRa] BUSY now LOW after pulldown (was floating)");
    }
  }

  // ============================================================
  // [2] RESET handling - E22-400MM22S: no NRST pin
  //     Try longer power-up delay
  // ============================================================
  #if P_LORA_RESET >= 0
    Serial.printf("[LoRa] Hardware reset via GPIO%d...\n", P_LORA_RESET);
    pinMode(P_LORA_RESET, OUTPUT);
    digitalWrite(P_LORA_RESET, LOW);
    delay(50);
    digitalWrite(P_LORA_RESET, HIGH);
    delay(200);  // SX1268 max 200 ms boot time
  #else
    Serial.println("[LoRa] No NRST pin - wait 300 ms for module to stabilize");
    delay(300);
  #endif

  // Re-check BUSY after stabilization
  int busy_after = digitalRead(P_LORA_BUSY);
  Serial.printf("[LoRa] BUSY level after wait: %s\n",
                busy_after == HIGH ? "HIGH" : "LOW");

  // ============================================================
  // [3] RF switch pins - will be configured in std_init()
  // ============================================================
  #if defined(SX126X_RXEN) && defined(SX126X_TXEN)
    Serial.println("[LoRa] External RF switch will be configured in std_init()");
    Serial.printf("[LoRa] RF switch pins: RXEN=%d, TXEN=%d\n", RXEN_PIN, TXEN_PIN);
    // Initialize pins to known state
    pinMode(RXEN_PIN, OUTPUT);
    pinMode(TXEN_PIN, OUTPUT);
    digitalWrite(RXEN_PIN, LOW);
    digitalWrite(TXEN_PIN, LOW);
  #else
    Serial.println("[LoRa] Using DIO2 for RF switch");
  #endif

  // ============================================================
  // [4] Initialize SPI for LoRa - use FSPI (SPI2_HOST) on ESP32-S3
  // (LCD uses HSPI (SPI3_HOST) on a separate hardware controller, so no conflict)
  // ============================================================
  spi.begin(P_LORA_SCLK, P_LORA_MISO, P_LORA_MOSI, P_LORA_NSS);
  Serial.printf("[LoRa] SPI initialized: SCK=%d, MISO=%d, MOSI=%d, NSS=%d\n",
                P_LORA_SCLK, P_LORA_MISO, P_LORA_MOSI, P_LORA_NSS);

  // Quick SPI self-test: toggle NSS manually and check MISO
  pinMode(P_LORA_NSS, OUTPUT);
  digitalWrite(P_LORA_NSS, HIGH);
  digitalWrite(P_LORA_NSS, LOW);
  delayMicroseconds(10);
  digitalWrite(P_LORA_NSS, HIGH);
  Serial.println("[LoRa] SPI NSS test: OK");

  // ============================================================
  // [4b] Direct SPI GET_STATUS test - verify chip is alive
  //      SX1268: CMD_GET_STATUS = 0xC0, returns status in dummy byte
  // ============================================================
  {
    // Wait for BUSY low before sending (SX126x spec: BUSY must be LOW)
    unsigned long wait_start = millis();
    while (digitalRead(P_LORA_BUSY) == HIGH && (millis() - wait_start) < 50) {
      delayMicroseconds(10);
    }
    int busy_at_cmd = digitalRead(P_LORA_BUSY);

    digitalWrite(P_LORA_NSS, LOW);
    spi.beginTransaction(SPISettings(2000000, MSBFIRST, SPI_MODE0));
    spi.transfer(0xC0);          // Send GET_STATUS command
    uint8_t status = spi.transfer(0x00);  // Dummy byte to read status
    digitalWrite(P_LORA_NSS, HIGH);
    spi.endTransaction();

    Serial.printf("[LoRa] GET_STATUS SPI read: 0x%02X (BUSY=%s)\n", status,
                  busy_at_cmd == HIGH ? "HIGH" : "LOW");

    // Status bits: [7:6] = CmdStatus, [5:4] = ChipMode
    uint8_t cmd_status = (status >> 6) & 0x03;
    uint8_t chip_mode = (status >> 4) & 0x03;

    const char* mode_str = "UNKNOWN";
    switch (chip_mode) {
      case 0x00: mode_str = "STBY_RC"; break;
      case 0x01: mode_str = "STBY_XOSC"; break;
      case 0x02: mode_str = "FS"; break;
      case 0x03: mode_str = "RX/TX"; break;
    }
    Serial.printf("[LoRa] Chip mode=0x%02X (%s), CmdStatus=0x%02X\n",
                  chip_mode, mode_str, cmd_status);

    if (status == 0xFF || status == 0x00) {
      Serial.println("[LoRa] SPI PROBLEM: 0x00 or 0xFF - MISO not connected, or chip dead, or BUSY stuck HIGH");
    } else if (cmd_status == 0x00) {
      Serial.printf("[LoRa] SPI OK - chip alive (chip_mode=0x%02X, cmd_status=0x%02X)\n", chip_mode, cmd_status);
    } else {
      Serial.printf("[LoRa] chip reports cmd_status=0x%02X (non-zero - check wiring?)\n", cmd_status);
    }
  }

  // ============================================================
  // [5] Try RadioLib initialization
  // ============================================================
#ifdef SX1262_RADIO
  Serial.println("[LoRa] Calling radio.std_init(SX1262)...");
#elif defined(SX1268_RADIO)
  Serial.println("[LoRa] Calling radio.std_init(SX1268)...");
#endif

  bool rc = radio.std_init(&spi);
  Serial.printf("[LoRa] std_init() returned: %s\n", rc ? "true" : "false");

  if (rc) {
#ifdef SX1262_RADIO
    Serial.println("[LoRa] SX1262 initialized successfully");
#elif defined(SX1268_RADIO)
    Serial.println("[LoRa] SX1268 initialized successfully");
#endif
    // PA config is now handled in CustomSX1268::std_init()
  } else {
#ifdef SX1262_RADIO
    Serial.println("[LoRa] ERROR: SX1262 initialization failed!");
#elif defined(SX1268_RADIO)
    Serial.println("[LoRa] ERROR: SX1268 initialization failed!");
#endif
    Serial.printf("[LoRa] Debug: BUSY final level=%s\n",
                  digitalRead(P_LORA_BUSY) == HIGH ? "HIGH" : "LOW");
  }

  return rc;
}

uint32_t radio_get_rng_seed() {
  return radio.random(0x7FFFFFFF);
}

void radio_set_params(float freq, float bw, uint8_t sf, uint8_t cr) {
  radio.setFrequency(freq);
  radio.setSpreadingFactor(sf);
  radio.setBandwidth(bw);
  radio.setCodingRate(cr);
}

void radio_set_tx_power(uint8_t dbm) {
  Serial.printf("[LoRa] Setting TX power to %d dBm\n", dbm);
  int16_t result = radio.setOutputPower(dbm);
  if (result == 0) {
    Serial.printf("[LoRa] TX power set successfully to %d dBm\n", dbm);
  } else {
    Serial.printf("[LoRa] TX power set failed: %d\n", result);
  }
}

mesh::LocalIdentity radio_new_identity() {
  RadioNoiseListener rng(radio);
  return mesh::LocalIdentity(&rng);
}