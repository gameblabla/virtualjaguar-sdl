//
// GUI.H
//
// Graphical User Interface support
//

#ifndef __GUI_H__
#define __GUI_H__

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

void InitGUI(void);
void GUIDone(void);
//void DrawString(int16_t * screen, uint32_t x, uint32_t y, uint8_t invert, const char * text, ...);
//uint8_t GUIMain(void);
uint8_t GUIMain(char *);

uint32_t JaguarLoadROM(uint8_t * rom, char * path);
uint8_t JaguarLoadFile(char * path);

#ifdef __cplusplus
}
#endif

#endif	// __GUI_H__
