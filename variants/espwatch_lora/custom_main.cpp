#include <Arduino.h>
#include <Mesh.h>
#include "MyMesh.h"
#include "target.h"

StdRNG fast_rng;
SimpleMeshTables tables;

static unsigned long next_refresh = 0;

#if defined(ESP32)
  #include <SPIFFS.h>
  DataStore store(SPIFFS, rtc_clock);
#endif

#ifdef BLE_PIN_CODE
  #include <helpers/esp32/SerialBLEInterface.h>
  SerialBLEInterface serial_interface;
#else
  #include <helpers/ArduinoSerialInterface.h>
  ArduinoSerialInterface serial_interface;
#endif

MyMesh the_mesh(radio_driver, fast_rng, rtc_clock, tables, store
#ifdef DISPLAY_CLASS
   , nullptr
#endif
);

void halt() {
  while (1) {
    delay(1000);
    Serial.println("System halted");
  }
}

void draw_startup_screen() {
#ifdef DISPLAY_CLASS
  if (!display.begin()) return;

  display.startFrame(DisplayDriver::DARK);
  display.setColor(DisplayDriver::LIGHT);

  display.setTextSize(2);
  display.drawTextCentered(120, 30, "MeshCore");

  display.setTextSize(1);
  display.drawTextCentered(120, 80, "ESPWatch LoRa E22");
  display.drawTextCentered(120, 120, "Sensors: BMP280 LIS2DH12");
  display.drawTextCentered(120, 140, "CW2015 BM8563");
  display.drawTextCentered(120, 260, "Made by Stachu");

  display.endFrame();
#endif
}

// ===========================================================================
// Screens for CUSTOM_BOARD mode with sensor access
// ===========================================================================
#ifdef CUSTOM_BOARD

enum DisplayPage {
  PAGE_STATUS = 0,
  PAGE_SENSORS,
  PAGE_ACCEL,
  PAGE_INFO,
  NUM_PAGES
};

static int current_page = PAGE_STATUS;

void draw_page_status() {
  char buf[128];

  display.startFrame(DisplayDriver::DARK);
  display.setColor(DisplayDriver::LIGHT);
  display.setTextSize(1);

  // Top header
  display.setTextSize(1);
  display.setCursor(0, 5);
  snprintf(buf, sizeof(buf), "[%d/%d] STATUS", current_page + 1, NUM_PAGES);
  display.print(buf);

  // Node info
  display.setCursor(0, 20);
  snprintf(buf, sizeof(buf), "Node: %s", the_mesh.getNodeName());
  display.print(buf);

  // Radio parameters
  display.setCursor(0, 35);
  snprintf(buf, sizeof(buf), "Freq: %.3f MHz", (float)LORA_FREQ / 1000000.0f);
  display.print(buf);

  display.setCursor(0, 50);
  snprintf(buf, sizeof(buf), "SF%u  BW%.1f  CR%u",
    (unsigned)LORA_SF, (float)LORA_BW / 1000.0f, (unsigned)LORA_CR);
  display.print(buf);

  display.setCursor(0, 65);
  snprintf(buf, sizeof(buf), "TX: %u dBm", (unsigned)LORA_TX_POWER);
  display.print(buf);

#ifdef BLE_PIN_CODE
  display.setCursor(0, 80);
  snprintf(buf, sizeof(buf), "BLE Pin: %lu", the_mesh.getBLEPin());
  display.print(buf);
#endif

  // Battery (CW2015)
  uint16_t vbatt = board.getBattMilliVolts();
  display.setCursor(0, 100);
  snprintf(buf, sizeof(buf), "VBat: %u mV", vbatt);
  display.print(buf);

  display.setCursor(0, 115);
  uint8_t bpct = board.getBattPercent();
  snprintf(buf, sizeof(buf), "Bat%%: %u%%", bpct);
  display.print(buf);

  // Uptime
  unsigned long uptime_s = millis() / 1000;
  unsigned long hours = uptime_s / 3600;
  unsigned long mins = (uptime_s % 3600) / 60;
  unsigned long secs = uptime_s % 60;
  display.setCursor(0, 135);
  snprintf(buf, sizeof(buf), "Up: %02lu:%02lu:%02lu", hours, mins, secs);
  display.print(buf);

  // Button hints (bottom)
  display.setCursor(0, 260);
  display.print("G0: Advert  G45: Next");

  display.endFrame();
}

void draw_page_sensors() {
  char buf[128];

  display.startFrame(DisplayDriver::DARK);
  display.setColor(DisplayDriver::LIGHT);
  display.setTextSize(1);

  display.setCursor(0, 5);
  snprintf(buf, sizeof(buf), "[%d/%d] SENSORS", current_page + 1, NUM_PAGES);
  display.print(buf);

  display.setCursor(0, 20);
  snprintf(buf, sizeof(buf), "Temp: %.2f C", sensors.getTemperature());
  display.print(buf);

  display.setCursor(0, 35);
  snprintf(buf, sizeof(buf), "Pressure: %.2f hPa", sensors.getPressure());
  display.print(buf);

  display.setCursor(0, 50);
  snprintf(buf, sizeof(buf), "Altitude: %.1f m", sensors.getAltitudeMeters());
  display.print(buf);

  // Rough weather indicator
  display.setCursor(0, 75);
  float p = sensors.getPressure();
  if (p >= 1018.0f) {
    display.print("Weather: Sunny/High-P");
  } else if (p >= 1010.0f) {
    display.print("Weather: Fair");
  } else if (p >= 1000.0f) {
    display.print("Weather: Cloudy");
  } else {
    display.print("Weather: Storm/Low-P");
  }

  display.setCursor(0, 95);
  snprintf(buf, sizeof(buf), "VBat: %u mV", board.getBattMilliVolts());
  display.print(buf);

  display.setCursor(0, 110);
  snprintf(buf, sizeof(buf), "Bat%%: %u%%", board.getBattPercent());
  display.print(buf);

  display.setCursor(0, 260);
  display.print("G0: Advert  G45: Next");
  display.endFrame();
}

void draw_page_accel() {
  char buf[128];

  display.startFrame(DisplayDriver::DARK);
  display.setColor(DisplayDriver::LIGHT);
  display.setTextSize(1);

  display.setCursor(0, 5);
  snprintf(buf, sizeof(buf), "[%d/%d] ACCELEROMETER", current_page + 1, NUM_PAGES);
  display.print(buf);

  display.setCursor(0, 20);
  snprintf(buf, sizeof(buf), "X: %+6.3f g", sensors.getAccelX());
  display.print(buf);

  display.setCursor(0, 35);
  snprintf(buf, sizeof(buf), "Y: %+6.3f g", sensors.getAccelY());
  display.print(buf);

  display.setCursor(0, 50);
  snprintf(buf, sizeof(buf), "Z: %+6.3f g", sensors.getAccelZ());
  display.print(buf);

  // Total acceleration magnitude
  float ax = sensors.getAccelX();
  float ay = sensors.getAccelY();
  float az = sensors.getAccelZ();
  float mag = sqrtf(ax*ax + ay*ay + az*az);

  display.setCursor(0, 70);
  snprintf(buf, sizeof(buf), "|A|: %.3f g", mag);
  display.print(buf);

  // Orientation
  display.setCursor(0, 90);
  display.print("Orient:");
  if (az > 0.7f) {
    display.print("UP (flat, top up)");
  } else if (az < -0.7f) {
    display.print("DOWN (flipped)");
  } else if (ax > 0.7f) {
    display.print("TILT +X (right)");
  } else if (ax < -0.7f) {
    display.print("TILT -X (left)");
  } else if (ay > 0.7f) {
    display.print("TILT +Y (toward you)");
  } else if (ay < -0.7f) {
    display.print("TILT -Y (away)");
  } else {
    display.print("MOVING / UNSTABLE");
  }

  // Simple tap/motion detector (threshold: |A| far from 1g)
  display.setCursor(0, 110);
  if (fabsf(mag - 1.0f) > 0.2f) {
    display.print("Motion: ACTIVE");
  } else {
    display.print("Motion: STILL");
  }

  display.setCursor(0, 260);
  display.print("G0: Advert  G45: Next");
  display.endFrame();
}

void draw_page_info() {
  char buf[128];

  display.startFrame(DisplayDriver::DARK);
  display.setColor(DisplayDriver::LIGHT);
  display.setTextSize(1);

  display.setCursor(0, 5);
  snprintf(buf, sizeof(buf), "[%d/%d] SYSTEM", current_page + 1, NUM_PAGES);
  display.print(buf);

  display.setCursor(0, 20);
  snprintf(buf, sizeof(buf), "Name: %s", the_mesh.getNodeName());
  display.print(buf);

  display.setCursor(0, 35);
  snprintf(buf, sizeof(buf), "FW: %s", FIRMWARE_VERSION);
  display.print(buf);

  display.setCursor(0, 50);
  snprintf(buf, sizeof(buf), "Build: %s", FIRMWARE_BUILD_DATE);
  display.print(buf);

  display.setCursor(0, 65);
  display.print("Board: ESPWatch-LoRa E22");

  display.setCursor(0, 80);
  display.print("Radio: SX1268 (433MHz)");

  display.setCursor(0, 95);
  display.print("Sensors: BMP280, LIS2DH12");

  display.setCursor(0, 110);
  display.print("Fuel: CW2015");

  display.setCursor(0, 125);
  display.print("RTC: BM8563");

  display.setCursor(0, 145);
  unsigned long uptime_s = millis() / 1000;
  unsigned long days = uptime_s / 86400;
  unsigned long hours = (uptime_s % 86400) / 3600;
  unsigned long mins = (uptime_s % 3600) / 60;
  display.setCursor(0, 165);
  snprintf(buf, sizeof(buf), "Uptime: %lud %02lu:%02lu", days, hours, mins);
  display.print(buf);

  display.setCursor(0, 185);
  snprintf(buf, sizeof(buf), "Chip: ESP32 @%luMHz", ESP.getCpuFreqMHz());
  display.print(buf);

  display.setCursor(0, 200);
  snprintf(buf, sizeof(buf), "Flash: %luKB", ESP.getFlashChipSize() / 1024);
  display.print(buf);

  display.setCursor(0, 260);
  display.print("G0: Advert  G45: Next");
  display.endFrame();
}

void draw_current_page() {
  switch (current_page) {
    case PAGE_STATUS:  draw_page_status(); break;
    case PAGE_SENSORS: draw_page_sensors(); break;
    case PAGE_ACCEL:   draw_page_accel(); break;
    case PAGE_INFO:    draw_page_info(); break;
    default:           draw_page_status(); break;
  }
}

#endif  // CUSTOM_BOARD

// ===========================================================================
// Fallback simple status screen (non-CUSTOM_BOARD)
// ===========================================================================
void draw_status_screen() {
#ifdef DISPLAY_CLASS
#ifdef CUSTOM_BOARD
  draw_current_page();
#else
  char buf[128];
  display.startFrame(DisplayDriver::DARK);
  display.setColor(DisplayDriver::LIGHT);
  display.setTextSize(1);

  display.setCursor(0, 5);
  snprintf(buf, sizeof(buf), "Node: %s", the_mesh.getNodeName());
  display.print(buf);

  display.setCursor(0, 20);
  snprintf(buf, sizeof(buf), "Freq: %.3f MHz", (float)LORA_FREQ / 1000000.0f);
  display.print(buf);

  display.setCursor(0, 35);
  snprintf(buf, sizeof(buf), "SF%u  BW%.1f  CR%u",
    (unsigned)LORA_SF, (float)LORA_BW / 1000.0f, (unsigned)LORA_CR);
  display.print(buf);

  display.setCursor(0, 50);
  snprintf(buf, sizeof(buf), "TX: %u dBm", (unsigned)LORA_TX_POWER);
  display.print(buf);

  display.setCursor(0, 90);
  snprintf(buf, sizeof(buf), "VBat: %u mV", board.getBattMilliVolts());
  display.print(buf);

  display.setCursor(0, 110);
  snprintf(buf, sizeof(buf), "Uptime: %lu s", millis() / 1000);
  display.print(buf);

  display.setCursor(0, 260);
  display.print("G0: Send advert");

  display.endFrame();
#endif  // CUSTOM_BOARD
#endif  // DISPLAY_CLASS
}

// ===========================================================================
// Setup & Loop
// ===========================================================================
void setup() {
  Serial.begin(115200);
  delay(300);  // extra time for serial monitor to attach
  Serial.println("\n=== MeshCore Boot [step 1/8: Serial OK] ===");
  Serial.printf("Chip: ESP32-S3, CPU Freq: %lu MHz\n", ESP.getCpuFreqMHz());

  Serial.println("[step 2/8] board.begin() ...");
  Serial.flush();
  board.begin();
  Serial.println("[step 2/8] board.begin() DONE");
  Serial.flush();

  Serial.println("[step 3/8] draw_startup_screen() ...");
  Serial.flush();
  draw_startup_screen();
  Serial.println("[step 3/8] draw_startup_screen() DONE");
  Serial.flush();

  Serial.println("[step 4/8] radio_init() ...");
  Serial.flush();
  if (!radio_init()) {
    Serial.println("[FATAL] Radio init failed! HALTED.");
    Serial.flush();
    halt();
  }
  Serial.println("[step 4/8] radio_init() DONE");

  Serial.println("[step 5/8] RNG, SPIFFS, Store, Mesh ...");
  Serial.flush();
  fast_rng.begin(radio_get_rng_seed());
  Serial.println("  RNG OK");

#if defined(ESP32)
  SPIFFS.begin(true);
  Serial.println("  SPIFFS OK");
#endif

  store.begin();
  Serial.println("  Store OK");

  the_mesh.begin(false);
  Serial.println("  Mesh OK");

  Serial.println("[step 6/8] Serial/BLE interface ...");
  Serial.flush();
#ifdef BLE_PIN_CODE
  serial_interface.begin(the_mesh.getNodeName(), the_mesh.getBLEPin());
  Serial.printf("  BLE OK, pin: %lu\n", the_mesh.getBLEPin());
#else
  serial_interface.begin(Serial);
  Serial.println("  Serial interface OK");
#endif
  the_mesh.startInterface(serial_interface);

  Serial.println("[step 7/8] sensors.begin() ...");
  Serial.flush();
  sensors.begin();
  Serial.println("[step 7/8] sensors.begin() DONE");

  Serial.println("[step 8/8] Boot complete");
  Serial.println("============================");
  Serial.flush();

  next_refresh = millis() + 2000;
}

void loop() {
  the_mesh.loop();
  sensors.loop();
  rtc_clock.tick();

  unsigned long now = millis();

#ifdef DISPLAY_CLASS
  // --- G0 button (PIN_USER_BTN=0): send advert, short beep ---
  int btn_event = user_btn.check();
  if (btn_event == BUTTON_EVENT_CLICK) {
    Serial.println("[UI] G0 pressed, sending advert");
    the_mesh.advert();
    board.beep(100, 2000);

#ifdef CUSTOM_BOARD
    display.startFrame(DisplayDriver::DARK);
    display.setColor(DisplayDriver::LIGHT);
    display.setTextSize(2);
    display.drawTextCentered(120, 130, "Advert sent!");
    display.endFrame();
    delay(1000);
    next_refresh = now;
#endif
  }

  // --- G45 button (PIN_BTN_2=45): cycle display page, long beep ---
#ifdef CUSTOM_BOARD
  int btn2_event = user_btn2.check();
  if (btn2_event == BUTTON_EVENT_CLICK) {
    Serial.println("[UI] G45 pressed, cycling page");
    board.beep(150, 1500);
    current_page = (current_page + 1) % NUM_PAGES;
    draw_current_page();
    next_refresh = now + 3000;
  }
#endif

  // --- Periodic screen refresh ---
  if (now >= next_refresh) {
    draw_status_screen();
    next_refresh = now + 3000;
  }
#endif  // DISPLAY_CLASS
}