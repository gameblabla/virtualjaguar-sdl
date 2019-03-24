//
// OBJECTP.H: Object Processor header file
//

#ifndef __OBJECTP_H__
#define __OBJECTP_H__

#include "types.h"

void op_init(void);
void op_reset(void);
void op_done(void);

void OPProcessList(int scanline, uint8_t render);
uint32_t op_get_list_pointer(void);
void op_set_status_register(uint32_t data);
uint32_t op_get_status_register(void);
void op_set_current_object(uint64_t object);

uint8 OPReadByte(uint32_t, uint32_t who);
uint16_t OPReadWord(uint32_t, uint32_t who);
void OPWriteByte(uint32_t, uint8_t, uint32_t who);
void OPWriteWord(uint32_t, uint16_t, uint32_t who);

#endif	// __OBJECTP_H__
