
#define RADIOLIB_STATIC_ONLY 1
#include "RadioLibWrappers.h"
#ifdef RADIOLIB_CUSTOM_SX1268
#include "CustomSX1268.h"
#endif
#include <SPI.h>

#define STATE_IDLE       0
#define STATE_RX         1
#define STATE_TX_WAIT    3
#define STATE_TX_DONE    4
#define STATE_INT_READY 16

#define NUM_NOISE_FLOOR_SAMPLES  64
#define SAMPLING_THRESHOLD  14

static volatile uint8_t state = STATE_IDLE;

// Raw SPI read of SX126x chip status (works around RadioLib getStatus() bug)
// RadioLib's SX126x::getStatus() calls SPIreadStream with numBytes=0 which
// never copies the status byte due to statusPos=1 offset. This function
// uses the same raw SPI approach as the init probe in target.cpp.
static uint8_t readSX126xStatusRaw() {
  SPI.beginTransaction(SPISettings(2000000, MSBFIRST, SPI_MODE0));
  digitalWrite(P_LORA_NSS, LOW);
  SPI.transfer(0xC0);                    // GET_STATUS command
  uint8_t status = SPI.transfer(0x00);   // read status byte
  digitalWrite(P_LORA_NSS, HIGH);
  SPI.endTransaction();
  return status;
}

// this function is called when a complete packet
// is transmitted by the module
static 
#if defined(ESP8266) || defined(ESP32)
  ICACHE_RAM_ATTR
#endif
void setFlag(void) {
  // we sent a packet, set the flag
  state |= STATE_INT_READY;
}

void RadioLibWrapper::begin() {
  _radio->setPacketReceivedAction(setFlag);  // this is also SentComplete interrupt
  state = STATE_IDLE;

  if (_board->getStartupReason() == BD_STARTUP_RX_PACKET) {  // received a LoRa packet (while in deep sleep)
    setFlag(); // LoRa packet is already received
  }

  _noise_floor = 0;
  _threshold = 0;

  // start average out some samples
  _num_floor_samples = 0;
  _floor_sample_sum = 0;
}

void RadioLibWrapper::idle() {
  _radio->standby();
  state = STATE_IDLE;   // need another startReceive()
}

void RadioLibWrapper::triggerNoiseFloorCalibrate(int threshold) {
  _threshold = threshold;
  if (_num_floor_samples >= NUM_NOISE_FLOOR_SAMPLES) {  // ignore trigger if currently sampling
    _num_floor_samples = 0;
    _floor_sample_sum = 0;
  }
}

void RadioLibWrapper::resetAGC() {
  // make sure we're not mid-receive of packet!
  if ((state & STATE_INT_READY) != 0 || isReceivingPacket()) return;

  // NOTE: according to higher powers, just issuing RadioLib's startReceive() will reset the AGC.
  //      revisit this if a better impl is discovered.
  state = STATE_IDLE;   // trigger a startReceive()
}

void RadioLibWrapper::loop() {
  static unsigned long debugLog = 0;
  if (millis() - debugLog > 10000) {
    Serial.printf("[RX] loop() called, state=%d, DIO1=%d\n", state, P_LORA_DIO_1);
#if defined(SX126X_RXEN) && defined(SX126X_TXEN)
    Serial.printf("[RX] RF switch pins: RXEN=%d, TXEN=%d\n", digitalRead(SX126X_RXEN), digitalRead(SX126X_TXEN));
#endif
    debugLog = millis();
  }
#if P_LORA_DIO_1 == -1
  // No DIO1 pin - poll for packet reception in loop()
  if (state == STATE_RX) {
    int pktLen = _radio->getPacketLength();
    static unsigned long pollLog = 0;
    if (pktLen > 0) {
      // Packet received
      Serial.printf("[RX] Packet detected! len=%d\n", pktLen);
      state |= STATE_INT_READY;
    } else if (millis() - pollLog > 3000) {
      // Read SX1268 IRQ flags and raw status
      uint16_t irq = ((SX126x *)_radio)->getIrqFlags();
      uint8_t status = readSX126xStatusRaw();
      uint8_t chip_mode = (status >> 4) & 0x03;
      float rssi = ((SX126x *)_radio)->getRSSI();
      Serial.printf("[RX] Polling... pktLen=%d, IRQ=0x%04X, chip_mode=%d, RSSI=%.1f\n", pktLen, irq, chip_mode, rssi);
      pollLog = millis();
    }
  } else {
    // Debug: log state if not in RX
    static unsigned long lastLog = 0;
    if (millis() - lastLog > 5000) {
      Serial.printf("[RX] Not in RX mode, state=%d\n", state);
      lastLog = millis();
    }
  }
#endif
  if (state == STATE_RX && _num_floor_samples < NUM_NOISE_FLOOR_SAMPLES) {
    if (!isReceivingPacket()) {
      int rssi = getCurrentRSSI();
      if (rssi < _noise_floor + SAMPLING_THRESHOLD) {  // only consider samples below current floor + sampling THRESHOLD
        _num_floor_samples++;
        _floor_sample_sum += rssi;
      }
    }
  } else if (_num_floor_samples >= NUM_NOISE_FLOOR_SAMPLES && _floor_sample_sum != 0) {
    _noise_floor = _floor_sample_sum / NUM_NOISE_FLOOR_SAMPLES;
    if (_noise_floor < -120) {
      _noise_floor = -120;    // clamp to lower bound of -120dBi
    }
    _floor_sample_sum = 0;

    MESH_DEBUG_PRINTLN("RadioLibWrapper: noise_floor = %d", (int)_noise_floor);
  }
}

void RadioLibWrapper::startRecv() {
#if defined(SX126X_RXEN) && defined(SX126X_TXEN)
  Serial.printf("[RX] startRecv: RXEN=%d, TXEN=%d\n", digitalRead(SX126X_RXEN), digitalRead(SX126X_TXEN));
#endif
  int err = _radio->startReceive();
  Serial.printf("[RX] startReceive returned: %d\n", err);
  if (err == RADIOLIB_ERR_NONE) {
    state = STATE_RX;
    delay(5);
    uint8_t status = readSX126xStatusRaw();
    uint8_t chip_mode = (status >> 4) & 0x03;
    Serial.printf("[RX] After startRecv: raw_status=0x%02X, chip_mode=%d, RXEN=%d, TXEN=%d\n",
                  status, chip_mode,
                  digitalRead(SX126X_RXEN), digitalRead(SX126X_TXEN));
  } else {
    MESH_DEBUG_PRINTLN("RadioLibWrapper: error: startReceive(%d)", err);
  }
}

bool RadioLibWrapper::isInRecvMode() const {
  return (state & ~STATE_INT_READY) == STATE_RX;
}

int RadioLibWrapper::recvRaw(uint8_t* bytes, int sz) {
  int len = 0;
#if P_LORA_DIO_1 == -1
  // No DIO1 pin - poll for packet reception
  if (state == STATE_RX) {
    int pktLen = _radio->getPacketLength();
    if (pktLen > 0) {
      // Packet received
      state |= STATE_INT_READY;
    }
  }
#endif
  if (state & STATE_INT_READY) {
    len = _radio->getPacketLength();
    Serial.printf("[RX] STATE_INT_READY set, pktLen=%d\n", len);
    if (len > 0) {
      if (len > sz) { len = sz; }
      int err = _radio->readData(bytes, len);
      if (err != RADIOLIB_ERR_NONE) {
        MESH_DEBUG_PRINTLN("RadioLibWrapper: error: readData(%d)", err);
        len = 0;
      } else {
      //  Serial.print("  readData() -> "); Serial.println(len);
        n_recv++;
      }
    }
    state = STATE_IDLE;   // need another startReceive()
  }

  if (state != STATE_RX) {
    Serial.printf("[RX] Calling startReceive() from recvRaw, state=%d\n", state);
    int err = _radio->startReceive();
    Serial.printf("[RX] startReceive returned: %d\n", err);
    if (err == RADIOLIB_ERR_NONE) {
      state = STATE_RX;
      // Read SX1268 status after startReceive (using raw SPI to avoid RadioLib getStatus() bug)
      delay(10);
      uint16_t irq = ((SX126x *)_radio)->getIrqFlags();
      uint8_t status = readSX126xStatusRaw();
      uint8_t chip_mode = (status >> 4) & 0x03;
      const char* mode_str = "???";
      switch (chip_mode) {
        case 0x00: mode_str = "STBY_RC"; break;
        case 0x01: mode_str = "STBY_XOSC"; break;
        case 0x02: mode_str = "FS"; break;
        case 0x03: mode_str = "RX/TX"; break;
      }
      Serial.printf("[RX] After startReceive: IRQ=0x%04X, STATUS=0x%02X (chip_mode=%s)\n", irq, status, mode_str);
    } else {
      MESH_DEBUG_PRINTLN("RadioLibWrapper: error: startReceive(%d)", err);
    }
  }
  return len;
}

uint32_t RadioLibWrapper::getEstAirtimeFor(int len_bytes) {
  return _radio->getTimeOnAir(len_bytes) / 1000;
}

bool RadioLibWrapper::startSendRaw(const uint8_t* bytes, int len) {
  _board->onBeforeTransmit();
#if defined(SX126X_RXEN) && defined(SX126X_TXEN)
  // Manually set RF switch to TX mode
  digitalWrite(SX126X_RXEN, LOW);
  digitalWrite(SX126X_TXEN, HIGH);
  Serial.printf("[TX] RF switch: RXEN=0, TXEN=1\n");
  delay(1);
#endif
  // Ensure chip is in standby before transmit
  Serial.println("[TX] Before standby()");
  _radio->standby();
  delay(5);
  Serial.println("[TX] Calling startTransmit()");
  int err = _radio->startTransmit((uint8_t *) bytes, len);
  Serial.printf("[TX] startTransmit returned: %d\n", err);
  if (err == RADIOLIB_ERR_NONE) {
#if P_LORA_DIO_1 == -1
    // No DIO1 pin - poll for transmit completion
    Serial.println("[TX] Polling for TX completion (no DIO1)");
    err = _radio->finishTransmit();
    Serial.printf("[TX] finishTransmit returned: %d\n", err);
    if (err == RADIOLIB_ERR_NONE) {
      n_sent++;
      _board->onAfterTransmit();
      state = STATE_INT_READY;  // Signal completion to Dispatcher
      return true;
    } else {
      MESH_DEBUG_PRINTLN("RadioLibWrapper: error: finishTransmit(%d)", err);
      idle();
      return false;
    }
#else
    state = STATE_TX_WAIT;
    return true;
#endif
  }
  MESH_DEBUG_PRINTLN("RadioLibWrapper: error: startTransmit(%d)", err);
  idle();   // trigger another startRecv()
  return false;
}

bool RadioLibWrapper::isSendComplete() {
  if (state & STATE_INT_READY) {
    Serial.println("[TX] isSendComplete() -> true");
    state = STATE_IDLE;
    n_sent++;
    return true;
  }
  return false;
}

void RadioLibWrapper::onSendFinished() {
#if defined(SX126X_RXEN) && defined(SX126X_TXEN)
  // Manually set RF switch back to RX mode
  digitalWrite(SX126X_RXEN, HIGH);
  digitalWrite(SX126X_TXEN, LOW);
  Serial.printf("[TX] RF switch after TX: RXEN=1, TXEN=0\n");
  delay(1);
#endif
  _radio->finishTransmit();
  _board->onAfterTransmit();
  state = STATE_IDLE;
  Serial.println("[TX] onSendFinished() done, state=IDLE");
}

bool RadioLibWrapper::isChannelActive() {
  return _threshold == 0 
          ? false    // interference check is disabled
          : getCurrentRSSI() > _noise_floor + _threshold;
}

float RadioLibWrapper::getLastRSSI() const {
  return _radio->getRSSI();
}
float RadioLibWrapper::getLastSNR() const {
  return _radio->getSNR();
}

// Approximate SNR threshold per SF for successful reception (based on Semtech datasheets)
static float snr_threshold[] = {
    -7.5,  // SF7 needs at least -7.5 dB SNR
    -10,   // SF8 needs at least -10 dB SNR
    -12.5, // SF9 needs at least -12.5 dB SNR
    -15,  // SF10 needs at least -15 dB SNR
    -17.5,// SF11 needs at least -17.5 dB SNR
    -20   // SF12 needs at least -20 dB SNR
};
  
float RadioLibWrapper::packetScoreInt(float snr, int sf, int packet_len) {
  if (sf < 7) return 0.0f;
  
  if (snr < snr_threshold[sf - 7]) return 0.0f;    // Below threshold, no chance of success

  auto success_rate_based_on_snr = (snr - snr_threshold[sf - 7]) / 10.0;
  auto collision_penalty = 1 - (packet_len / 256.0);   // Assuming max packet of 256 bytes

  return max(0.0, min(1.0, success_rate_based_on_snr * collision_penalty));
}