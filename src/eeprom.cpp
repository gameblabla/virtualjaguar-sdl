//
// EEPROM handler
//
// by Cal2
// GCC/SDL port by Niels Wagenaar (Linux/WIN32) and Caz (BeOS)
// Cleanups/enhancements by James L. Hammons
//

#include <stdlib.h>
#include "eeprom.h"
#include "settings.h"

#define eeprom_LOG

static uint16 eeprom_ram[64];

//
// Private function prototypes
//

void EEPROMSave(void);
void eeprom_set_di(uint32 state); 
void eeprom_set_cs(uint32 state);
uint32 eeprom_get_do(void);

enum { EE_STATE_START = 1, EE_STATE_OP_A, EE_STATE_OP_B, EE_STATE_0, EE_STATE_1,
	EE_STATE_2, EE_STATE_3, EE_STATE_0_0, EE_READ_ADDRESS, EE_STATE_0_0_0,
	EE_STATE_0_0_1, EE_STATE_0_0_2, EE_STATE_0_0_3, EE_STATE_0_0_1_0, EE_READ_DATA,
	EE_STATE_BUSY, EE_STATE_1_0, EE_STATE_1_1, EE_STATE_2_0, EE_STATE_3_0 };

// Local global variables

uint16 jerry_ee_state = EE_STATE_START;
uint16 jerry_ee_op = 0;
uint16 jerry_ee_rstate = 0;
uint16 jerry_ee_address_data = 0;
uint16 jerry_ee_address_cnt = 6;
uint16 jerry_ee_data = 0;
uint16 jerry_ee_data_cnt = 16;
uint16 jerry_writes_enabled = 0;
uint16 jerry_ee_direct_jump = 0;
static char eeprom_filename[MAX_PATH];
static bool foundEEPROM = false;

void eeprom_init(void)
{
	sprintf(eeprom_filename, "%s%08X.eep", vjs.EEPROMPath, (unsigned int)jaguar_mainRom_crc32);
	FILE * jerry_ee_fp = fopen(eeprom_filename, "rb");
	if (jerry_ee_fp)
	{
		fread(eeprom_ram, 1, 128, jerry_ee_fp);
		fclose(jerry_ee_fp);
		WriteLog("EEPROM: Loaded from %s\n", eeprom_filename);
		foundEEPROM = true;
	}
	else
		WriteLog("EEPROM: Could not open file \"%s\"!\n", eeprom_filename);
}

void eeprom_reset(void)
{
	if (!foundEEPROM)
		memset(eeprom_ram, 0xFF, 64 * sizeof(uint16));
}

void eeprom_done(void)
{
}

void EEPROMSave(void)
{
	FILE * jerry_ee_fp = fopen(eeprom_filename, "wb");

	if (jerry_ee_fp == NULL)
	{
		WriteLog("EEPROM: Could not create file \"%s!\"\n", eeprom_filename);
		return;
	}

	fwrite(eeprom_ram, 1, 128, jerry_ee_fp);
	fclose(jerry_ee_fp);
}

uint8 eeprom_byte_read(uint32 offset)
{
	switch (offset)
	{
	case 0xF14001:
		return eeprom_get_do();
	case 0xF14801:
		break;
	case 0xF15001:
		eeprom_set_cs(1);
		break;
//	default: WriteLog("eeprom: unmapped 0x%.8x\n",offset); break;
	}

	return 0x00;
}

uint16 eeprom_word_read(uint32 offset)
{
	return ((uint16)eeprom_byte_read(offset+0) << 8) | eeprom_byte_read(offset+1);
}

void eeprom_byte_write(uint32 offset, uint8 data)
{
	switch (offset)
	{
	case 0xF14001:
		break;
	case 0xF14801:
		eeprom_set_di(data & 0x01);
		break;
	case 0xF15001:
		eeprom_set_cs(1);
		break;
//	default: WriteLog("eeprom: unmapped 0x%.8x\n",offset); break;
	}
}

void eeprom_word_write(uint32 offset, uint16 data)
{
	eeprom_byte_write(offset+0, (data >> 8) & 0xFF);
	eeprom_byte_write(offset+1, data & 0xFF);
}

void eeprom_set_di(uint32 data)
{
//	WriteLog("eeprom: di=%i\n",data);
//	WriteLog("eeprom: state %i\n",jerry_ee_state);
	switch (jerry_ee_state)
	{
	case EE_STATE_START:
		jerry_ee_state = EE_STATE_OP_A;
		break;
	case EE_STATE_OP_A:
		jerry_ee_op = (data << 1);
		jerry_ee_state = EE_STATE_OP_B;
		break;
	case EE_STATE_OP_B:
		jerry_ee_op |= data;
		jerry_ee_direct_jump = 0;
//		WriteLog("eeprom: opcode %i\n",jerry_ee_op);
		switch (jerry_ee_op)
		{
		case 0: jerry_ee_state = EE_STATE_0; break;
		case 1: jerry_ee_state = EE_STATE_1; break;
		case 2: jerry_ee_state = EE_STATE_2; break;
		case 3: jerry_ee_state = EE_STATE_3; break;
		}
		eeprom_set_di(data);
		break;
	case EE_STATE_0:
		jerry_ee_rstate = EE_STATE_0_0;
		jerry_ee_state = EE_READ_ADDRESS;
		jerry_ee_direct_jump = 1;
		jerry_ee_address_cnt = 6;
		jerry_ee_address_data = 0;
		break;
	case EE_STATE_0_0:
		switch ((jerry_ee_address_data >> 4) & 0x03)
		{
		case 0: jerry_ee_state=EE_STATE_0_0_0; break;
		case 1: jerry_ee_state=EE_STATE_0_0_1; break;
		case 2: jerry_ee_state=EE_STATE_0_0_2; break;
		case 3: jerry_ee_state=EE_STATE_0_0_3; break;
		}
		eeprom_set_di(data);
		break;
	case EE_STATE_0_0_0:
		// writes disable
		// WriteLog("eeprom: read only\n");
		jerry_writes_enabled = 0;
		jerry_ee_state = EE_STATE_START;
		break;
	case EE_STATE_0_0_1:
		// writes all
		jerry_ee_rstate = EE_STATE_0_0_1_0;
		jerry_ee_state = EE_READ_DATA;
		jerry_ee_data_cnt = 16;
		jerry_ee_data = 0;
		jerry_ee_direct_jump = 1;
		break;
	case EE_STATE_0_0_1_0:
		// WriteLog("eeprom: filling eeprom with 0x%.4x\n",data);
		if (jerry_writes_enabled)
			for(int i=0; i<64; i++)
				eeprom_ram[i] = jerry_ee_data;
		EEPROMSave();								// Save it NOW!
		//else
		//	WriteLog("eeprom: not writing because read only\n");
		jerry_ee_state = EE_STATE_BUSY;
		break;
	case EE_STATE_0_0_2:
		// erase all
		//WriteLog("eeprom: erasing eeprom\n");
		if (jerry_writes_enabled)
			for(int i=0; i<64; i++)
				eeprom_ram[i] = 0xFFFF;

		jerry_ee_state = EE_STATE_BUSY;
		break;
	case EE_STATE_0_0_3:
		// writes enable
		//WriteLog("eeprom: read/write\n");
		jerry_writes_enabled = 1;
		jerry_ee_state = EE_STATE_START;
		break;
	case EE_STATE_1:
		jerry_ee_rstate = EE_STATE_1_0;
		jerry_ee_state = EE_READ_ADDRESS;
		jerry_ee_address_cnt = 6;
		jerry_ee_address_data = 0;
		jerry_ee_direct_jump = 1;
		break;
	case EE_STATE_1_0:
		jerry_ee_rstate = EE_STATE_1_1;
		jerry_ee_state = EE_READ_DATA;
		jerry_ee_data_cnt = 16;
		jerry_ee_data = 0;
		jerry_ee_direct_jump = 1;
		break;
	case EE_STATE_1_1:
		//WriteLog("eeprom: writing 0x%.4x at 0x%.2x\n",jerry_ee_data,jerry_ee_address_data);
		if (jerry_writes_enabled)
			eeprom_ram[jerry_ee_address_data] = jerry_ee_data;
		EEPROMSave();								// Save it NOW!
		jerry_ee_state = EE_STATE_BUSY;
		break;
	case EE_STATE_2:
		jerry_ee_rstate = EE_STATE_2_0;
		jerry_ee_state = EE_READ_ADDRESS;
		jerry_ee_address_cnt = 6;
		jerry_ee_address_data = 0;
		jerry_ee_data_cnt = 16;
		jerry_ee_data = 0;
		break;
	case EE_STATE_3:
		jerry_ee_rstate = EE_STATE_3_0;
		jerry_ee_state = EE_READ_ADDRESS;
		jerry_ee_address_cnt = 6;
		jerry_ee_address_data = 0;
		jerry_ee_direct_jump = 1;
		break;
	case EE_STATE_3_0:
		//WriteLog("eeprom: erasing 0x%.2x\n",jerry_ee_address_data);
		if (jerry_writes_enabled)
			eeprom_ram[jerry_ee_address_data] = 0xFFFF;
		jerry_ee_state = EE_STATE_BUSY;
		break;
	case EE_READ_DATA: 
		//WriteLog("eeprom:\t\t\t%i bit %i\n",data,jerry_ee_data_cnt-1);
		jerry_ee_data <<= 1;
		jerry_ee_data |= data;
		jerry_ee_data_cnt--;
		if (!jerry_ee_data_cnt)
		{
			jerry_ee_state = jerry_ee_rstate;
			if (jerry_ee_direct_jump)
				eeprom_set_di(data);
		}
		break;
	case EE_READ_ADDRESS: 
		jerry_ee_address_data <<= 1;
		jerry_ee_address_data |= data;
		jerry_ee_address_cnt--;
//		WriteLog("eeprom:\t%i bits remaining\n",jerry_ee_address_cnt);
		if (!jerry_ee_address_cnt)
		{
			jerry_ee_state = jerry_ee_rstate;
			//WriteLog("eeprom:\t\tread address 0x%.2x\n",jerry_ee_address_data);
			if (jerry_ee_direct_jump)
				eeprom_set_di(data);
		}
		break;
	default:
		jerry_ee_state = EE_STATE_OP_A; 
	}
}

void eeprom_set_cs(uint32 state)
{
//	WriteLog("eeprom: cs=%i\n",state);
	jerry_ee_state = EE_STATE_START;
	jerry_ee_op = 0;
	jerry_ee_rstate = 0;
	jerry_ee_address_data = 0;
	jerry_ee_address_cnt = 6;
	jerry_ee_data = 0;
	jerry_ee_data_cnt = 16;
	jerry_writes_enabled = 1;
}

uint32 eeprom_get_do(void)
{
	uint16 data = 1;

	switch (jerry_ee_state)
	{
	case EE_STATE_START:
		data = 1;
		break;
	case EE_STATE_BUSY:
		jerry_ee_state = EE_STATE_START;
		data = 0;
		break;
	case EE_STATE_2_0:
		jerry_ee_data_cnt--;
		data = (eeprom_ram[jerry_ee_address_data] & (1 << jerry_ee_data_cnt)) >> jerry_ee_data_cnt;
		if (!jerry_ee_data_cnt)
		{
			//WriteLog("eeprom: read 0x%.4x at 0x%.2x cpu %i pc=0x%.8x\n",eeprom_ram[jerry_ee_address_data],jerry_ee_address_data,jaguar_cpu_in_exec,s68000readPC());
			jerry_ee_state = EE_STATE_START;
		}
		break;
	}

//	WriteLog("eeprom: do=%i\n",data);
	return data;
}
