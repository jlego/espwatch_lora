#pragma once

#include <RadioLib.h>

#define SX126X_IRQ_HEADER_VALID                     0b0000010000
#define SX126X_IRQ_PREAMBLE_DETECTED           0x04

class CustomSX1268 : public SX1268 {
  public:
    CustomSX1268(Module *mod) : SX1268(mod) { }

  #ifdef RP2040_PLATFORM
    bool std_init(SPIClassRP2040* spi = NULL)
  #else
    bool std_init(SPIClass* spi = NULL)
  #endif
    {
  #ifdef SX126X_DIO3_TCXO_VOLTAGE
      float tcxo = SX126X_DIO3_TCXO_VOLTAGE;
  #else
      float tcxo = 1.6f;
  #endif

  #ifdef LORA_CR
      uint8_t cr = LORA_CR;
  #else
      uint8_t cr = 5;
  #endif

  #if defined(P_LORA_SCLK)
    #ifdef NRF52_PLATFORM
      if (spi) { spi->setPins(P_LORA_MISO, P_LORA_SCLK, P_LORA_MOSI); spi->begin(); }
    #elif defined(RP2040_PLATFORM)
      if (spi) {
        spi->setMISO(P_LORA_MISO);
        spi->setSCK(P_LORA_SCLK);
        spi->setMOSI(P_LORA_MOSI);
        spi->begin();
      }
    #else
      if (spi) spi->begin(P_LORA_SCLK, P_LORA_MISO, P_LORA_MOSI);
    #endif
  #endif

      int status = begin(LORA_FREQ, LORA_BW, LORA_SF, cr, 0x3444, LORA_TX_POWER, 16, tcxo);
      if (status == RADIOLIB_ERR_SPI_CMD_FAILED || status == RADIOLIB_ERR_SPI_CMD_INVALID) {
        tcxo = 0.0f;
        status = begin(LORA_FREQ, LORA_BW, LORA_SF, cr, 0x3444, LORA_TX_POWER, 16, tcxo);
      }
      if (status != RADIOLIB_ERR_NONE) {
        Serial.print("ERROR: radio init failed: ");
        Serial.println(status);
        return false;
      }

      setCRC(1);

  #ifdef SX126X_CURRENT_LIMIT
      setCurrentLimit(SX126X_CURRENT_LIMIT);
  #endif
  #if defined(SX126X_DIO2_AS_RF_SWITCH)
      setDio2AsRfSwitch(SX126X_DIO2_AS_RF_SWITCH);
      Serial.printf("[LoRa] DIO2 configured as RF switch\n");
  #elif defined(P_LORA_RXEN) && defined(P_LORA_TXEN)
      // Configure external RF switch pins
      pinMode(P_LORA_RXEN, OUTPUT);
      pinMode(P_LORA_TXEN, OUTPUT);
      digitalWrite(P_LORA_RXEN, HIGH);  // Default to RX mode
      digitalWrite(P_LORA_TXEN, LOW);
      Serial.printf("[LoRa] External RF switch configured: RXEN=%d (HIGH), TXEN=%d (LOW)\n", P_LORA_RXEN, P_LORA_TXEN);
  #endif

      Serial.printf("[LoRa] PA config: paDutyCycle=4, hpMax=7, deviceSel=0 (SX1268 HP)\n");
      Serial.printf("[LoRa] Init OK: freq=%.3f, bw=%.1f, sf=%d, cr=%d, power=%d, tcxo=%.1f\n",
                    LORA_FREQ, LORA_BW, LORA_SF, cr, LORA_TX_POWER, tcxo);

      return true;
    }

    bool isReceiving() {
      uint16_t irq = getIrqFlags();
      bool detected = (irq & SX126X_IRQ_HEADER_VALID) || (irq & SX126X_IRQ_PREAMBLE_DETECTED);
      return detected;
    }
};