// Include lcd.h FIRST (it defines color macros like RED, BLUE, etc.)
#include "lcd.h"

// Undefine lcd.h color macros to avoid clashing with DisplayDriver::Color enum
#undef WHITE
#undef BLACK
#undef BLUE
#undef RED
#undef GREEN
#undef YELLOW
#undef BROWN
#undef GRAY
#undef GRAY0
#undef GRAY1
#undef GRAY2

// Now include DisplayDriver interface (after clearing conflicting macros)
#include "ESPWatchTFTDisplay.h"

// Simple 5x7 bitmap font for ASCII characters 32-126
// Each entry has 5 bytes representing the 5 columns
// MSB of each byte is the top row of that column
const uint8_t ESPWatchTFTDisplay::font5x7[][5] = {
  {0x00, 0x00, 0x00, 0x00, 0x00}, // 32 space
  {0x00, 0x00, 0x5F, 0x00, 0x00}, // 33 !
  {0x00, 0x07, 0x00, 0x07, 0x00}, // 34 "
  {0x14, 0x7F, 0x14, 0x7F, 0x14}, // 35 #
  {0x24, 0x2A, 0x7F, 0x2A, 0x12}, // 36 $
  {0x23, 0x13, 0x08, 0x64, 0x62}, // 37 %
  {0x36, 0x49, 0x55, 0x22, 0x50}, // 38 &
  {0x00, 0x05, 0x03, 0x00, 0x00}, // 39 '
  {0x00, 0x1C, 0x22, 0x41, 0x00}, // 40 (
  {0x00, 0x41, 0x22, 0x1C, 0x00}, // 41 )
  {0x14, 0x08, 0x3E, 0x08, 0x14}, // 42 *
  {0x08, 0x08, 0x3E, 0x08, 0x08}, // 43 +
  {0x00, 0x00, 0xA0, 0x60, 0x00}, // 44 ,
  {0x08, 0x08, 0x08, 0x08, 0x08}, // 45 -
  {0x00, 0x60, 0x60, 0x00, 0x00}, // 46 .
  {0x20, 0x10, 0x08, 0x04, 0x02}, // 47 /
  {0x3E, 0x51, 0x49, 0x45, 0x3E}, // 48 0
  {0x00, 0x42, 0x7F, 0x40, 0x00}, // 49 1
  {0x42, 0x61, 0x51, 0x49, 0x46}, // 50 2
  {0x21, 0x41, 0x45, 0x4B, 0x31}, // 51 3
  {0x18, 0x14, 0x12, 0x7F, 0x10}, // 52 4
  {0x27, 0x45, 0x45, 0x45, 0x39}, // 53 5
  {0x3C, 0x4A, 0x49, 0x49, 0x30}, // 54 6
  {0x01, 0x71, 0x09, 0x05, 0x03}, // 55 7
  {0x36, 0x49, 0x49, 0x49, 0x36}, // 56 8
  {0x06, 0x49, 0x49, 0x29, 0x1E}, // 57 9
  {0x00, 0x36, 0x36, 0x00, 0x00}, // 58 :
  {0x00, 0x56, 0x36, 0x00, 0x00}, // 59 ;
  {0x08, 0x14, 0x22, 0x41, 0x00}, // 60 <
  {0x14, 0x14, 0x14, 0x14, 0x14}, // 61 =
  {0x00, 0x41, 0x22, 0x14, 0x08}, // 62 >
  {0x02, 0x01, 0x51, 0x09, 0x06}, // 63 ?
  {0x32, 0x49, 0x59, 0x51, 0x3E}, // 64 @
  {0x7C, 0x12, 0x11, 0x12, 0x7C}, // 65 A
  {0x7F, 0x49, 0x49, 0x49, 0x36}, // 66 B
  {0x3E, 0x41, 0x41, 0x41, 0x22}, // 67 C
  {0x7F, 0x41, 0x41, 0x22, 0x1C}, // 68 D
  {0x7F, 0x49, 0x49, 0x49, 0x41}, // 69 E
  {0x7F, 0x09, 0x09, 0x09, 0x01}, // 70 F
  {0x3E, 0x41, 0x49, 0x49, 0x7A}, // 71 G
  {0x7F, 0x08, 0x08, 0x08, 0x7F}, // 72 H
  {0x00, 0x41, 0x7F, 0x41, 0x00}, // 73 I
  {0x20, 0x40, 0x41, 0x3F, 0x01}, // 74 J
  {0x7F, 0x08, 0x14, 0x22, 0x41}, // 75 K
  {0x7F, 0x40, 0x40, 0x40, 0x40}, // 76 L
  {0x7F, 0x02, 0x0C, 0x02, 0x7F}, // 77 M
  {0x7F, 0x04, 0x08, 0x10, 0x7F}, // 78 N
  {0x3E, 0x41, 0x41, 0x41, 0x3E}, // 79 O
  {0x7F, 0x09, 0x09, 0x09, 0x06}, // 80 P
  {0x3E, 0x41, 0x51, 0x21, 0x5E}, // 81 Q
  {0x7F, 0x09, 0x19, 0x29, 0x46}, // 82 R
  {0x46, 0x49, 0x49, 0x49, 0x31}, // 83 S
  {0x01, 0x01, 0x7F, 0x01, 0x01}, // 84 T
  {0x3F, 0x40, 0x40, 0x40, 0x3F}, // 85 U
  {0x1F, 0x20, 0x40, 0x20, 0x1F}, // 86 V
  {0x3F, 0x40, 0x38, 0x40, 0x3F}, // 87 W
  {0x63, 0x14, 0x08, 0x14, 0x63}, // 88 X
  {0x07, 0x08, 0x70, 0x08, 0x07}, // 89 Y
  {0x61, 0x51, 0x49, 0x45, 0x43}, // 90 Z
  {0x00, 0x7F, 0x41, 0x41, 0x00}, // 91 [
  {0x55, 0x2A, 0x55, 0x2A, 0x55}, // 92 backslash (alternating)
  {0x00, 0x41, 0x41, 0x7F, 0x00}, // 93 ]
  {0x04, 0x02, 0x01, 0x02, 0x04}, // 94 ^
  {0x40, 0x40, 0x40, 0x40, 0x40}, // 95 _
  {0x00, 0x01, 0x02, 0x04, 0x00}, // 96 `
  {0x20, 0x54, 0x54, 0x54, 0x78}, // 97 a
  {0x7F, 0x48, 0x44, 0x44, 0x38}, // 98 b
  {0x38, 0x44, 0x44, 0x44, 0x20}, // 99 c
  {0x38, 0x44, 0x44, 0x48, 0x7F}, // 100 d
  {0x38, 0x54, 0x54, 0x54, 0x18}, // 101 e
  {0x08, 0x7E, 0x09, 0x01, 0x02}, // 102 f
  {0x0C, 0x52, 0x52, 0x52, 0x3E}, // 103 g
  {0x7F, 0x08, 0x04, 0x04, 0x78}, // 104 h
  {0x00, 0x44, 0x7D, 0x40, 0x00}, // 105 i
  {0x20, 0x40, 0x44, 0x3D, 0x00}, // 106 j
  {0x00, 0x7F, 0x10, 0x28, 0x44}, // 107 k
  {0x00, 0x41, 0x7F, 0x40, 0x00}, // 108 l
  {0x7C, 0x04, 0x18, 0x04, 0x78}, // 109 m
  {0x7C, 0x08, 0x04, 0x04, 0x78}, // 110 n
  {0x38, 0x44, 0x44, 0x44, 0x38}, // 111 o
  {0x7C, 0x14, 0x14, 0x14, 0x08}, // 112 p
  {0x08, 0x14, 0x14, 0x18, 0x7C}, // 113 q
  {0x7C, 0x08, 0x04, 0x04, 0x08}, // 114 r
  {0x48, 0x54, 0x54, 0x54, 0x20}, // 115 s
  {0x04, 0x3F, 0x44, 0x40, 0x20}, // 116 t
  {0x3C, 0x40, 0x40, 0x20, 0x7C}, // 117 u
  {0x1C, 0x20, 0x40, 0x20, 0x1C}, // 118 v
  {0x3C, 0x40, 0x30, 0x40, 0x3C}, // 119 w
  {0x44, 0x28, 0x10, 0x28, 0x44}, // 120 x
  {0x0C, 0x50, 0x50, 0x50, 0x3C}, // 121 y
  {0x44, 0x64, 0x54, 0x4C, 0x44}, // 122 z
  {0x08, 0x36, 0x41, 0x00, 0x00}, // 123 {
  {0x00, 0x00, 0x7F, 0x00, 0x00}, // 124 |
  {0x00, 0x41, 0x36, 0x08, 0x00}, // 125 }
  {0x10, 0x08, 0x08, 0x10, 0x08}, // 126 ~
};

void ESPWatchTFTDisplay::drawPixel(int x, int y, uint16_t color) {
  if (x < 0 || x >= 240 || y < 0 || y >= 285) return;
  LCD_SetCursor(x, y);
  Lcd_WriteData_16Bit(color);
}

// 辅助：把一个 16-bit 颜色预编码成 9-bit SPI 的 2 字节格式
static inline void encode_color_9bit(uint16_t color, uint8_t out[2]) {
  uint8_t hi = (uint8_t)((color >> 8) & 0xFF);
  uint8_t lo = (uint8_t)(color & 0xFF);
  out[0] = (uint8_t)(0x80 | ((hi >> 1) & 0x7F));
  out[1] = (uint8_t)(hi << 7) & 0xFF;
  out[2] = (uint8_t)(0x80 | ((lo >> 1) & 0x7F));
  out[3] = (uint8_t)(lo << 7) & 0xFF;
}

// 辅助：推一个已预编码的 16-bit 颜色 (4 字节 -> 实际是 2 个 9-bit 帧)
static inline void push_encoded_pixel(const uint8_t encoded[4]) {
  digitalWrite(LCD_CS, LOW);
  lcdSPI->transferBytes((uint8_t*)encoded, NULL, 2);
  digitalWrite(LCD_CS, HIGH);
  digitalWrite(LCD_CS, LOW);
  lcdSPI->transferBytes((uint8_t*)(encoded + 2), NULL, 2);
  digitalWrite(LCD_CS, HIGH);
}

void ESPWatchTFTDisplay::drawChar5x7(char c, int x, int y, uint16_t color, uint16_t bkg, int size) {
  if (c < 32 || c > 126) c = 32;
  uint8_t idx = c - 32;

  // 预编码前景色
  uint8_t enc_color[4];
  encode_color_9bit(color, enc_color);

  uint32_t pixel_count = 0;

  // 透明背景：只写前景色像素，背景像素跳过，让底下已绘制的内容透出来
  // 这样画在蓝色标题栏上的字就不会有黑色底
  for (int row = 0; row < CHAR_HEIGHT; row++) {
    for (int sy = 0; sy < size; sy++) {
      for (int col = 0; col < CHAR_WIDTH; col++) {
        bool pixel_on = font5x7[idx][col] & (1 << row);
        if (pixel_on) {
          // 这一整块 size×1 行像素都要画前景色
          LCD_SetWindows(x + col * size, y + row * size + sy,
                         x + col * size + size - 1, y + row * size + sy);
          for (int sx = 0; sx < size; sx++) {
            push_encoded_pixel(enc_color);
            pixel_count++;
            if ((pixel_count & 0x3F) == 0) yield();
          }
        }
        // pixel_off: 不写，保持底下原有颜色（透明背景）
      }
    }
  }
  // 右侧间距列也透明：跳过不写
  (void)bkg;
}

// Draw test pattern - 4 colored corner squares to verify pixel drawing works
void ESPWatchTFTDisplay::drawTestPattern() {
  Serial.println("[ESPWatch] Drawing test pattern...");

  // 4 colored corners - 20x20 squares
  int s = 30;
  // Top-left: RED
  LCD_Fill_hv(0, 0, s - 1, s - 1, 0xF800);
  // Top-right: GREEN
  LCD_Fill_hv(240 - s, 0, 239, s - 1, 0x07E0);
  // Bottom-left: BLUE
  LCD_Fill_hv(0, 285 - s, s - 1, 284, 0x001F);
  // Bottom-right: YELLOW
  LCD_Fill_hv(240 - s, 285 - s, 239, 284, 0xFFE0);
  // Center: WHITE
  LCD_Fill_hv(105, 127, 134, 156, 0xFFFF);

  // Write test text in center
  _bkg = 0x0000;
  _color = 0xFFFF;
  _text_size = 2;

  // Draw "LCD OK" text centered vertically above center
  const char* text = "LCD OK";
  int tx = 120 - (int)getTextWidth(text) / 2;
  int ty = 20;
  _cursor_x = tx;
  _cursor_y = ty;
  print(text);

  // Draw "TFT 240x285" below center
  const char* text2 = "TFT 240x285";
  _cursor_x = 120 - (int)getTextWidth(text2) / 2;
  _cursor_y = 250;
  print(text2);

  Serial.println("[ESPWatch] Test pattern drawn");
}

bool ESPWatchTFTDisplay::begin() {
  if (!_isOn) {
    Serial.println("[ESPWatch] Starting display begin()...");

    // Full LCD init - this will light the backlight and send SPI init commands
    LCD_Init();

    // Draw a visible test pattern to prove the display is working
    drawTestPattern();

    _isOn = true;
    _needsReinit = false;  // LCD_Init already did full initialization
    Serial.println("ESPWatch TFT 240x285 display initialized");
  }
  return true;
}

void ESPWatchTFTDisplay::turnOn() {
  // 先尝试快速唤醒：SLPOUT + DISPON + 背光
  // 如果 SPI 在 light sleep 后丢失，LCD_Wakeup 会失败，再 fallback 到完整 LCD_Init
  LCD_Wakeup();
  _isOn = true;
  _needsReinit = false;
  Serial.println("[LCD] turnOn() - LCD_Wakeup (SLPOUT+DISPON+backlight)");
}

void ESPWatchTFTDisplay::turnOff() {
  if (_isOn) {
    LCD_Sleep();
    _isOn = false;
  }
}

void ESPWatchTFTDisplay::clear() {
  LCD_Clear(_bkg);
}

void ESPWatchTFTDisplay::_reinitSpi() {
  // LCD 用 HSPI (SPI3_HOST)，LoRa 用 FSPI (SPI2_HOST)，完全独立。
  // 但 LoRa 射频操作可能通过 GPIO 串扰让 ST7789 进入不可恢复的异常状态。
  // 仅靠 LCD_Wakeup 不够——需要发送软件复位 SWRESET，然后完整重配关键寄存器。
  Serial.println("[DISP]   SWRESET + reinit ST7789...");

  // 1. 软件复位
  LCD_WR_REG(0x01);  // SWRESET
  delay(150);      // ST7789 手册要求复位后等待 120ms+

  // 2. 退出睡眠（SWRESET 后芯片自动进入睡眠）
  LCD_WR_REG(0x11);  // SLPOUT
  delay(120);

  // 3. 关键寄存器（与 LCD_Init 一致）
  LCD_WR_REG(0x36);  // MADCTL
  LCD_WR_DATA(0x00);

  LCD_WR_REG(0x3A);  // COLMOD: 16-bit RGB565
  LCD_WR_DATA(0x05);

  LCD_WR_REG(0xB2);  // PORCTRL
  LCD_WR_DATA(0x0C);
  LCD_WR_DATA(0x0C);
  LCD_WR_DATA(0x00);
  LCD_WR_DATA(0x33);
  LCD_WR_DATA(0x33);

  LCD_WR_REG(0xB7);  // GCTRL
  LCD_WR_DATA(0x35);

  LCD_WR_REG(0xBB);  // VCOMS
  LCD_WR_DATA(0x17);

  LCD_WR_REG(0xC0);  // LCMCTRL
  LCD_WR_DATA(0x2C);

  LCD_WR_REG(0xC2);  // VDVVRHEN
  LCD_WR_DATA(0x01);

  LCD_WR_REG(0xC3);  // VRHS
  LCD_WR_DATA(0x12);

  LCD_WR_REG(0xC4);  // VDVS
  LCD_WR_DATA(0x20);

  LCD_WR_REG(0xC6);  // FRCTRL2
  LCD_WR_DATA(0x0F);

  LCD_WR_REG(0xD0);  // PWCTRL1
  LCD_WR_DATA(0xA4);
  LCD_WR_DATA(0xA1);

  // Gamma 正极性
  LCD_WR_REG(0xE0);
  LCD_WR_DATA(0xD0);
  LCD_WR_DATA(0x04);
  LCD_WR_DATA(0x0D);
  LCD_WR_DATA(0x11);
  LCD_WR_DATA(0x13);
  LCD_WR_DATA(0x2B);
  LCD_WR_DATA(0x3F);
  LCD_WR_DATA(0x54);
  LCD_WR_DATA(0x4C);
  LCD_WR_DATA(0x18);
  LCD_WR_DATA(0x0D);
  LCD_WR_DATA(0x0B);
  LCD_WR_DATA(0x1F);
  LCD_WR_DATA(0x23);

  // Gamma 负极性
  LCD_WR_REG(0xE1);
  LCD_WR_DATA(0xD0);
  LCD_WR_DATA(0x04);
  LCD_WR_DATA(0x0C);
  LCD_WR_DATA(0x11);
  LCD_WR_DATA(0x13);
  LCD_WR_DATA(0x2C);
  LCD_WR_DATA(0x3F);
  LCD_WR_DATA(0x44);
  LCD_WR_DATA(0x51);
  LCD_WR_DATA(0x2F);
  LCD_WR_DATA(0x1F);
  LCD_WR_DATA(0x1F);
  LCD_WR_DATA(0x20);
  LCD_WR_DATA(0x23);

  LCD_WR_REG(0x21);  // INVON

  LCD_WR_REG(0x29);  // DISPON

  LCD_set_direction(USE_HORIZONTAL);

  Serial.println("[DISP]   ST7789 SWRESET + reinit complete");
}

void ESPWatchTFTDisplay::startFrame(Color bkg) {
  if (!_isOn) {
    Serial.println("[DISP] startFrame: display not on, calling begin()");
    begin();
  }

  switch (bkg) {
    case DisplayDriver::DARK :
      _bkg = 0x0000;
      break;
    case DisplayDriver::LIGHT :
      _bkg = 0xFFFF;
      break;
    case DisplayDriver::RED :
      _bkg = 0xF800;
      break;
    case DisplayDriver::GREEN :
      _bkg = 0x07E0;
      break;
    case DisplayDriver::BLUE :
      _bkg = 0x001F;
      break;
    case DisplayDriver::YELLOW :
      _bkg = 0xFFE0;
      break;
    case DisplayDriver::ORANGE :
      _bkg = 0xFC00;
      break;
    default:
      _bkg = 0x0000;
      break;
  }
  _color = (_bkg == 0x0000) ? 0xFFFF : 0x0000;

  Serial.printf("[DISP] startFrame: _isOn=%d, _bkg=0x%04X, _color=0x%04X\n", _isOn, _bkg, _color);

  // 只在首次初始化或需要重新初始化时才调用 _reinitSpi()
  if (_needsReinit) {
    _reinitSpi();
    _needsReinit = false;
  }

  LCD_Clear(_bkg);

  _cursor_x = 0;
  _cursor_y = 0;
  Serial.println("[DISP]   clear done");
}

void ESPWatchTFTDisplay::setTextSize(int sz) {
  _text_size = max(1, min(sz, 8));
}

void ESPWatchTFTDisplay::setColor(Color c) {
  switch (c) {
    case DisplayDriver::DARK :
      _color = 0x0000;
      break;
    case DisplayDriver::LIGHT :
      _color = 0xFFFF;
      break;
    case DisplayDriver::RED :
      _color = 0xF800;
      break;
    case DisplayDriver::GREEN :
      _color = 0x07E0;
      break;
    case DisplayDriver::BLUE :
      _color = 0x001F;
      break;
    case DisplayDriver::YELLOW :
      _color = 0xFFE0;
      break;
    case DisplayDriver::ORANGE :
      _color = 0xFC00;
      break;
    default:
      _color = 0xFFFF;
      break;
  }
}

void ESPWatchTFTDisplay::setCursor(int x, int y) {
  _cursor_x = x;
  _cursor_y = y;
}

void ESPWatchTFTDisplay::print(const char* str) {
  if (!str || !_isOn) return;

  unsigned long t0 = millis();
  int cx = _cursor_x;
  int cy = _cursor_y;
  int char_count = 0;

  for (int i = 0; str[i] != 0; i++) {
    if (str[i] == '\n') {
      cx = 0;
      cy += charHeightPx();
      continue;
    }
    if (str[i] == '\r') continue;

    if (cx + charWidthPx() > 240) {
      cx = 0;
      cy += charHeightPx();
    }
    if (cy + charHeightPx() > 285) break;

    drawChar5x7(str[i], cx, cy, _color, _bkg, _text_size);
    cx += charWidthPx();
    char_count++;
  }

  _cursor_x = cx;
  _cursor_y = cy;

  if (char_count > 0) {
    Serial.printf("[DISP]   print '%s' (%d chars) done in %lums\n", str, char_count, millis() - t0);
  }
}

void ESPWatchTFTDisplay::fillRect(int x, int y, int w, int h) {
  if (!_isOn || w <= 0 || h <= 0) return;
  unsigned long t0 = millis();
  // LCD_Fill_hv 已经是 WDT 安全的（每 64 像素 yield 一次）
  LCD_Fill_hv(x, y, x + w - 1, y + h - 1, _color);
  Serial.printf("[DISP]   fillRect(%d,%d,%d,%d) color=0x%04X done in %lums\n", x, y, w, h, _color, millis() - t0);
}

void ESPWatchTFTDisplay::drawRect(int x, int y, int w, int h) {
  if (!_isOn || w <= 0 || h <= 0) return;
  // 用 4 个 1 像素宽的 fillRect 画边，避免逐像素的 LCD_SetCursor 开销
  fillRect(x, y, w, 1);           // 顶
  fillRect(x, y + h - 1, w, 1);   // 底
  fillRect(x, y, 1, h);           // 左
  fillRect(x + w - 1, y, 1, h);   // 右
}

void ESPWatchTFTDisplay::drawXbm(int x, int y, const uint8_t* bits, int w, int h) {
  if (!_isOn) return;
  int width_in_bytes = (w + 7) / 8;

  uint8_t enc_color[4];
  encode_color_9bit(_color, enc_color);

  uint32_t pixel_count = 0;

  for (int row = 0; row < h; row++) {
    for (int col = 0; col < w; col++) {
      int byte_idx = (row * width_in_bytes) + (col / 8);
      int bit_idx = 7 - (col % 8);  // MSB first
      bool pixel_on = bits[byte_idx] & (1 << bit_idx);
      if (pixel_on) {
        LCD_SetCursor(x + col, y + row);
        push_encoded_pixel(enc_color);
        pixel_count++;
        if ((pixel_count & 0x3F) == 0) yield();
      }
    }
  }
}

uint16_t ESPWatchTFTDisplay::getTextWidth(const char* str) {
  if (!str) return 0;
  int count = 0;
  for (int i = 0; str[i] != 0; i++) {
    if (str[i] != '\n' && str[i] != '\r') count++;
  }
  return count * charWidthPx();
}

void ESPWatchTFTDisplay::endFrame() {
}