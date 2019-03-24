#ifndef __ANAJOY_H__
#define __ANAJOY_H__

#include "types.h"

void anajoy_init(void);
void anajoy_reset(void);
void anajoy_done(void);
void anajoy_byte_write(uint32_t, uint8_t);
void anajoy_word_write(uint32_t, uint16_t);
uint8_t anajoy_byte_read(uint32_t);
uint16_t anajoy_word_read(uint32_t);

#endif
