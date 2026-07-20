#ifndef __LCD_H
#define __LCD_H		
//#include "sys.h"	 

#include <Arduino.h>
#include "stdlib.h"
#include <SPI.h>
#include "soc/gpio_struct.h"
#include "soc/io_mux_reg.h"

// LCD SPI controller — defined in lcd.cpp, shared with ESPWatchTFT.h
extern SPIClass* lcdSPI;

#define u8  char
#define u16 int
#define u32 long


typedef struct  
{										    
	uint16_t width;			
	uint16_t height;			
	uint16_t id;				
	uint8_t  dir;			 
	uint16_t	 wramcmd;		
	uint16_t  setxcmd;		
	uint16_t  setycmd;		 
	uint8_t   xoffset;    
  uint8_t	 yoffset;	
}_lcd_dev; 


extern _lcd_dev lcddev;	

#ifndef USE_HORIZONTAL
#define USE_HORIZONTAL  	 0
#endif

//////////////////////////////////////////////////////////////////////////////////	  

// 定义默认分辨率，如果未在variant.h中定义的话
#ifndef LCD_W
#define LCD_W 240
#endif

#ifndef LCD_H
#define LCD_H 285
#endif
 
extern u16  POINT_COLOR;  
extern u16  BACK_COLOR; 

// 引脚定义 - 优先使用variant.h中的定义，否则使用默认值
#ifndef LCD_LED
#ifdef TFT_BL
#define LCD_LED TFT_BL
#else
#define LCD_LED  15
#endif
#endif

#ifndef LCD_CS
#ifdef ST7789_CS
#define LCD_CS   ST7789_CS
#else
#define LCD_CS   18
#endif
#endif

#ifndef LCD_DC
#ifdef ST7789_RS
#define LCD_DC   ST7789_RS
#else
#define LCD_DC   -1  //LCD_DC    
#endif
#endif

#ifndef LCD_RST
#ifdef ST7789_RESET
#define LCD_RST  ST7789_RESET
#else
#define LCD_RST  -1
#endif
#endif

#ifndef VSPI_MISO
#ifdef ST7789_MISO
#define VSPI_MISO ST7789_MISO
#else
#define VSPI_MISO -1
#endif
#endif

#ifndef VSPI_MOSI
#ifdef ST7789_SDA
#define VSPI_MOSI ST7789_SDA
#else
#define VSPI_MOSI 17
#endif
#endif

#ifndef VSPI_SCLK
#ifdef ST7789_SCK
#define VSPI_SCLK ST7789_SCK
#else
#define VSPI_SCLK 16
#endif
#endif

#ifndef VSPI_SS
#define VSPI_SS   LCD_CS //cs
#endif

#define LCD_BLK_Clr() digitalWrite(LCD_LED, 0)
#define LCD_BLK_Set()  digitalWrite(LCD_LED, 1)
#define	LCD_DC_SET	digitalWrite(LCD_DC,1)
#define	LCD_DC_CLR	digitalWrite(LCD_DC,0)
#define	LCD_RST_SET	digitalWrite(LCD_RST,1)
#define	LCD_RST_CLR	digitalWrite(LCD_RST,0)

#define	LCD_CS_SET()	digitalWrite(LCD_CS,1)
#define	LCD_CS_CLR()	digitalWrite(LCD_CS,0)

#define WHITE       0xFFFF
#define BLACK      	0x0000
#define BLUE       	0x001F
#define BRED        0XF81F
#define GRED 			 	0XFFE0
#define GBLUE			 	0X07FF
#define RED         0xF800
#define MAGENTA     0xF81F
#define GREEN       0x07E0
#define CYAN        0x7FFF
#define YELLOW      0xFFE0
#define BROWN 			0XBC40 
#define BRRED 			0XFC07
#define GRAY  			0X8430 
#define GRAY0       0xEF7D 
#define GRAY1       0x8410      	
#define GRAY2       0x4208 


#define DARKBLUE      	 0X01CF
#define LIGHTBLUE      	 0X7D7C
#define GRAYBLUE       	 0X5458 

 
#define LIGHTGREEN     	0X841F 
#define LIGHTGRAY     0XEF5B 
#define LGRAY 			 		0XC618 

#define LGRAYBLUE      	0XA651
#define LBBLUE          0X2B12
	    															  
void LCD_Init(void);
void LCD_DisplayOn(void);
void LCD_DisplayOff(void);
void LCD_Clear(u16 Color);	 
void LCD_SetCursor(u16 Xpos, u16 Ypos);
void LCD_DrawPoint(u16 x,u16 y);
u16  LCD_ReadPoint(u16 x,u16 y); 
void LCD_DrawLine(u16 x1, u16 y1, u16 x2, u16 y2);
void LCD_DrawRectangle(u16 x1, u16 y1, u16 x2, u16 y2);		   
void LCD_SetWindows(u16 xStar, u16 yStar,u16 xEnd,u16 yEnd);

u16 LCD_RD_DATA(void);							    
void LCD_WriteReg(u8 LCD_Reg, u16 LCD_RegValue);
void LCD_WR_DATA(u8 data);
u16 LCD_ReadReg(u8 LCD_Reg);
void LCD_WriteRAM_Prepare(void);
void LCD_WriteRAM(u16 RGB_Code);
u16 LCD_ReadRAM(void);		   
u16 LCD_BGR2RGB(u16 c);
void LCD_SetParam(void);
void Lcd_WriteData_16Bit(u16 Data);
void LCD_set_direction(u8 lcd_direction );
void LCD_Fill(u16 sx,u16 sy,u16 ex,u16 ey,u16 color);			 
void LCD_Fill_hv(u16 sx,u16 sy,u16 ex,u16 ey,u16 color);
void LCD_PushImage(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint8_t *data, uint32_t byte_len);
void LCD_Sleep(void);
void LCD_Wakeup(void);
#endif