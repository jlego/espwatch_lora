#include "lcd.h"

#define u8 char
#define u16 int
#define u32 long
#define SPI1 0

// LCD uses independent FSPI controller (not shared with LoRa's SPI)
// LoRa uses SPI2 with pins 18/11/12, LCD uses FSPI with pins 5/6
SPIClass lcdSPI(FSPI);
_lcd_dev lcddev;

#if CONFIG_IDF_TARGET_ESP32S2 || CONFIG_IDF_TARGET_ESP32S3
#define VSPI FSPI
#endif

#define delay_ms(x) delay(x)
u16 POINT_COLOR = 0x0000, BACK_COLOR = 0xFFFF;

// 9-bit SPI via 2-byte transfer (16 clock cycles):
// Frame:  [cmd, D7, D6, D5, D4, D3, D2, D1, D0]
// Byte 0: [cmd, D7, D6, D5, D4, D3, D2, D1]
// Byte 1: [D0,   0,  0,  0,  0,  0,  0,  0 ]
// NOTE: CS MUST toggle between each 9-bit frame to reset alignment
//       (because 16 clock ≠ 9 clock, leftover bits cause misalignment)
u8 SPI_WriteByte(int SPIx, u8 Byte, u8 cmd) {
    uint8_t txbuf[2];
    txbuf[0] = (uint8_t)((cmd << 7) | (Byte >> 1)) & 0xFF;
    txbuf[1] = (uint8_t)(Byte << 7) & 0xFF;
    lcdSPI.transferBytes(txbuf, NULL, 2);
    return 0;
}

void LCD_WR_REG(u8 data) {
    digitalWrite(LCD_CS, LOW);
    SPI_WriteByte(SPI1, data, 0);
    digitalWrite(LCD_CS, HIGH);
}

void LCD_WR_DATA(u8 data) {
    digitalWrite(LCD_CS, LOW);
    SPI_WriteByte(SPI1, data, 1);
    digitalWrite(LCD_CS, HIGH);
}

void LCD_WriteReg(u8 LCD_Reg, u16 LCD_RegValue) {
    LCD_WR_REG(LCD_Reg);
    LCD_WR_DATA(LCD_RegValue);
}

void LCD_WriteRAM_Prepare(void) {
    LCD_WR_REG(lcddev.wramcmd);
}

void Lcd_WriteData_16Bit(u16 Data) {
    u8 hi = (u8)(Data >> 8);
    u8 lo = (u8)(Data & 0xFF);
    digitalWrite(LCD_CS, LOW);
    SPI_WriteByte(SPI1, lo, 1);
    digitalWrite(LCD_CS, HIGH);
    digitalWrite(LCD_CS, LOW);
    SPI_WriteByte(SPI1, hi, 1);
    digitalWrite(LCD_CS, HIGH);
}

void LCD_DrawPoint(u16 x, u16 y) {
    LCD_SetCursor(x, y);
    Lcd_WriteData_16Bit(POINT_COLOR);
}

// LCD_Clear: write solid color to entire screen
// IMPORTANT: toggle CS after each 9-bit frame to maintain alignment
void LCD_Clear(u16 Color) {
    unsigned int i, m;
    u8 hi = (u8)(Color >> 8);
    u8 lo = (u8)(Color & 0xFF);
    LCD_SetWindows(0, 0, lcddev.width - 1, lcddev.height - 1);

    LCD_WriteRAM_Prepare();

    for (i = 0; i < lcddev.height; i++) {
        for (m = 0; m < lcddev.width; m++) {
            digitalWrite(LCD_CS, LOW);
            SPI_WriteByte(SPI1, lo, 1);
            digitalWrite(LCD_CS, HIGH);
            digitalWrite(LCD_CS, LOW);
            SPI_WriteByte(SPI1, hi, 1);
            digitalWrite(LCD_CS, HIGH);
        }
    }
}

void LCD_GPIOInit(void) {
    pinMode(LCD_LED, OUTPUT);
    pinMode(LCD_CS, OUTPUT);

    digitalWrite(LCD_CS, HIGH);
    lcdSPI.begin(VSPI_SCLK, VSPI_MISO, VSPI_MOSI, VSPI_SS);
    lcdSPI.beginTransaction(SPISettings(40000000, MSBFIRST, SPI_MODE0));
}

void LCD_RESET(void) {
#if LCD_RST != -1
    pinMode(LCD_RST, OUTPUT);
    digitalWrite(LCD_RST, LOW);
    delay_ms(100);
    digitalWrite(LCD_RST, HIGH);
    delay_ms(50);
#else
    delay_ms(150);
#endif
}

void LCD_Init(void) {
    LCD_GPIOInit();
    LCD_RESET();

    // ST7789 IPS init sequence
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

    LCD_WR_REG(0x21);

    LCD_WR_REG(0x11);
    delay_ms(120);

    LCD_WR_REG(0x29);

    LCD_set_direction(USE_HORIZONTAL);
    ledcAttach(LCD_LED, 5000, 8);
    ledcWrite(LCD_LED, BRIGHTNESS_DEFAULT);
}

void LCD_SetWindows(u16 xStar, u16 yStar, u16 xEnd, u16 yEnd) {
    LCD_WR_REG(lcddev.setxcmd);
    LCD_WR_DATA((xStar + lcddev.xoffset) >> 8);
    LCD_WR_DATA(xStar + lcddev.xoffset);
    LCD_WR_DATA((xEnd + lcddev.xoffset) >> 8);
    LCD_WR_DATA(xEnd + lcddev.xoffset);
    LCD_WR_REG(lcddev.setycmd);
    LCD_WR_DATA((yStar + lcddev.yoffset) >> 8);
    LCD_WR_DATA(yStar + lcddev.yoffset);
    LCD_WR_DATA((yEnd + lcddev.yoffset) >> 8);
    LCD_WR_DATA(yEnd + lcddev.yoffset);
}

void LCD_SetCursor(u16 Xpos, u16 Ypos) {
    LCD_WR_REG(lcddev.setxcmd);
    LCD_WR_DATA(Xpos >> 8);
    LCD_WR_DATA(Xpos + lcddev.xoffset);
    LCD_WR_REG(lcddev.setycmd);
    LCD_WR_DATA(Ypos >> 8);
    LCD_WR_DATA(Ypos + lcddev.yoffset);
}

void LCD_set_direction(u8 lcd_direction) {
    lcddev.setxcmd = 0x2A;
    lcddev.setycmd = 0x2B;
    lcddev.wramcmd = 0x2C;
    lcddev.dir = lcd_direction % 4;
    switch (lcddev.dir) {
        case 0:
            lcddev.width = LCD_W;
            lcddev.height = LCD_H;
            lcddev.xoffset = 0;
            lcddev.yoffset = 0;
            break;
        case 1:
            lcddev.width = LCD_H;
            lcddev.height = LCD_W;
            lcddev.xoffset = 0;
            lcddev.yoffset = 0;
            break;
        case 2:
            lcddev.width = LCD_W;
            lcddev.height = LCD_H;
            lcddev.xoffset = 0;
            lcddev.yoffset = 0;
            break;
        case 3:
            lcddev.width = LCD_H;
            lcddev.height = LCD_W;
            lcddev.xoffset = 0;
            lcddev.yoffset = 0;
            break;
    }
}

void LCD_Fill_hv(u16 sx, u16 sy, u16 ex, u16 ey, u16 color) {
    u16 i, j;
    u16 width = ex - sx + 1;
    u16 height = ey - sy + 1;
    u8 hi = (u8)(color >> 8);
    u8 lo = (u8)(color & 0xFF);
    LCD_SetWindows(sx, sy, ex, ey);

    LCD_WriteRAM_Prepare();

    for (i = 0; i < height; i++) {
        for (j = 0; j < width; j++) {
            digitalWrite(LCD_CS, LOW);
            SPI_WriteByte(SPI1, lo, 1);
            digitalWrite(LCD_CS, HIGH);
            digitalWrite(LCD_CS, LOW);
            SPI_WriteByte(SPI1, hi, 1);
            digitalWrite(LCD_CS, HIGH);
        }
    }
}

void LCD_PushImage(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint8_t *data, uint32_t byte_len) {
    LCD_SetWindows(x1, y1, x2, y2);

    LCD_WriteRAM_Prepare();

    for (uint32_t i = 0; i < byte_len; i++) {
        digitalWrite(LCD_CS, LOW);
        SPI_WriteByte(SPI1, data[i], 1);
        digitalWrite(LCD_CS, HIGH);
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