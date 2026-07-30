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

static
#if defined(ESP8266) || defined(ESP32)
  ICACHE_RAM_ATTR
#endif
void setFlag(void) {
  state |= STATE_INT_READY;
}

void RadioLibWrapper::begin() {
#if P_LORA_DIO_1 != -1
  _radio->setPacketReceivedAction(setFlag);
#endif
  state = STATE_IDLE;

  if (_board->getStartupReason() == BD_STARTUP_RX_PACKET) {
    setFlag();
  }

  _noise_floor = 0;
  _threshold = 0;

  _num_floor_samples = 0;
  _floor_sample_sum = 0;
}

void RadioLibWrapper::idle() {
  _radio->standby();
  state = STATE_IDLE;
}

void RadioLibWrapper::triggerNoiseFloorCalibrate(int threshold) {
  _threshold = threshold;
  if (_num_floor_samples >= NUM_NOISE_FLOOR_SAMPLES) {
    _num_floor_samples = 0;
    _floor_sample_sum = 0;
  }
}

void RadioLibWrapper::resetAGC() {
  if ((state & STATE_INT_READY) != 0 || isReceivingPacket()) return;

  state = STATE_IDLE;
}

void RadioLibWrapper::loop() {
#if P_LORA_DIO_1 == -1
  if (state == STATE_TX_WAIT) {
    if (digitalRead(P_LORA_BUSY) == HIGH) {
      return;
    }
    state |= STATE_INT_READY;
  }
  if (state == STATE_RX) {
    int pktLen = _radio->getPacketLength();
    if (pktLen > 0) {
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
  }
}

void RadioLibWrapper::startRecv() {
#if defined(P_LORA_RXEN) && defined(P_LORA_TXEN)
  digitalWrite(P_LORA_RXEN, HIGH);
  digitalWrite(P_LORA_TXEN, LOW);
#endif
  int err = _radio->startReceive();
  if (err == RADIOLIB_ERR_NONE) {
    state = STATE_RX;
  } else {
    MESH_DEBUG_PRINTLN("RadioLibWrapper: error: startReceive(%d)", err);
  }
}

bool RadioLibWrapper::isInRecvMode() const {
  return (state & ~STATE_INT_READY) == STATE_RX;
}

int RadioLibWrapper::recvRaw(uint8_t* bytes, int sz) {
  int len = 0;
  if (state & STATE_INT_READY) {
    len = _radio->getPacketLength();
    if (len > 0) {
      if (len > sz) { len = sz; }
      int err = _radio->readData(bytes, len);
      if (err != RADIOLIB_ERR_NONE) {
        MESH_DEBUG_PRINTLN("RadioLibWrapper: error: readData(%d)", err);
        len = 0;
      } else {
        n_recv++;
      }
    }
    state = STATE_IDLE;
  }

  if (state != STATE_RX) {
#if defined(P_LORA_RXEN) && defined(P_LORA_TXEN)
    digitalWrite(P_LORA_RXEN, HIGH);
    digitalWrite(P_LORA_TXEN, LOW);
#endif
    int err = _radio->startReceive();
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
#if defined(P_LORA_RXEN) && defined(P_LORA_TXEN)
  digitalWrite(P_LORA_RXEN, LOW);
  digitalWrite(P_LORA_TXEN, HIGH);
#endif
  _board->onBeforeTransmit();
  int err = _radio->startTransmit((uint8_t *) bytes, len);
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
  if (complete) {
    state = STATE_IDLE;
    n_sent++;
    return true;
  }
  return false;
}

void RadioLibWrapper::onSendFinished() {
  _radio->finishTransmit();
  _board->onAfterTransmit();
#if defined(P_LORA_RXEN) && defined(P_LORA_TXEN)
  digitalWrite(P_LORA_RXEN, HIGH);
  digitalWrite(P_LORA_TXEN, LOW);
#endif
  state = STATE_IDLE;
}

bool RadioLibWrapper::isChannelActive() {
  return _threshold == 0
          ? false
          : getCurrentRSSI() > _noise_floor + _threshold;
}

float RadioLibWrapper::getLastRSSI() const {
  return _radio->getRSSI();
}
float RadioLibWrapper::getLastSNR() const {
  return _radio->getSNR();
}

static float snr_threshold[] = {
    -7.5,
    -10,
    -12.5,
    -15,
    -17.5,
    -20
};

float RadioLibWrapper::packetScoreInt(float snr, int sf, int packet_len) {
  if (sf < 7) return 0.0f;

  if (snr < snr_threshold[sf - 7]) return 0.0f;

  auto success_rate_based_on_snr = (snr - snr_threshold[sf - 7]) / 10.0;
  auto collision_penalty = 1 - (packet_len / 256.0);

  return max(0.0, min(1.0, success_rate_based_on_snr * collision_penalty));
}