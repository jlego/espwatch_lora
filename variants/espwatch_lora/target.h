#pragma once

#define RADIOLIB_STATIC_ONLY 1
#include <RadioLib.h>
#include <helpers/radiolib/RadioLibWrappers.h>
#ifdef SX1262_RADIO
  #include <helpers/radiolib/CustomSX1262Wrapper.h>
#elif defined(SX1268_RADIO)
  #include <helpers/radiolib/CustomSX1268Wrapper.h>
#else
  #error "Radio type not defined! Use SX1262_RADIO or SX1268_RADIO"
#endif

#ifdef CUSTOM_BOARD
  #include "CustomBoard.h"
  typedef CustomBoard BoardType;
#else
  #include <ESPWatchLoraBoard.h>
  typedef M5CardputerBoard BoardType;
#endif

#include <helpers/AutoDiscoverRTCClock.h>
#include <helpers/ESP32Board.h>
#include <helpers/SensorManager.h>

#ifdef CUSTOM_BOARD
/**
 * CustomSensorManager - no sensors (BMP280 and LIS2DH12 disabled for power saving)
 */
class CustomSensorManager : public SensorManager {
public:
  bool begin() override;
  bool querySensors(uint8_t requester_permissions, CayenneLPP& telemetry) override;
  void loop() override;

  float getTemperature() const { return 25.0f; }
  float getPressure() const { return 1013.25f; }
  float getAltitudeMeters() const { return 0.0f; }
  float getAccelX() const { return 0.0f; }
  float getAccelY() const { return 0.0f; }
  float getAccelZ() const { return 1.0f; }
};
#else  // CUSTOM_BOARD not defined -> fallback to CardputerSensorManager
#ifdef HAS_GPS
  #include <helpers/sensors/MicroNMEALocationProvider.h>
class CardputerSensorManager : public SensorManager {
  bool gps_active = false;
  LocationProvider* _location;
  void* _node_prefs;

  void start_gps();
  void stop_gps();
public:
  CardputerSensorManager(LocationProvider &location): _location(&location), _node_prefs(nullptr) { }
  void setNodePrefs(void* prefs) { _node_prefs = prefs; }
  bool begin() override;
  bool querySensors(uint8_t requester_permissions, CayenneLPP& telemetry) override;
  void loop() override;
  int getNumSettings() const override;
  const char* getSettingName(int i) const override;
  const char* getSettingValue(int i) const override;
  bool setSettingValue(const char* name, const char* value) override;
};
#endif
#endif

#ifdef DISPLAY_CLASS
  #if defined(CUSTOM_BOARD)
    #include "ESPWatchTFTDisplay.h"
  #else
    #include "ESPWatchTFTDisplay.h"
    #include <helpers/ui/M5CardputerDisplay.h>
  #endif
  #include <helpers/ui/MomentaryButton.h>
#endif

extern BoardType board;
extern WRAPPER_CLASS radio_driver;
extern ESP32RTCClock fallback_clock;
extern AutoDiscoverRTCClock rtc_clock;

#ifdef CUSTOM_BOARD
  extern CustomSensorManager sensors;
#elif defined(HAS_GPS)
  extern CardputerSensorManager sensors;
#else
  extern SensorManager sensors;
#endif

#ifdef DISPLAY_CLASS
  extern DISPLAY_CLASS display;
  extern MomentaryButton user_btn;
  extern MomentaryButton user_btn2;
#endif

bool radio_init();
uint32_t radio_get_rng_seed();
void radio_set_params(float freq, float bw, uint8_t sf, uint8_t cr);
void radio_set_tx_power(uint8_t dbm);
mesh::LocalIdentity radio_new_identity();