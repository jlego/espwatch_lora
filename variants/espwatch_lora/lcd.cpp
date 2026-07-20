#include "lcd.h"

#define u8 char
#define u16 int
#define u32 long
#define SPI1 0

// LCD driver for ST7789-style display (9-bit SPI).
// CS toggles between frames to reset ST7789's internal 9-bit counter.
// beginTransaction is called ONCE in LCD_GPIOInit and never again -
// this matches the "working reference" pattern and is sufficient because:
// 1. LCD is the only device actively using this SPIClass instance
// 2. LoRa uses its own SPIClass instance (different pins, different CS)
// 3. Even though both use SPI2_HOST internally, Arduino-ESP32's
//    SPIClass implementation handles GPIO-matrix pin remapping per
//    instance, so the LCD's MOSI/SCK are completely separate from LoRa's.
SPIClass* lcdSPI = NULL;
static const int spiClk = 5 * 1000 * 1000;  // 5 MHz - compromise
_lcd_dev lcddev;

#define delay_ms(x) delay(x)
u16 POINT_COLOR = 0x0000, BACK_COLOR = 0xFFFF;

// ============================================================
// 9-bit SPI core
// ============================================================
// Frame:  [cmd/data, D7, D6, D5, D4, D3, D2, D1, D0]
// Sent as two 8-bit SPI bytes (16 SCK edges total):
// Byte 0: [cmd_bit, D7, D6, D5, D4, D3, D2, D1]  (cmd_bit = 0 for reg, 1 for data)
// Byte 1: [D0,       0,  0,  0,  0,  0,  0,  0 ]
//
// CS must toggle HIGH between frames for ST7789 to recognize frame
// boundaries. No beginTransaction needed per call - settings are
// configured once in LCD_GPIOInit.
void SPI_WriteByte(int SPIx, u8 Byte, u8 cmd) {
    uint8_t txbuf[2];
    txbuf[0] = (uint8_t)((cmd << 7) | ((Byte >> 1) & 0x7F));
    txbuf[1] = (uint8_t)(Byte << 7) & 0xFF;
    lcdSPI->transferBytes(txbuf, NULL, 2);
}

void LCD_WR_REG(u8 data) {
    digitalWrite(LCD_CS, LOW);
    SPI_WriteByte(SPI1, data, 0);
    digitalWrite(LCD_CS, HIGH);
    delayMicroseconds(1);
}

void LCD_WR_DATA(u8 data) {
    digitalWrite(LCD_CS, LOW);
    SPI_WriteByte(SPI1, data, 1);
    digitalWrite(LCD_CS, HIGH);
    delayMicroseconds(1);
}

void LCD_WriteReg(u8 LCD_Reg, u16 LCD_RegValue) {
    LCD_WR_REG(LCD_Reg);
    LCD_WR_DATA(LCD_RegValue);
}

void LCD_WriteRAM_Prepare(void) {
    LCD_WR_REG(lcddev.wramcmd);
}

// Write 16-bit pixel color (RGB565 HI then LO)
void Lcd_WriteData_16Bit(u16 Data) {
    digitalWrite(LCD_CS, LOW);
    SPI_WriteByte(SPI1, (Data >> 8) & 0xFF, 1);
    digitalWrite(LCD_CS, HIGH);
    digitalWrite(LCD_CS, LOW);
    SPI_WriteByte(SPI1, Data & 0xFF, 1);
    digitalWrite(LCD_CS, HIGH);
}

void LCD_DrawPoint(u16 x, u16 y) {
    LCD_SetCursor(x, y);
    Lcd_WriteData_16Bit(POINT_COLOR);
}

// ============================================================
// Bulk fill - direct, no beginTransaction spam
// ============================================================
void LCD_Clear(u16 Color) {
    u8 hi = (u8)((Color >> 8) & 0xFF);
    u8 lo = (u8)(Color & 0xFF);
    uint32_t total = (uint32_t)lcddev.width * lcddev.height;

    LCD_SetWindows(0, 0, lcddev.width - 1, lcddev.height - 1);

    uint8_t hih[2], loh[2];
    hih[0] = (uint8_t)(0x80 | ((hi >> 1) & 0x7F));
    hih[1] = (uint8_t)(hi << 7) & 0xFF;
    loh[0] = (uint8_t)(0x80 | ((lo >> 1) & 0x7F));
    loh[1] = (uint8_t)(lo << 7) & 0xFF;

    for (uint32_t n = 0; n < total; n++) {
        digitalWrite(LCD_CS, LOW);
        lcdSPI->transferBytes(hih, NULL, 2);
        digitalWrite(LCD_CS, HIGH);
        digitalWrite(LCD_CS, LOW);
        lcdSPI->transferBytes(loh, NULL, 2);
        digitalWrite(LCD_CS, HIGH);
        if ((n & 0x7FF) == 0) yield();
    }
}

// ============================================================
// Hardware init
// ============================================================
void LCD_GPIOInit(void) {
    lcdSPI = new SPIClass();

    pinMode(LCD_LED, OUTPUT);
    pinMode(LCD_CS, OUTPUT);
    digitalWrite(LCD_CS, HIGH);

    // Explicitly configure SCK and MOSI pins as OUTPUT to ensure
    // GPIO matrix routing is active. (Some framework versions only
    // route during beginTransaction; setting pinMode here is a belt-
    // and-suspenders approach to guarantee the pins are driven.)
    pinMode(VSPI_SCLK, OUTPUT);
    pinMode(VSPI_MOSI, OUTPUT);
    digitalWrite(VSPI_SCLK, LOW);
    digitalWrite(VSPI_MOSI, LOW);

    lcdSPI->begin(VSPI_SCLK, VSPI_MISO, VSPI_MOSI, -1);

    // ONE-TIME SPI configuration - settings remain valid until the
    // end of the program. This is the "original" simple pattern from
    // the working reference project.
    lcdSPI->beginTransaction(SPISettings(spiClk, MSBFIRST, SPI_MODE0));
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

// Backlight: LEDC PWM 5% brightness
void LCD_BacklightInit(void) {
#if LCD_LED != -1
    const int LEDC_CH = 0;
    const int LEDC_FREQ = 5000;
    const int LEDC_RES = 8;
    const int DUTY_5_PCT = 13;

    ledcSetup(LEDC_CH, LEDC_FREQ, LEDC_RES);
    ledcAttachPin(LCD_LED, LEDC_CH);
    ledcWrite(LEDC_CH, DUTY_5_PCT);
    Serial.printf("[LCD] Backlight 5%% on GPIO%d (LEDC ch=%d)\n", LCD_LED, LEDC_CH);
#endif
}

void LCD_Init(void) {
    Serial.println("[LCD] Initializing TFT display...");
    Serial.printf("[LCD] Pins: CS=%d, RST=%d, LED=%d, SCK=%d, MOSI=%d\n",
                  LCD_CS, LCD_RST, LCD_LED, VSPI_SCLK, VSPI_MOSI);

    LCD_GPIOInit();
    LCD_RESET();

    // -------- ST7789 init sequence --------
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
    // ---------------------------------------

    LCD_set_direction(USE_HORIZONTAL);
    LCD_BacklightInit();

    // Fill with WHITE (0xFFFF) - if SPI is working you MUST see light
    LCD_Clear(0xFFFF);
    Serial.println("[LCD] Init complete - screen filled WHITE");
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
    const u16 PANEL_OFFSET = 0;
    switch (lcddev.dir) {
        case 0:
            lcddev.width = LCD_W;
            lcddev.height = LCD_H;
            lcddev.xoffset = 0;
            lcddev.yoffset = PANEL_OFFSET;
            LCD_WriteReg(0x36, 0);
            break;
        case 1:
            lcddev.width = LCD_H;
            lcddev.height = LCD_W;
            lcddev.xoffset = PANEL_OFFSET;
            lcddev.yoffset = 0;
            LCD_WriteReg(0x36, (1 << 6) | (1 << 5));
            break;
        case 2:
            lcddev.width = LCD_W;
            lcddev.height = LCD_H;
            lcddev.xoffset = 0;
            lcddev.yoffset = PANEL_OFFSET;
            LCD_WriteReg(0x36, (1 << 6) | (1 << 7));
            break;
        case 3:
            lcddev.width = LCD_H;
            lcddev.height = LCD_W;
            lcddev.xoffset = PANEL_OFFSET;
            lcddev.yoffset = 0;
            LCD_WriteReg(0x36, (1 << 7) | (1 << 5));
            break;
    }
}

void LCD_Fill_hv(u16 sx, u16 sy, u16 ex, u16 ey, u16 color) {
    u8 hi = (u8)((color >> 8) & 0xFF);
    u8 lo = (u8)(color & 0xFF);
    uint32_t total = (uint32_t)(ex - sx + 1) * (ey - sy + 1);

    LCD_SetWindows(sx, sy, ex, ey);

    uint8_t hih[2], loh[2];
    hih[0] = (uint8_t)(0x80 | ((hi >> 1) & 0x7F));
    hih[1] = (uint8_t)(hi << 7) & 0xFF;
    loh[0] = (uint8_t)(0x80 | ((lo >> 1) & 0x7F));
    loh[1] = (uint8_t)(lo << 7) & 0xFF;

    for (uint32_t n = 0; n < total; n++) {
        digitalWrite(LCD_CS, LOW);
        lcdSPI->transferBytes(hih, NULL, 2);
        digitalWrite(LCD_CS, HIGH);
        digitalWrite(LCD_CS, LOW);
        lcdSPI->transferBytes(loh, NULL, 2);
        digitalWrite(LCD_CS, HIGH);
        if ((n & 0x7FF) == 0) yield();
    }
}

void LCD_PushImage(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint8_t *data, uint32_t byte_len) {
    LCD_SetWindows(x1, y1, x2, y2);
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