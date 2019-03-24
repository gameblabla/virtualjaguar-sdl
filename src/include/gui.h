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

extern void InitGUI(void);
extern void GUIDone(void);
extern uint8_t GUIMain(char *);

extern uint32_t JaguarLoadROM(uint8_t * rom, char * path);
extern uint8_t JaguarLoadFile(char * path);

#ifdef __cplusplus
}
#endif

#endif	// __GUI_H__
