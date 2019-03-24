#ifndef __CLOCK_H__
#define __CLOCK_H__

#include "types.h"

void clock_init(void);
void clock_reset(void);
void clock_done(void);
void clock_byte_write(uint32_t, uint8_t);
void clock_word_write(uint32_t, uint16_t);
uint8_t clock_byte_read(uint32_t);
uint16_t clock_word_read(uint32_t);

#endif
