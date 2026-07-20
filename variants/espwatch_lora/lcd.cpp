#include "lcd.h"

#define u8 char
#define u16 int
#define u32 long
#define SPI1 0

// LCD 独占 ESP32-S3 的 SPI3_HOST (HSPI) 控制器
// LoRa 独占 SPI2_HOST (FSPI) 控制器
// 两个独立硬件 SPI 控制器，完全不存在总线竞争！
SPIClass* lcdSPI = NULL;
static const int spiClk = 40 * 1000 * 1000;  // 40 MHz
_lcd_dev lcddev;

#define delay_ms(x) delay(x)
u16 POINT_COLOR = 0x0000, BACK_COLOR = 0xFFFF;

// ============================================================
// 9-bit SPI 核心
// ============================================================
// 帧格式: [cmd/data bit, D7..D0] 共 9 位
// 拆成两个 8-bit SPI 字节发送:
//   Byte 0: [cmd_bit, D7, D6, D5, D4, D3, D2, D1]  (cmd_bit: 0=命令, 1=数据)
//   Byte 1: [D0,        0,  0,  0,  0,  0,  0,  0 ]
// CS 在帧之间必须拉高，用于 ST7789 重置 9-bit 帧计数器
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

// 写 16-bit 像素颜色 (RGB565: HI + LO)
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
// 批量填充 - LCD 独占 SPI3_HOST，无需释放总线
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
        if ((n & 0x3F) == 0) yield();  // 每 64 像素喂一次看门狗 - ESP32 WDT 安全
    }
}

// ============================================================
// 硬件初始化 - LCD 使用 HSPI (SPI3_HOST)，与 LoRa 的 FSPI (SPI2_HOST) 独立
// ============================================================
void LCD_GPIOInit(void) {
    // 只在第一次调用时 new SPIClass，避免重复 new 导致 SPI 控制器状态异常
    // LoRa 用 FSPI (SPI2_HOST)，LCD 用 HSPI (SPI3_HOST)，两个独立硬件控制器
    if (lcdSPI == NULL) {
        lcdSPI = new SPIClass(HSPI);
    }

    pinMode(LCD_LED, OUTPUT);
    pinMode(LCD_CS, OUTPUT);
    digitalWrite(LCD_CS, HIGH);

    // 配置 LCD 的 SPI 引脚到 HSPI 控制器（通过 GPIO Matrix 任意映射）
    lcdSPI->begin(VSPI_SCLK, VSPI_MISO, VSPI_MOSI, -1);

    // LCD 独占 HSPI，只需 beginTransaction 一次配置即可持续使用
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
    delay_ms(200);  // 等待电源上电复位完成
#endif
}

// 背光: LEDC PWM 5% 亮度
void LCD_BacklightInit(void) {
#if LCD_LED != -1
    const int LEDC_CH = 7;  // 通道7，避免与 tone() 蜂鸣器冲突（tone() 从通道0开始抢占）
    const int LEDC_FREQ = 5000;
    const int LEDC_RES = 8;
    const int DUTY_5_PCT = 13;

    ledcSetup(LEDC_CH, LEDC_FREQ, LEDC_RES);
    ledcAttachPin(LCD_LED, LEDC_CH);
    ledcWrite(LEDC_CH, DUTY_5_PCT);
    Serial.printf("[LCD] Backlight 5%% on GPIO%d (LEDC ch=%d)\n", LCD_LED, LEDC_CH);
#endif
}

void LCD_Reinit(void) {
    // 快速重新初始化 LCD - 只做关键步骤，不等待
    // 用于在 LoRa/I2C 操作后确保 LCD 状态稳定
    LCD_GPIOInit();

    // 发送关键 ST7789 寄存器，确保显示开启、像素格式正确
    LCD_WR_REG(0x11);        // SLPOUT: 退出睡眠
    delay_ms(120);           // ST7789 手册要求 120ms

    LCD_WR_REG(0x36);        // MADCTL: 内存访问控制
    LCD_WR_DATA(0x00);

    LCD_WR_REG(0x3A);        // COLMOD: 像素格式
    LCD_WR_DATA(0x05);       // 16-bit RGB565

    LCD_WR_REG(0x21);        // INVON: 反色显示开 (匹配原 init)

    LCD_WR_REG(0x29);        // DISPON: 显示开启

    LCD_set_direction(USE_HORIZONTAL);
}

void LCD_Init(void) {
    Serial.println("[LCD] Initializing TFT display...");
    Serial.printf("[LCD] Pins: CS=%d, RST=%d, LED=%d, SCK=%d, MOSI=%d (HSPI/SPI3_HOST)\n",
                  LCD_CS, LCD_RST, LCD_LED, VSPI_SCLK, VSPI_MOSI);

    LCD_GPIOInit();
    LCD_RESET();

    // -------- ST7789 初始化序列 --------
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

    // 填充白色确认显示工作
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
        if ((n & 0x3F) == 0) yield();  // 每 64 像素喂一次看门狗 - ESP32 WDT 安全
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