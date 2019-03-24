//
// DAC.H: Header file
//

#ifndef __DAC_H__
#define __DAC_H__

#include "types.h"

void DACInit(void);
void DACReset(void);
void DACDone(void);

// DAC memory access

void DACWriteByte(uint32_t offset, uint8_t data, uint32_t who);
void DACWriteWord(uint32_t offset, uint16_t data, uint32_t who);
uint8_t DACReadByte(uint32_t offset, uint32_t who);
uint16_t DACReadWord(uint32_t offset, uint32_t who);

// Global variables

extern uint16_t lrxd, rrxd;							// I2S ports (into Jaguar)

#endif	// __DAC_H__
