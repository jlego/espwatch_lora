#pragma once

#include <helpers/ui/DisplayDriver.h>
#include <Arduino.h>
#include <SPI.h>

class ESPWatchTFTDisplay : public DisplayDriver {
  bool _isOn;
  uint16_t _color;
  uint16_t _bkg;
  int _cursor_x, _cursor_y;
  int _text_size;

  // 5x7 font for ASCII 32-126
  static const uint8_t font5x7[][5];
  static const int CHAR_WIDTH = 5;
  static const int CHAR_HEIGHT = 7;
  static const int CHAR_SPACING = 1;  // 1 pixel between characters
  static const int LINE_SPACING = 1;  // 1 pixel between lines

  void _reinitSpi();
  void drawPixel(int x, int y, uint16_t color);
  void drawChar5x7(char c, int x, int y, uint16_t color, uint16_t bkg, int size);
  int charWidthPx() const { return (CHAR_WIDTH + CHAR_SPACING) * _text_size; }
  int charHeightPx() const { return (CHAR_HEIGHT + LINE_SPACING) * _text_size; }

  void drawTestPattern();

public:
  ESPWatchTFTDisplay() : DisplayDriver(240, 285), _isOn(false), _color(0xFFFF), _bkg(0x0000), _cursor_x(0), _cursor_y(0), _text_size(1) {}
  bool begin();

  bool isOn() override { return _isOn; }
  void turnOn() override;
  void turnOff() override;
  void clear() override;
  void startFrame(Color bkg = DARK) override;
  void setTextSize(int sz) override;
  void setColor(Color c) override;
  void setCursor(int x, int y) override;
  void print(const char* str) override;
  void fillRect(int x, int y, int w, int h) override;
  void drawRect(int x, int y, int w, int h) override;
  void drawXbm(int x, int y, const uint8_t* bits, int w, int h) override;
  uint16_t getTextWidth(const char* str) override;
  void endFrame() override;
};