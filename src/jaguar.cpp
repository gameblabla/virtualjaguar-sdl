//
// JAGUAR.CPP
//
// Originally by David Raingeard (Cal2)
// GCC/SDL port by Niels Wagenaar (Linux/WIN32) and Carwin Jones (BeOS)
// Cleanups and endian wrongness amelioration by James L. Hammons
// Note: Endian wrongness probably stems from the MAME origins of this emu and
//       the braindead way in which MAME handles memory. :-)
// 

#include "jaguar.h"
#include "video.h"
#include "settings.h"
#include <string.h>
#include <stdint.h>	
//#include "m68kdasmAG.h"

//#define CPU_DEBUG
//Do this in makefile??? Yes! Could, but it's easier to define here...
//#define LOG_UNMAPPED_MEMORY_ACCESSES
//#define ABORT_ON_UNMAPPED_MEMORY_ACCESS
//#define ABORT_ON_ILLEGAL_INSTRUCTIONS
//#define ABORT_ON_OFFICIAL_ILLEGAL_INSTRUCTION
//#define CPU_DEBUG_MEMORY

// Private function prototypes

unsigned jaguar_unknown_readbyte(unsigned address, uint32 who = UNKNOWN);
unsigned jaguar_unknown_readword(unsigned address, uint32 who = UNKNOWN);
void jaguar_unknown_writebyte(unsigned address, unsigned data, uint32 who = UNKNOWN);
void jaguar_unknown_writeword(unsigned address, unsigned data, uint32 who = UNKNOWN);
void M68K_show_context(void);

// External variables

//extern bool hardwareTypeNTSC;						// Set to false for PAL
#ifdef CPU_DEBUG_MEMORY
extern bool startMemLog;							// Set by "e" key
extern int effect_start;
extern int effect_start2, effect_start3, effect_start4, effect_start5, effect_start6;
#endif

// Memory debugging identifiers

char * whoName[9] =
	{ "Unknown", "Jaguar", "DSP", "GPU", "TOM", "JERRY", "M68K", "Blitter", "OP" };

uint32 jaguar_active_memory_dumps = 0;

uint32 jaguar_mainRom_crc32, jaguarRomSize, jaguarRunAddress;

/*static*/ uint8 * jaguar_mainRam = NULL;
/*static*/ uint8 * jaguar_mainRom = NULL;
/*static*/ uint8 * jaguar_bootRom = NULL;
/*static*/ uint8 * jaguar_CDBootROM = NULL;

#ifdef CPU_DEBUG_MEMORY
uint8 writeMemMax[0x400000], writeMemMin[0x400000];
uint8 readMem[0x400000];
uint32 returnAddr[4000], raPtr = 0xFFFFFFFF;
#endif

uint32 pcQueue[0x400];
uint32 pcQPtr = 0;

//
// Callback function to detect illegal instructions
//
void GPUDumpDisassembly(void);
void GPUDumpRegisters(void);

void M68KInstructionHook(void)
{
}

//
// Musashi 68000 read/write/IRQ functions
//

int irq_ack_handler(int level)
{
	int vector = M68K_INT_ACK_AUTOVECTOR;

	// The GPU/DSP/etc are probably *not* issuing an NMI, but it seems to work OK...

	if (level == 7)
	{
		m68k_set_irq(0);						// Clear the IRQ...
		vector = 64;							// Set user interrupt #0
	}

	return vector;
}

unsigned int m68k_read_memory_8(unsigned int address)
{
#ifdef CPU_DEBUG_MEMORY
	if ((address >= 0x000000) && (address <= 0x3FFFFF))
	{
		if (startMemLog)
			readMem[address] = 1;
	}
#endif
////WriteLog("[RM8] Addr: %08X\n", address);
//; So, it seems that it stores the returned DWORD at $51136 and $FB074.
/*	if (address == 0x51136 || address == 0x51138 || address == 0xFB074 || address == 0xFB076
		|| address == 0x1AF05E)
		//WriteLog("[RM8  PC=%08X] Addr: %08X, val: %02X\n", m68k_get_reg(NULL, M68K_REG_PC), address, jaguar_mainRam[address]);//*/
	unsigned int retVal = 0;

	if ((address >= 0x000000) && (address <= 0x3FFFFF))
		retVal = jaguar_mainRam[address];
//	else if ((address >= 0x800000) && (address <= 0xDFFFFF))
	else if ((address >= 0x800000) && (address <= 0xDFFEFF))
		retVal = jaguar_mainRom[address - 0x800000];
	else if ((address >= 0xE00000) && (address <= 0xE3FFFF))
		retVal = jaguar_bootRom[address - 0xE00000];
#ifdef CDROM_EMU
	else if ((address >= 0xDFFF00) && (address <= 0xDFFFFF))
		retVal = CDROMReadByte(address);
#endif
	else if ((address >= 0xF00000) && (address <= 0xF0FFFF))
		retVal = TOMReadByte(address, M68K);
	else if ((address >= 0xF10000) && (address <= 0xF1FFFF))
		retVal = JERRYReadByte(address, M68K);
	else
		retVal = jaguar_unknown_readbyte(address, M68K);

//if (address >= 0x2800 && address <= 0x281F)
//	//WriteLog("M68K: Read byte $%02X at $%08X [PC=%08X]\n", retVal, address, m68k_get_reg(NULL, M68K_REG_PC));
//if (address >= 0x8B5E4 && address <= 0x8B5E4 + 16)
//	//WriteLog("M68K: Read byte $%02X at $%08X [PC=%08X]\n", retVal, address, m68k_get_reg(NULL, M68K_REG_PC));
    return retVal;
}

void gpu_dump_disassembly(void);
void gpu_dump_registers(void);

unsigned int m68k_read_memory_16(unsigned int address)
{
#ifdef CPU_DEBUG_MEMORY
/*	if ((address >= 0x000000) && (address <= 0x3FFFFE))
	{
		if (startMemLog)
			readMem[address] = 1, readMem[address + 1] = 1;
	}//*/
/*	if (effect_start && (address >= 0x8064FC && address <= 0x806501))
	{
		return 0x4E71;	// NOP
	}
	if (effect_start2 && (address >= 0x806502 && address <= 0x806507))
	{
		return 0x4E71;	// NOP
	}
	if (effect_start3 && (address >= 0x806512 && address <= 0x806517))
	{
		return 0x4E71;	// NOP
	}
	if (effect_start4 && (address >= 0x806524 && address <= 0x806527))
	{
		return 0x4E71;	// NOP
	}
	if (effect_start5 && (address >= 0x80653E && address <= 0x806543)) //Collision detection!
	{
		return 0x4E71;	// NOP
	}
	if (effect_start6 && (address >= 0x806544 && address <= 0x806547))
	{
		return 0x4E71;	// NOP
	}//*/
#endif
////WriteLog("[RM16] Addr: %08X\n", address);
/*if (m68k_get_reg(NULL, M68K_REG_PC) == 0x00005FBA)
//	for(int i=0; i<10000; i++)
	//WriteLog("[M68K] In routine #6!\n");//*/
//if (m68k_get_reg(NULL, M68K_REG_PC) == 0x00006696) // GPU Program #4
//if (m68k_get_reg(NULL, M68K_REG_PC) == 0x00005B3C)	// GPU Program #2
/*if (m68k_get_reg(NULL, M68K_REG_PC) == 0x00005BA8)	// GPU Program #3
{
	//WriteLog("[M68K] About to run GPU! (Addr:%08X, data:%04X)\n", address, TOMReadWord(address));
	gpu_dump_registers();
	gpu_dump_disassembly();
//	for(int i=0; i<10000; i++)
//		//WriteLog("[M68K] About to run GPU!\n");
}//*/
////WriteLog("[WM8  PC=%08X] Addr: %08X, val: %02X\n", m68k_get_reg(NULL, M68K_REG_PC), address, value);
/*if (m68k_get_reg(NULL, M68K_REG_PC) >= 0x00006696 && m68k_get_reg(NULL, M68K_REG_PC) <= 0x000066A8)
{
	if (address == 0x000066A0)
	{
		gpu_dump_registers();
		gpu_dump_disassembly();
	}
	for(int i=0; i<10000; i++)
		//WriteLog("[M68K] About to run GPU! (Addr:%08X, data:%04X)\n", address, TOMReadWord(address));
}//*/
//; So, it seems that it stores the returned DWORD at $51136 and $FB074.
/*	if (address == 0x51136 || address == 0x51138 || address == 0xFB074 || address == 0xFB076
		|| address == 0x1AF05E)
		//WriteLog("[RM16  PC=%08X] Addr: %08X, val: %04X\n", m68k_get_reg(NULL, M68K_REG_PC), address, GET16(jaguar_mainRam, address));//*/
    unsigned int retVal = 0;

	if ((address >= 0x000000) && (address <= 0x3FFFFE))
//		retVal = (jaguar_mainRam[address] << 8) | jaguar_mainRam[address+1];
		retVal = GET16(jaguar_mainRam, address);
//	else if ((address >= 0x800000) && (address <= 0xDFFFFE))
	else if ((address >= 0x800000) && (address <= 0xDFFEFE))
		retVal = (jaguar_mainRom[address - 0x800000] << 8) | jaguar_mainRom[address - 0x800000 + 1];
	else if ((address >= 0xE00000) && (address <= 0xE3FFFE))
		retVal = (jaguar_bootRom[address - 0xE00000] << 8) | jaguar_bootRom[address - 0xE00000 + 1];
#ifdef CDROM_EMU
	else if ((address >= 0xDFFF00) && (address <= 0xDFFFFE))
		retVal = CDROMReadWord(address, M68K);
#endif
	else if ((address >= 0xF00000) && (address <= 0xF0FFFE))
		retVal = TOMReadWord(address, M68K);
	else if ((address >= 0xF10000) && (address <= 0xF1FFFE))
		retVal = JERRYReadWord(address, M68K);
	else
		retVal = jaguar_unknown_readword(address, M68K);

//if (address >= 0xF1B000 && address <= 0xF1CFFF)
//	//WriteLog("M68K: Read word $%04X at $%08X [PC=%08X]\n", retVal, address, m68k_get_reg(NULL, M68K_REG_PC));
//if (address >= 0x2800 && address <= 0x281F)
//	//WriteLog("M68K: Read word $%04X at $%08X [PC=%08X]\n", retVal, address, m68k_get_reg(NULL, M68K_REG_PC));
//$8B3AE -> Transferred from $F1C010
//$8B5E4 -> Only +1 read at $808AA
//if (address >= 0x8B5E4 && address <= 0x8B5E4 + 16)
//	//WriteLog("M68K: Read word $%04X at $%08X [PC=%08X]\n", retVal, address, m68k_get_reg(NULL, M68K_REG_PC));
    return retVal;
}

unsigned int m68k_read_memory_32(unsigned int address)
{
//; So, it seems that it stores the returned DWORD at $51136 and $FB074.
/*	if (address == 0x51136 || address == 0xFB074 || address == 0x1AF05E)
		//WriteLog("[RM32  PC=%08X] Addr: %08X, val: %08X\n", m68k_get_reg(NULL, M68K_REG_PC), address, (m68k_read_memory_16(address) << 16) | m68k_read_memory_16(address + 2));//*/

////WriteLog("--> [RM32]\n");
    return (m68k_read_memory_16(address) << 16) | m68k_read_memory_16(address + 2);
}

void m68k_write_memory_8(unsigned int address, unsigned int value)
{
#ifdef CPU_DEBUG_MEMORY
	if ((address >= 0x000000) && (address <= 0x3FFFFF))
	{
		if (startMemLog)
		{
			if (value > writeMemMax[address])
				writeMemMax[address] = value;
			if (value < writeMemMin[address])
				writeMemMin[address] = value;
		}
	}
#endif
//if ((address >= 0x1FF020 && address <= 0x1FF03F) || (address >= 0x1FF820 && address <= 0x1FF83F))
//	//WriteLog("M68K: Writing %02X at %08X\n", value, address);
////WriteLog("[WM8  PC=%08X] Addr: %08X, val: %02X\n", m68k_get_reg(NULL, M68K_REG_PC), address, value);
/*if (effect_start)
	if (address >= 0x18FA70 && address < (0x18FA70 + 8000))
		//WriteLog("M68K: Byte %02X written at %08X by 68K\n", value, address);//*/

	if ((address >= 0x000000) && (address <= 0x3FFFFF))
		jaguar_mainRam[address] = value;
#ifdef CDROM_EMU
	else if ((address >= 0xDFFF00) && (address <= 0xDFFFFF))
		CDROMWriteByte(address, value, M68K);
#endif
	else if ((address >= 0xF00000) && (address <= 0xF0FFFF))
		TOMWriteByte(address, value, M68K);
	else if ((address >= 0xF10000) && (address <= 0xF1FFFF))
		JERRYWriteByte(address, value, M68K);
	else
		jaguar_unknown_writebyte(address, value, M68K);
}

void m68k_write_memory_16(unsigned int address, unsigned int value)
{
#ifdef CPU_DEBUG_MEMORY
	if ((address >= 0x000000) && (address <= 0x3FFFFE))
	{
		if (startMemLog)
		{
			uint8 hi = value >> 8, lo = value & 0xFF;

			if (hi > writeMemMax[address])
				writeMemMax[address] = hi;
			if (hi < writeMemMin[address])
				writeMemMin[address] = hi;

			if (lo > writeMemMax[address+1])
				writeMemMax[address+1] = lo;
			if (lo < writeMemMin[address+1])
				writeMemMin[address+1] = lo;
		}
	}
#endif
//if ((address >= 0x1FF020 && address <= 0x1FF03F) || (address >= 0x1FF820 && address <= 0x1FF83F))
//	//WriteLog("M68K: Writing %04X at %08X\n", value, address);
////WriteLog("[WM16 PC=%08X] Addr: %08X, val: %04X\n", m68k_get_reg(NULL, M68K_REG_PC), address, value);
//if (address >= 0xF02200 && address <= 0xF0229F)
//	//WriteLog("M68K: Writing to blitter --> %04X at %08X\n", value, address);
//if (address >= 0x0E75D0 && address <= 0x0E75E7)
//	//WriteLog("M68K: Writing %04X at %08X, M68K PC=%08X\n", value, address, m68k_get_reg(NULL, M68K_REG_PC));
/*extern uint32 totalFrames;
if (address == 0xF02114)
	//WriteLog("M68K: Writing to GPU_CTRL (frame:%u)... [M68K PC:%08X]\n", totalFrames, m68k_get_reg(NULL, M68K_REG_PC));
if (address == 0xF02110)
	//WriteLog("M68K: Writing to GPU_PC (frame:%u)... [M68K PC:%08X]\n", totalFrames, m68k_get_reg(NULL, M68K_REG_PC));//*/
//if (address >= 0xF03B00 && address <= 0xF03DFF)
//	//WriteLog("M68K: Writing %04X to %08X...\n", value, address);

/*if (address == 0x0100)//64*4)
	//WriteLog("M68K: Wrote word to VI vector value %04X...\n", value);//*/
/*if (effect_start)
	if (address >= 0x18FA70 && address < (0x18FA70 + 8000))
		//WriteLog("M68K: Word %04X written at %08X by 68K\n", value, address);//*/
/*	if (address == 0x51136 || address == 0x51138 || address == 0xFB074 || address == 0xFB076
		|| address == 0x1AF05E)
		//WriteLog("[WM16  PC=%08X] Addr: %08X, val: %04X\n", m68k_get_reg(NULL, M68K_REG_PC), address, value);//*/

	if ((address >= 0x000000) && (address <= 0x3FFFFE))
	{
/*		jaguar_mainRam[address] = value >> 8;
		jaguar_mainRam[address + 1] = value & 0xFF;*/
		SET16(jaguar_mainRam, address, value);
	}
#ifdef CDROM_EMU
	else if ((address >= 0xDFFF00) && (address <= 0xDFFFFE))
		CDROMWriteWord(address, value, M68K);
#endif
	else if ((address >= 0xF00000) && (address <= 0xF0FFFE))
		TOMWriteWord(address, value, M68K);
	else if ((address >= 0xF10000) && (address <= 0xF1FFFE))
		JERRYWriteWord(address, value, M68K);
	else
	{
		jaguar_unknown_writeword(address, value, M68K);
#ifdef LOG_UNMAPPED_MEMORY_ACCESSES
		//WriteLog("\tA0=%08X, A1=%08X, D0=%08X, D1=%08X\n",
			m68k_get_reg(NULL, M68K_REG_A0), m68k_get_reg(NULL, M68K_REG_A1),
			m68k_get_reg(NULL, M68K_REG_D0), m68k_get_reg(NULL, M68K_REG_D1));
#endif
	}
}

void m68k_write_memory_32(unsigned int address, unsigned int value)
{
////WriteLog("--> [WM32]\n");
/*if (address == 0x0100)//64*4)
	//WriteLog("M68K: Wrote dword to VI vector value %08X...\n", value);//*/
/*if (address >= 0xF03214 && address < 0xF0321F)
	//WriteLog("M68K: Writing DWORD (%08X) to GPU RAM (%08X)...\n", value, address);//*/
//M68K: Writing DWORD (88E30047) to GPU RAM (00F03214)...
/*extern bool doGPUDis;
if (address == 0xF03214 && value == 0x88E30047)
//	start = true;
	doGPUDis = true;//*/
/*	if (address == 0x51136 || address == 0xFB074)
		//WriteLog("[WM32  PC=%08X] Addr: %08X, val: %02X\n", m68k_get_reg(NULL, M68K_REG_PC), address, value);//*/

	m68k_write_memory_16(address, value >> 16);
	m68k_write_memory_16(address + 2, value & 0xFFFF);
}


uint32 jaguar_get_handler(uint32 i)
{
	return JaguarReadLong(i * 4);
}

uint32 jaguar_interrupt_handler_is_valid(uint32 i)
{
	uint32 handler = jaguar_get_handler(i);
	if (handler && (handler != 0xFFFFFFFF))
		return 1;
	else
		return 0;
}

void M68K_show_context(void)
{
}

//
// Unknown read/write byte/word routines
//

// It's hard to believe that developers would be sloppy with their memory writes, yet in
// some cases the developers screwed up royal. E.g., Club Drive has the following code:
//
// 807EC4: movea.l #$f1b000, A1
// 807ECA: movea.l #$8129e0, A0
// 807ED0: move.l  A0, D0
// 807ED2: move.l  #$f1bb94, D1
// 807ED8: sub.l   D0, D1
// 807EDA: lsr.l   #2, D1
// 807EDC: move.l  (A0)+, (A1)+
// 807EDE: dbra    D1, 807edc
//
// The problem is at $807ED0--instead of putting A0 into D0, they really meant to put A1
// in. This mistake causes it to try and overwrite approximately $700000 worth of address
// space! (That is, unless the 68K causes a bus error...)

void jaguar_unknown_writebyte(unsigned address, unsigned data, uint32 who/*=UNKNOWN*/)
{
#ifdef LOG_UNMAPPED_MEMORY_ACCESSES
	//WriteLog("Jaguar: Unknown byte %02X written at %08X by %s (M68K PC=%06X)\n", data, address, whoName[who], m68k_get_reg(NULL, M68K_REG_PC));
#endif
#ifdef ABORT_ON_UNMAPPED_MEMORY_ACCESS
	extern bool finished;
	finished = true;
	extern bool doDSPDis;
	if (who == DSP)
		doDSPDis = true;
#endif
}

void jaguar_unknown_writeword(unsigned address, unsigned data, uint32 who/*=UNKNOWN*/)
{
#ifdef LOG_UNMAPPED_MEMORY_ACCESSES
	//WriteLog("Jaguar: Unknown word %04X written at %08X by %s (M68K PC=%06X)\n", data, address, whoName[who], m68k_get_reg(NULL, M68K_REG_PC));
#endif
#ifdef ABORT_ON_UNMAPPED_MEMORY_ACCESS
	extern bool finished;
	finished = true;
	extern bool doDSPDis;
	if (who == DSP)
		doDSPDis = true;
#endif
}

unsigned jaguar_unknown_readbyte(unsigned address, uint32 who/*=UNKNOWN*/)
{
#ifdef LOG_UNMAPPED_MEMORY_ACCESSES
	//WriteLog("Jaguar: Unknown byte read at %08X by %s (M68K PC=%06X)\n", address, whoName[who], m68k_get_reg(NULL, M68K_REG_PC));
#endif
#ifdef ABORT_ON_UNMAPPED_MEMORY_ACCESS
	extern bool finished;
	finished = true;
	extern bool doDSPDis;
	if (who == DSP)
		doDSPDis = true;
#endif
    return 0xFF;
}

unsigned jaguar_unknown_readword(unsigned address, uint32 who/*=UNKNOWN*/)
{
#ifdef LOG_UNMAPPED_MEMORY_ACCESSES
	//WriteLog("Jaguar: Unknown word read at %08X by %s (M68K PC=%06X)\n", address, whoName[who], m68k_get_reg(NULL, M68K_REG_PC));
#endif
#ifdef ABORT_ON_UNMAPPED_MEMORY_ACCESS
	extern bool finished;
	finished = true;
	extern bool doDSPDis;
	if (who == DSP)
		doDSPDis = true;
#endif
    return 0xFFFF;
}

uint8 JaguarReadByte(uint32 offset, uint32 who/*=UNKNOWN*/)
{
	uint8 data = 0x00;

	offset &= 0xFFFFFF;
	if (offset < 0x400000)
		data = jaguar_mainRam[offset & 0x3FFFFF];
	else if ((offset >= 0x800000) && (offset < 0xC00000))
		data = jaguar_mainRom[offset - 0x800000];
#ifdef CDROM_EMU
	else if ((offset >= 0xDFFF00) && (offset <= 0xDFFFFF))
		data = CDROMReadByte(offset, who);
#endif
	else if ((offset >= 0xE00000) && (offset < 0xE40000))
		data = jaguar_bootRom[offset & 0x3FFFF];
	else if ((offset >= 0xF00000) && (offset < 0xF10000))
		data = TOMReadByte(offset, who);
	else if ((offset >= 0xF10000) && (offset < 0xF20000))
		data = JERRYReadByte(offset, who);
	else
		data = jaguar_unknown_readbyte(offset, who);

	return data;
}

uint16 JaguarReadWord(uint32 offset, uint32 who/*=UNKNOWN*/)
{
	offset &= 0xFFFFFF;
	if (offset <= 0x3FFFFE)
	{
		return (jaguar_mainRam[(offset+0) & 0x3FFFFF] << 8) | jaguar_mainRam[(offset+1) & 0x3FFFFF];
	}
	else if ((offset >= 0x800000) && (offset <= 0xBFFFFE))
	{
		offset -= 0x800000;
		return (jaguar_mainRom[offset+0] << 8) | jaguar_mainRom[offset+1];
	}
//	else if ((offset >= 0xDFFF00) && (offset < 0xDFFF00))
#ifdef CDROM_EMU
	else if ((offset >= 0xDFFF00) && (offset <= 0xDFFFFE))
		return CDROMReadWord(offset, who);
#endif
	else if ((offset >= 0xE00000) && (offset <= 0xE3FFFE))
		return (jaguar_bootRom[(offset+0) & 0x3FFFF] << 8) | jaguar_bootRom[(offset+1) & 0x3FFFF];
	else if ((offset >= 0xF00000) && (offset <= 0xF0FFFE))
		return TOMReadWord(offset, who);
	else if ((offset >= 0xF10000) && (offset <= 0xF1FFFE))
		return JERRYReadWord(offset, who);

	return jaguar_unknown_readword(offset, who);
}

void JaguarWriteByte(uint32 offset, uint8 data, uint32 who/*=UNKNOWN*/)
{
//Need to check for writes in the range of $18FA70 + 8000...
/*if (effect_start)
	if (offset >= 0x18FA70 && offset < (0x18FA70 + 8000))
		//WriteLog("JWB: Byte %02X written at %08X by %s\n", data, offset, whoName[who]);//*/

	offset &= 0xFFFFFF;
	if (offset < 0x400000)
	{
		jaguar_mainRam[offset & 0x3FFFFF] = data;
		return;
	}
#ifdef CDROM_EMU
	else if ((offset >= 0xDFFF00) && (offset <= 0xDFFFFF))
	{
		CDROMWriteByte(offset, data, who);
		return;
	}
#endif
	else if ((offset >= 0xF00000) && (offset <= 0xF0FFFF))
	{
		TOMWriteByte(offset, data, who);
		return;
	}
	else if ((offset >= 0xF10000) && (offset <= 0xF1FFFF))
	{
		JERRYWriteByte(offset, data, who);
		return;
	}
    
	jaguar_unknown_writebyte(offset, data, who);
}

uint32 starCount;
void JaguarWriteWord(uint32 offset, uint16 data, uint32 who/*=UNKNOWN*/)
{
/*if (offset == 0x0100)//64*4)
	//WriteLog("M68K: %s wrote word to VI vector value %04X...\n", whoName[who], data);
if (offset == 0x0102)//64*4)
	//WriteLog("M68K: %s wrote word to VI vector+2 value %04X...\n", whoName[who], data);//*/
//TEMP--Mirror of F03000? Yes, but only 32-bit CPUs can do it (i.e., NOT the 68K!)
// PLUS, you would handle this in the GPU/DSP WriteLong code! Not here!
//Need to check for writes in the range of $18FA70 + 8000...
/*if (effect_start)
	if (offset >= 0x18FA70 && offset < (0x18FA70 + 8000))
		//WriteLog("JWW: Word %04X written at %08X by %s\n", data, offset, whoName[who]);//*/
/*if (offset >= 0x2C00 && offset <= 0x2CFF)
	//WriteLog("Jaguar: Word %04X written to TOC+%02X by %s\n", data, offset-0x2C00, whoName[who]);//*/

	offset &= 0xFFFFFF;

	if (offset <= 0x3FFFFE)
	{
/*
GPU Table (CD BIOS)

1A 69 F0 ($0000) -> Starfield
1A 73 C8 ($0001) -> Final clearing blit & bitmap blit?
1A 79 F0 ($0002)
1A 88 C0 ($0003)
1A 8F E8 ($0004) -> "Jaguar" small color logo?
1A 95 20 ($0005)
1A 9F 08 ($0006)
1A A1 38 ($0007)
1A AB 38 ($0008)
1A B3 C8 ($0009)
1A B9 C0 ($000A)
*/

//This MUST be done by the 68K!
/*if (offset == 0x670C)
	//WriteLog("Jaguar: %s writing to location $670C...\n", whoName[who]);*/

/*extern bool doGPUDis;
//if ((offset == 0x100000 + 75522) && who == GPU)	// 76,226 -> 75522
if ((offset == 0x100000 + 128470) && who == GPU)	// 107,167 -> 128470 (384 x 250 screen size 16BPP)
//if ((offset >= 0x100000 && offset <= 0x12C087) && who == GPU)
	doGPUDis = true;//*/
/*if (offset == 0x100000 + 128470) // 107,167 -> 128470 (384 x 250 screen size 16BPP)
	//WriteLog("JWW: Writing value %04X at %08X by %s...\n", data, offset, whoName[who]);
if ((data & 0xFF00) != 0x7700)
	//WriteLog("JWW: Writing value %04X at %08X by %s...\n", data, offset, whoName[who]);//*/
/*if ((offset >= 0x100000 && offset <= 0x147FFF) && who == GPU)
	return;//*/
/*if ((data & 0xFF00) != 0x7700 && who == GPU)
	//WriteLog("JWW: Writing value %04X at %08X by %s...\n", data, offset, whoName[who]);//*/
/*if ((offset >= 0x100000 + 0x48000 && offset <= 0x12C087 + 0x48000) && who == GPU)
	return;//*/
/*extern bool doGPUDis;
if (offset == 0x120216 && who == GPU)
	doGPUDis = true;//*/
/*extern uint32 gpu_pc;
if (who == GPU && (gpu_pc == 0xF03604 || gpu_pc == 0xF03638))
{
	uint32 base = offset - (offset > 0x148000 ? 0x148000 : 0x100000);
	uint32 y = base / 0x300;
	uint32 x = (base - (y * 0x300)) / 2;
	//WriteLog("JWW: Writing starfield star %04X at %08X (%u/%u) [%s]\n", data, offset, x, y, (gpu_pc == 0xF03604 ? "s" : "L"));
}//*/
/*
JWW: Writing starfield star 775E at 0011F650 (555984/1447)
*/
//if (offset == (0x001E17F8 + 0x34))
/*if (who == GPU && offset == (0x001E17F8 + 0x34))
	data = 0xFE3C;//*/
//	//WriteLog("JWW: Write at %08X written to by %s.\n", 0x001E17F8 + 0x34, whoName[who]);//*/
/*extern uint32 gpu_pc;
if (who == GPU && (gpu_pc == 0xF03604 || gpu_pc == 0xF03638))
{
	extern int objectPtr;
//	if (offset > 0x148000)
//		return;
	starCount++;
	if (starCount > objectPtr)
		return;

//	if (starCount == 1)
//		//WriteLog("--> Drawing 1st star...\n");
//
//	uint32 base = offset - (offset > 0x148000 ? 0x148000 : 0x100000);
//	uint32 y = base / 0x300;
//	uint32 x = (base - (y * 0x300)) / 2;
//	//WriteLog("JWW: Writing starfield star %04X at %08X (%u/%u) [%s]\n", data, offset, x, y, (gpu_pc == 0xF03604 ? "s" : "L"));

//A star of interest...
//-->JWW: Writing starfield star 77C9 at 0011D31A (269/155) [s]
//1st trail +3(x), -1(y) -> 272, 154 -> 0011D020
//JWW: Blitter writing echo 77B3 at 0011D022...
}//*/
//extern bool doGPUDis;
/*if (offset == 0x11D022 + 0x48000 || offset == 0x11D022)// && who == GPU)
{
//	doGPUDis = true;
	//WriteLog("JWW: %s writing echo %04X at %08X...\n", whoName[who], data, offset);
//	LogBlit();
}
if (offset == 0x11D31A + 0x48000 || offset == 0x11D31A)
	//WriteLog("JWW: %s writing star %04X at %08X...\n", whoName[who], data, offset);//*/

		jaguar_mainRam[(offset+0) & 0x3FFFFF] = data >> 8;
		jaguar_mainRam[(offset+1) & 0x3FFFFF] = data & 0xFF;
		return;
	}
#ifdef CDROM_EMU
	else if (offset >= 0xDFFF00 && offset <= 0xDFFFFE)
	{
		CDROMWriteWord(offset, data, who);
		return;
	}
#endif
	else if (offset >= 0xF00000 && offset <= 0xF0FFFE)
	{
		TOMWriteWord(offset, data, who);
		return;
	}
	else if (offset >= 0xF10000 && offset <= 0xF1FFFE)
	{
		JERRYWriteWord(offset, data, who);
		return;
	}
	// Don't bomb on attempts to write to ROM
	else if (offset >= 0x800000 && offset <= 0xEFFFFF)
		return;

	jaguar_unknown_writeword(offset, data, who);
}

// We really should re-do this so that it does *real* 32-bit access... !!! FIX !!!
uint32 JaguarReadLong(uint32 offset, uint32 who/*=UNKNOWN*/)
{
	return (JaguarReadWord(offset, who) << 16) | JaguarReadWord(offset+2, who);
}

// We really should re-do this so that it does *real* 32-bit access... !!! FIX !!!
void JaguarWriteLong(uint32 offset, uint32 data, uint32 who/*=UNKNOWN*/)
{
/*	extern bool doDSPDis;
	if (offset < 0x400 && !doDSPDis)
	{
		//WriteLog("JLW: Write to %08X by %s... Starting DSP log!\n\n", offset, whoName[who]);
		doDSPDis = true;
	}//*/
/*if (offset == 0x0100)//64*4)
	//WriteLog("M68K: %s wrote dword to VI vector value %08X...\n", whoName[who], data);//*/

	JaguarWriteWord(offset, data >> 16, who);
	JaguarWriteWord(offset+2, data & 0xFFFF, who);
}

//
// Jaguar console initialization
//
void jaguar_init(void)
{
#ifdef CPU_DEBUG_MEMORY
	memset(readMem, 0x00, 0x400000);
	memset(writeMemMin, 0xFF, 0x400000);
	memset(writeMemMax, 0x00, 0x400000);
#endif
	memory_malloc_secure((void **)&jaguar_mainRam, 0x400000, "Jaguar 68K CPU RAM");
	memory_malloc_secure((void **)&jaguar_mainRom, 0x600000, "Jaguar 68K CPU ROM");
	memory_malloc_secure((void **)&jaguar_bootRom, 0x040000, "Jaguar 68K CPU BIOS ROM"); // Only uses half of this!
	memory_malloc_secure((void **)&jaguar_CDBootROM, 0x040000, "Jaguar 68K CPU CD BIOS ROM");
	memset(jaguar_mainRam, 0x00, 0x400000);
//	memset(jaguar_mainRom, 0xFF, 0x200000);	// & set it to all Fs...
//	memset(jaguar_mainRom, 0x00, 0x200000);	// & set it to all 0s...
//NOTE: This *doesn't* fix FlipOut...
//Or does it? Hmm...
//Seems to want $01010101... Dunno why. Investigate!
	memset(jaguar_mainRom, 0x01, 0x600000);	// & set it to all 01s...
//	memset(jaguar_mainRom, 0xFF, 0x600000);	// & set it to all Fs...

	m68k_set_cpu_type(M68K_CPU_TYPE_68000);
	GPUInit();
	#ifdef DSP_EMU
	DSPInit();
	#endif
	tom_init();
	jerry_init();
#ifdef CDROM_EMU
	CDROMInit();
#endif
}

void jaguar_reset(void)
{
	if (vjs.useJaguarBIOS)
		memcpy(jaguar_mainRam, jaguar_bootRom, 8);
	else
		SET32(jaguar_mainRam, 4, jaguarRunAddress);

//	//WriteLog("jaguar_reset():\n");
	tom_reset();
	jerry_reset();
	GPUReset();
	#ifdef DSP_EMU
	DSPReset();
	#endif
#ifdef CDROM_EMU
	CDROMReset();
#endif
    m68k_pulse_reset();								// Reset the 68000
	//WriteLog("Jaguar: 68K reset. PC=%06X SP=%08X\n", m68k_get_reg(NULL, M68K_REG_PC), m68k_get_reg(NULL, M68K_REG_A7));
}

void jaguar_done(void)
{
#ifdef CDROM_EMU
	CDROMDone();
#endif
	GPUDone();
	#ifdef DSP_EMU
	DSPDone();
	#endif
	tom_done();
	jerry_done();

	memory_free(jaguar_mainRom);
	memory_free(jaguar_mainRam);
	memory_free(jaguar_bootRom);
	memory_free(jaguar_CDBootROM);
}

//
// Main Jaguar execution loop (1 frame)
//
void JaguarExecute(int16 * backbuffer, bool render)
{
	uint16 vp = TOMReadWord(0xF0003E) + 1;//Hmm. This is a WO register. Will work? Looks like. But wrong behavior!
	uint16 vi = TOMReadWord(0xF0004E);//Another WO register...
//Using WO registers is OK, since we're the ones controlling access--there's nothing wrong here! ;-)

//	uint16 vdb = TOMReadWord(0xF00046);
//Note: This is the *definite* end of the display, though VDE *might* be less than this...
//	uint16 vbb = TOMReadWord(0xF00040);
//It seems that they mean it when they say that VDE is the end of object processing.
//However, we need to be able to tell the OP (or TOM) that we've reached the end of the
//buffer and not to write any more pixels... !!! FIX !!!
//	uint16 vde = TOMReadWord(0xF00048);

	uint16 refreshRate = (vjs.hardwareTypeNTSC ? 60 : 50);
	// Should these be hardwired or read from VP? Yes, from VP!
	uint32 M68KCyclesPerScanline
		= (vjs.hardwareTypeNTSC ? M68K_CLOCK_RATE_NTSC : M68K_CLOCK_RATE_PAL) / (vp * refreshRate);
	uint32_t RISCCyclesPerScanline
		= (vjs.hardwareTypeNTSC ? RISC_CLOCK_RATE_NTSC : RISC_CLOCK_RATE_PAL) / (vp * refreshRate);

	TOMResetBackbuffer(backbuffer);
/*extern int effect_start;
if (effect_start)
	//WriteLog("JagExe: VP=%u, VI=%u, CPU CPS=%u, GPU CPS=%u\n", vp, vi, M68KCyclesPerScanline, RISCCyclesPerScanline);//*/

//extern int start_logging;
	for(uint16 i=0; i<vp; i++)
	{
		// Increment the horizontal count (why? RNG? Besides which, this is *NOT* cycle accurate!)
		TOMWriteWord(0xF00004, (TOMReadWord(0xF00004) + 1) & 0x7FF);

		TOMWriteWord(0xF00006, i);					// Write the VC

//		if (i == vi)								// Time for Vertical Interrupt?
//Not sure if this is correct...
//Seems to be, kinda. According to the JTRM, this should only fire on odd lines in non-interlace mode...
//Which means that it normally wouldn't go when it's zero.
		if (i == vi && i > 0 && tom_irq_enabled(IRQ_VBLANK))	// Time for Vertical Interrupt?
		{
			// We don't have to worry about autovectors & whatnot because the Jaguar
			// tells you through its HW registers who sent the interrupt...
			tom_set_pending_video_int();
			m68k_set_irq(7);
		}

//if (start_logging)
//	//WriteLog("About to execute M68K (%u)...\n", i);
		m68k_execute(M68KCyclesPerScanline);
//if (start_logging)
//	//WriteLog("About to execute TOM's PIT (%u)...\n", i);
		TOMExecPIT(RISCCyclesPerScanline);
//if (start_logging)
//	//WriteLog("About to execute JERRY's PIT (%u)...\n", i);
		JERRYExecPIT(RISCCyclesPerScanline);
//if (start_logging)
//	//WriteLog("About to execute JERRY's SSI (%u)...\n", i);
		jerry_i2s_exec(RISCCyclesPerScanline);
#ifdef CDROM_EMU
		BUTCHExec(RISCCyclesPerScanline);
#endif
//if (start_logging)
//	//WriteLog("About to execute GPU (%u)...\n", i);
		GPUExec(RISCCyclesPerScanline);

		#ifdef DSP_EMU
		if (vjs.DSPEnabled)
		{
			if (vjs.usePipelinedDSP)
				DSPExecP2(RISCCyclesPerScanline);	// Pipelined DSP execution (3 stage)...
			else
				DSPExec(RISCCyclesPerScanline);		// Ordinary non-pipelined DSP
//			DSPExecComp(RISCCyclesPerScanline);		// Comparison core
		}
		#endif

//if (start_logging)
//	//WriteLog("About to execute OP (%u)...\n", i);
		TOMExecScanline(i, render);
	}
}

// Temp debugging stuff

void DumpMainMemory(void)
{
}

uint8 * GetRamPtr(void)
{
	return jaguar_mainRam;
}
