//
// CDINTF.H: OS agnostic CDROM access funcions
//
// by James L. Hammons
//

#ifndef __CDINTF_H__
#define __CDINTF_H__

#include "types.h"

uint8_t CDIntfInit(void);
void CDIntfDone(void);
uint8_t CDIntfReadBlock(uint32_t, uint8 *);
uint32_t CDIntfGetNumSessions(void);
void CDIntfSelectDrive(uint32);
uint32_t CDIntfGetCurrentDrive(void);
const uint8 * CDIntfGetDriveName(uint32);
uint8 CDIntfGetSessionInfo(uint32_t, uint32);
uint8 CDIntfGetTrackInfo(uint32_t, uint32);

#endif	// __CDINTF_H__
