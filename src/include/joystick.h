#ifndef __JOYSTICK_H__
#define __JOYSTICK_H__

#include "types.h"

void joystick_init(void);
void joystick_reset(void);
void joystick_done(void);
void joystick_byte_write(uint32_t, uint8_t);
void joystick_word_write(uint32_t, uint16_t);
uint8_t joystick_byte_read(uint32_t);
uint16_t joystick_word_read(uint32_t);
void joystick_exec(void);

#endif
