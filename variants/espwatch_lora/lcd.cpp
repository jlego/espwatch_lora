#include "lcd.h"

#define u8 char
#define u16 int
#define u32 long
#define SPI1 0

// LCD uses its own SPIClass instance with custom pins (SCK=16, MOSI=17).
// Default constructor picks the default SPI host (HSPI/SPI2 on ESP32-S3).
// Pins are routed via GPIO Matrix, independent of LoRa's SPI on different pins.
// CS is manually toggled to maintain 9-bit SPI alignment for ST7789.
SPIClass* lcdSPI = NULL;
static const int spiClk = 10 * 1000 * 1000;  // 10 MHz for reliable signal
_lcd_dev lcddev;

#define delay_ms(x) delay(x)
u16 POINT_COLOR = 0x0000, BACK_COLOR = 0xFFFF;

// 9-bit SPI via 2-byte transfer (16 clock cycles):
// Frame:  [cmd, D7, D6, D5, D4, D3, D2, D1, D0]
// Byte 0: [cmd, D7, D6, D5, D4, D3, D2, D1]
// Byte 1: [D0,   0,  0,  0,  0,  0,  0,  0 ]
// CS is toggled manually between transfers to maintain 9-bit alignment.
u8 SPI_WriteByte(int SPIx, u8 Byte, u8 cmd) {
    uint8_t txbuf[2];
    txbuf[0] = (uint8_t)((cmd << 7) | ((Byte >> 1) & 0x7F));
    txbuf[1] = (uint8_t)(Byte << 7) & 0xFF;
    lcdSPI->transferBytes(txbuf, NULL, 2);
    return 0;
}

void LCD_WR_REG(u8 data) {
    lcdSPI->beginTransaction(SPISettings(spiClk, MSBFIRST, SPI_MODE0));
    digitalWrite(LCD_CS, LOW);
    SPI_WriteByte(SPI1, data, 0);
    digitalWrite(LCD_CS, HIGH);
    lcdSPI->endTransaction();
}

void LCD_WR_DATA(u8 data) {
    lcdSPI->beginTransaction(SPISettings(spiClk, MSBFIRST, SPI_MODE0));
    digitalWrite(LCD_CS, LOW);
    SPI_WriteByte(SPI1, data, 1);
    digitalWrite(LCD_CS, HIGH);
    lcdSPI->endTransaction();
}

void LCD_WriteReg(u8 LCD_Reg, u16 LCD_RegValue) {
    LCD_WR_REG(LCD_Reg);
    LCD_WR_DATA(LCD_RegValue);
}

void LCD_WriteRAM_Prepare(void) {
    LCD_WR_REG(lcddev.wramcmd);
}

// Write 16-bit pixel color as two 9-bit frames:
// Frame 1: 1 + (HI BYTE)
// Frame 2: 1 + (LO BYTE)
// CS toggles between the two bytes to maintain 9-bit alignment.
void Lcd_WriteData_16Bit(u16 Data) {
    u8 hi = (u8)((Data >> 8) & 0xFF);
    u8 lo = (u8)(Data & 0xFF);
    lcdSPI->beginTransaction(SPISettings(spiClk, MSBFIRST, SPI_MODE0));
    // ST7789 expects RGB565 HI-byte first, then LO-byte
    digitalWrite(LCD_CS, LOW);
    SPI_WriteByte(SPI1, hi, 1);
    digitalWrite(LCD_CS, HIGH);
    digitalWrite(LCD_CS, LOW);
    SPI_WriteByte(SPI1, lo, 1);
    digitalWrite(LCD_CS, HIGH);
    lcdSPI->endTransaction();
}

void LCD_DrawPoint(u16 x, u16 y) {
    LCD_SetCursor(x, y);
    Lcd_WriteData_16Bit(POINT_COLOR);
}

// LCD_Clear: write solid color to entire screen
void LCD_Clear(u16 Color) {
    unsigned int i, m;
    LCD_SetWindows(0, 0, lcddev.width - 1, lcddev.height - 1);
    // NOTE: LCD_SetWindows already calls LCD_WriteRAM_Prepare() internally

    for (i = 0; i < lcddev.height; i++) {
        for (m = 0; m < lcddev.width; m++) {
            Lcd_WriteData_16Bit(Color);
        }
    }
}

void LCD_GPIOInit(void) {
    // Create new SPI instance using default constructor.
    // On ESP32-S3, default SPIClass maps to HSPI (SPI2_HOST). LoRa uses its own
    // SPIClass instance on the same host but different pins, routed via GPIO Matrix.
    lcdSPI = new SPIClass();
    pinMode(LCD_LED, OUTPUT);
    pinMode(LCD_CS, OUTPUT);
    digitalWrite(LCD_CS, HIGH);

    // Initialize SPI controller with custom pins.
    // SCK=16, MOSI=17, MISO=-1 (unused, LCD is write-only).
    // SS=-1: SPI peripheral does NOT auto-control CS — we toggle it manually
    // in LCD_WR_REG/LCD_WR_DATA to maintain 9-bit SPI alignment.
    lcdSPI->begin(VSPI_SCLK, VSPI_MISO, VSPI_MOSI, -1);
}

void LCD_RESET(void) {
#if LCD_RST != -1
    pinMode(LCD_RST, OUTPUT);
    digitalWrite(LCD_RST, LOW);
    delay_ms(100);
    digitalWrite(LCD_RST, HIGH);
    delay_ms(100);
#else
    delay_ms(200);  // wait for power-on reset to complete
#endif
}

// Backlight: turn on at high brightness
void LCD_BacklightInit(void) {
#if LCD_LED != -1
    digitalWrite(LCD_LED, HIGH);
    Serial.printf("[LCD] Backlight enabled on GPIO%d\n", LCD_LED);
#endif
}

void LCD_Init(void) {
    Serial.println("[LCD] Initializing TFT display...");
    Serial.printf("[LCD] Pins: CS=%d, RST=%d, LED=%d, SCK=%d, MOSI=%d\n",
                  LCD_CS, LCD_RST, LCD_LED, VSPI_SCLK, VSPI_MOSI);

    // GPIO + SPI init
    LCD_GPIOInit();

    // Hardware reset
    LCD_RESET();

    // ST7789 init sequence (from working reference)
    LCD_WR_REG(0x36);
    LCD_WR_DATA(0x00);

    LCD_WR_REG(0x3A);
    LCD_WR_DATA(0x05);

    LCD_WR_REG(0xB2);
    LCD_WR_DATA(0x0C);
    LCD_WR_DATA(0x0C);
    LCD_WR_DATA(0x00);
    LCD_WR_DATA(0x33);
    LCD_WR_DATA(0x33);

    LCD_WR_REG(0xB7);
    LCD_WR_DATA(0x35);

    LCD_WR_REG(0xBB);
    LCD_WR_DATA(0x17);

    LCD_WR_REG(0xC0);
    LCD_WR_DATA(0x2C);

    LCD_WR_REG(0xC2);
    LCD_WR_DATA(0x01);

    LCD_WR_REG(0xC3);
    LCD_WR_DATA(0x12);

    LCD_WR_REG(0xC4);
    LCD_WR_DATA(0x20);

    LCD_WR_REG(0xC6);
    LCD_WR_DATA(0x0F);

    LCD_WR_REG(0xD0);
    LCD_WR_DATA(0xA4);
    LCD_WR_DATA(0xA1);

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

    LCD_WR_REG(0x21);  // display inversion

    LCD_WR_REG(0x11);  // sleep out
    delay_ms(120);

    LCD_WR_REG(0x29);  // display on

    // Set direction (this writes 0x36 register)
    LCD_set_direction(USE_HORIZONTAL);

    // Enable backlight
    LCD_BacklightInit();

    // Clear screen to verify
    LCD_Clear(0x0000);
    Serial.println("[LCD] Init complete");
}

void LCD_SetWindows(u16 xStar, u16 yStar, u16 xEnd, u16 yEnd) {
    u16 xs = xStar + lcddev.xoffset;
    u16 xe = xEnd + lcddev.xoffset;
    u16 ys = yStar + lcddev.yoffset;
    u16 ye = yEnd + lcddev.yoffset;

    LCD_WR_REG(lcddev.setxcmd);
    LCD_WR_DATA((u8)(xs >> 8));
    LCD_WR_DATA((u8)(xs & 0xFF));
    LCD_WR_DATA((u8)(xe >> 8));
    LCD_WR_DATA((u8)(xe & 0xFF));

    LCD_WR_REG(lcddev.setycmd);
    LCD_WR_DATA((u8)(ys >> 8));
    LCD_WR_DATA((u8)(ys & 0xFF));
    LCD_WR_DATA((u8)(ye >> 8));
    LCD_WR_DATA((u8)(ye & 0xFF));

    LCD_WriteRAM_Prepare();
}

void LCD_SetCursor(u16 Xpos, u16 Ypos) {
    LCD_SetWindows(Xpos, Ypos, Xpos, Ypos);
}

void LCD_set_direction(u8 lcd_direction) {
    lcddev.setxcmd = 0x2A;
    lcddev.setycmd = 0x2B;
    lcddev.wramcmd = 0x2C;
    lcddev.dir = lcd_direction % 4;
    // 240x285 is likely a 240x320 ST7789 panel with top/bottom rows cropped.
    // y-offset = (320-285)/2 = 17.5, rounded to 20 for common panel layouts.
    const u16 PANEL_Y_OFFSET = 20;
    switch (lcddev.dir) {
        case 0:
            lcddev.width = LCD_W;
            lcddev.height = LCD_H;
            lcddev.xoffset = 0;
            lcddev.yoffset = PANEL_Y_OFFSET;
            LCD_WriteReg(0x36, 0);
            break;
        case 1:
            lcddev.width = LCD_H;
            lcddev.height = LCD_W;
            lcddev.xoffset = PANEL_Y_OFFSET;
            lcddev.yoffset = 0;
            LCD_WriteReg(0x36, (1 << 6) | (1 << 5));
            break;
        case 2:
            lcddev.width = LCD_W;
            lcddev.height = LCD_H;
            lcddev.xoffset = 0;
            lcddev.yoffset = PANEL_Y_OFFSET;
            LCD_WriteReg(0x36, (1 << 6) | (1 << 7));
            break;
        case 3:
            lcddev.width = LCD_H;
            lcddev.height = LCD_W;
            lcddev.xoffset = PANEL_Y_OFFSET;
            lcddev.yoffset = 0;
            LCD_WriteReg(0x36, (1 << 7) | (1 << 5));
            break;
    }
}

void LCD_Fill_hv(u16 sx, u16 sy, u16 ex, u16 ey, u16 color) {
    u16 i, j;
    u16 width = ex - sx + 1;
    u16 height = ey - sy + 1;
    LCD_SetWindows(sx, sy, ex, ey);
    // NOTE: LCD_SetWindows already calls LCD_WriteRAM_Prepare() internally

    for (i = 0; i < height; i++) {
        for (j = 0; j < width; j++) {
            Lcd_WriteData_16Bit(color);
        }
    }
}

void LCD_PushImage(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint8_t *data, uint32_t byte_len) {
    LCD_SetWindows(x1, y1, x2, y2);

    LCD_WriteRAM_Prepare();

    // Push raw bytes (assume caller has correct RGB565 HI-LO ordering)
    for (uint32_t i = 0; i < byte_len; i++) {
        LCD_WR_DATA(data[i]);
    }
}

void LCD_Sleep(void) {
    LCD_WR_REG(0x10);
    delay_ms(120);
}

void LCD_Wakeup(void) {
    LCD_WR_REG(0x11);
    delay_ms(120);
    LCD_WR_REG(0x29);
}