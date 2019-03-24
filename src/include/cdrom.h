//
// CDROM.H
//

#ifndef __CDROM_H__
#define __CDROM_H__

#include "jaguar.h"

void CDROMInit(void);
void CDROMReset(void);
void CDROMDone(void);

void BUTCHExec(uint32_t cycles);

uint8 CDROMReadByte(uint32_t offset, uint32_t who);
uint16_t CDROMReadWord(uint32_t offset, uint32_t who);
void CDROMWriteByte(uint32_t offset, uint8 data, uint32_t who);
void CDROMWriteWord(uint32_t offset, uint16_t data, uint32_t who);

uint8_t ButchIsReadyToSend(void);
uint16_t GetWordFromButchSSI(uint32_t offset, uint32_t who);
void SetSSIWordsXmittedFromButch(void);

#endif	// __CDROM_H__
