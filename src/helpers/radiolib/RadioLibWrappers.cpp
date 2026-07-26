
#define RADIOLIB_STATIC_ONLY 1
#include "RadioLibWrappers.h"

#define STATE_IDLE       0
#define STATE_RX         1
#define STATE_TX_WAIT    3
#define STATE_TX_DONE    4
#define STATE_INT_READY 16

#define NUM_NOISE_FLOOR_SAMPLES  64
#define SAMPLING_THRESHOLD  14

static volatile uint8_t state = STATE_IDLE;

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
#if P_LORA_DIO_1 != -1
  _radio->setPacketReceivedAction(setFlag);  // this is also SentComplete interrupt
#endif
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
#if P_LORA_DIO_1 == -1
  if (state == STATE_TX_WAIT) {
    if (digitalRead(P_LORA_BUSY) == HIGH) {
      return;
    }
    Serial.println("[LoRa] TX_WAIT: BUSY went LOW, setting INT_READY");
    state |= STATE_INT_READY;
  }
  if (state == STATE_RX) {
    int pktLen = _radio->getPacketLength();
    static unsigned long rx_debug_last = 0;
    if (millis() - rx_debug_last > 1000) {  // 每秒采样
      int rssi = getCurrentRSSI();
      int busy = digitalRead(P_LORA_BUSY);
      Serial.printf("[LoRa] RX debug: pktLen=%d, rssi=%d, busy=%d\n", 
                    pktLen, rssi, busy);
      rx_debug_last = millis();
    }
    if (pktLen > 0) {
      Serial.printf("[LoRa] RX: Packet detected, len=%d\n", pktLen);
      state |= STATE_INT_READY;
    }
  }
#endif

  if (state == STATE_RX && _num_floor_samples < NUM_NOISE_FLOOR_SAMPLES) {
    if (!isReceivingPacket()) {
      int rssi = getCurrentRSSI();
      if (rssi < _noise_floor + SAMPLING_THRESHOLD) {
        _num_floor_samples++;
        _floor_sample_sum += rssi;
      }
    }
  } else if (_num_floor_samples >= NUM_NOISE_FLOOR_SAMPLES && _floor_sample_sum != 0) {
    _noise_floor = _floor_sample_sum / NUM_NOISE_FLOOR_SAMPLES;
    if (_noise_floor < -120) {
      _noise_floor = -120;
    }
    _floor_sample_sum = 0;

    MESH_DEBUG_PRINTLN("RadioLibWrapper: noise_floor = %d", (int)_noise_floor);
  }
}

void RadioLibWrapper::startRecv() {
  Serial.println("[LoRa] startRecv() called");
#if defined(P_LORA_RXEN) && defined(P_LORA_TXEN)
  digitalWrite(P_LORA_RXEN, HIGH);
  digitalWrite(P_LORA_TXEN, LOW);
  Serial.println("[LoRa] RX mode: RXEN=HIGH, TXEN=LOW");
#endif
  int err = _radio->startReceive();
  Serial.printf("[LoRa] startReceive returned: %d\n", err);
  if (err == RADIOLIB_ERR_NONE) {
    state = STATE_RX;
    Serial.println("[LoRa] State set to STATE_RX");
  } else {
    MESH_DEBUG_PRINTLN("RadioLibWrapper: error: startReceive(%d)", err);
  }
}

bool RadioLibWrapper::isInRecvMode() const {
  return (state & ~STATE_INT_READY) == STATE_RX;
}

int RadioLibWrapper::recvRaw(uint8_t* bytes, int sz) {
  int len = 0;
  static bool recv_debug_printed = false;
  if (!recv_debug_printed) {
    Serial.printf("[LoRa] recvRaw called: state=%d\n", state);
    recv_debug_printed = true;
  }
  if (state & STATE_INT_READY) {
    len = _radio->getPacketLength();
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
    Serial.printf("[LoRa] recvRaw: state=%d, calling startReceive()\n", state);
#if defined(P_LORA_RXEN) && defined(P_LORA_TXEN)
    digitalWrite(P_LORA_RXEN, HIGH);  // RX on
    digitalWrite(P_LORA_TXEN, LOW);   // TX off
    Serial.println("[LoRa] recvRaw: RX mode: RXEN=HIGH, TXEN=LOW");
#endif
    int err = _radio->startReceive();
    Serial.printf("[LoRa] recvRaw: startReceive returned: %d\n", err);
    if (err == RADIOLIB_ERR_NONE) {
      state = STATE_RX;
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
  Serial.printf("[LoRa] startSendRaw: len=%d\n", len);
#if defined(P_LORA_RXEN) && defined(P_LORA_TXEN)
  digitalWrite(P_LORA_RXEN, LOW);
  digitalWrite(P_LORA_TXEN, HIGH);
  Serial.println("[LoRa] TX mode: TXEN=HIGH, RXEN=LOW");
#endif
  _board->onBeforeTransmit();
  int err = _radio->startTransmit((uint8_t *) bytes, len);
  Serial.printf("[LoRa] startTransmit returned: %d\n", err);
  if (err == RADIOLIB_ERR_NONE) {
    state = STATE_TX_WAIT;
    return true;
  }
  MESH_DEBUG_PRINTLN("RadioLibWrapper: error: startTransmit(%d)", err);
  idle();
  return false;
}

bool RadioLibWrapper::isSendComplete() {
  bool complete = (state & STATE_INT_READY) != 0;
  Serial.printf("[LoRa] isSendComplete: state=%d, complete=%d\n", state, complete);
  if (complete) {
    state = STATE_IDLE;
    n_sent++;
    return true;
  }
  return false;
}

void RadioLibWrapper::onSendFinished() {
  Serial.println("[LoRa] onSendFinished called");
  _radio->finishTransmit();
  _board->onAfterTransmit();
#if defined(P_LORA_RXEN) && defined(P_LORA_TXEN)
  digitalWrite(P_LORA_RXEN, HIGH);
  digitalWrite(P_LORA_TXEN, LOW);
  Serial.println("[LoRa] TX done, back to RX mode: RXEN=HIGH, TXEN=LOW");
#endif
  state = STATE_IDLE;
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