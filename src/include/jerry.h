//
// JERRY.H: Header file
//

#ifndef __JERRY_H__
#define __JERRY_H__

#include "jaguar.h"

void jerry_init(void);
void jerry_reset(void);
void jerry_done(void);

uint8_t JERRYReadByte(uint32_t offset, uint32_t who);
uint16_t JERRYReadWord(uint32_t offset, uint32_t who);
void JERRYWriteByte(uint32_t offset, uint8_t data, uint32_t who);
void JERRYWriteWord(uint32_t offset, uint16_t data, uint32_t who);

void JERRYExecPIT(uint32_t cycles);
void jerry_i2s_exec(uint32_t cycles);

// 68000 Interrupt bit positions (enabled at $F10020)

enum { IRQ2_EXTERNAL = 0, IRQ2_DSP, IRQ2_TIMER1, IRQ2_TIMER2, IRQ2_ASI, IRQ2_SSI };

uint8_t JERRYIRQEnabled(int irq);
void JERRYSetPendingIRQ(int irq);

#endif
