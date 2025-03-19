#ifndef EPD_H
#define EPD_H

#include <stddef.h>
#include <stdint.h>

#define EPD_WIDTH 240
#define EPD_HEIGHT 360

#define EPD_WHITE 0xFF
#define EPD_BLACK 0x00
#define EPD_Source_Line 0xAA
#define EPD_Gate_Line 0x55
#define EPD_UP_BLACK_DOWN_WHITE 0xF0
#define EPD_LEFT_BLACK_RIGHT_WHITE 0x0F
#define EPD_Frame 0x01
#define EPD_Crosstalk 0x02
#define EPD_Chessboard 0x03
#define EPD_Image 0x04

extern unsigned char EPD_Flag;

void EPD_Reset(void);
void EPD_SendCommand(uint8_t Reg);
void EPD_SendData(uint8_t Data);
void EPD_refresh(void);
void EPD_lut_GC(void);
void EPD_lut_DU(void);
void EPD_Init(void);
void EPD_display(uint8_t *picData);
void EPD_display_NUM(uint8_t NUM);
void EPD_Clear(void);
void EPD_sleep(void);

#endif