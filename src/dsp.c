//
// DSP core
//
// Originally by David Raingeard
// GCC/SDL port by Niels Wagenaar (Linux/WIN32) and Caz (BeOS)
// Extensive cleanups/rewrites by James L. Hammons
//

#include <SDL.h>	// Used only for SDL_GetTicks...
#include "dsp.h"
#define NEW_SCOREBOARD

// Pipeline structures

const uint8_t affectsScoreboard[64] =
{
	 true,  true,  true,  true,
	 true,  true,  true,  true,
	 true,  true,  true,  true,
	 true, false,  true,  true,

	 true,  true, false,  true,
	false,  true,  true,  true,
	 true,  true,  true,  true,
	 true,  true, false, false,

	 true,  true,  true,  true,
	false,  true,  true,  true,
	 true,  true,  true,  true,
	 true, false, false, false,

	 true, false, false,  true,
	false, false,  true,  true,
	 true, false,  true,  true,
	false, false, false,  true
};

struct PipelineStage
{
	uint16_t instruction;
	uint8_t opcode, operand1, operand2;
	uint32_t reg1, reg2, areg1, areg2;
	uint32_t result;
	uint8_t writebackRegister;
	// General memory store...
	uint32_t address;
	uint32_t value;
	uint8_t type;
};

#define TYPE_BYTE			0
#define TYPE_WORD			1
#define TYPE_DWORD			2
#define PIPELINE_STALL		64						// Set to # of opcodes + 1
#ifndef NEW_SCOREBOARD
uint8_t scoreboard[32];
#else
uint8_t scoreboard[32];
#endif
uint8_t plPtrFetch, plPtrRead, plPtrExec, plPtrWrite;
struct PipelineStage pipeline[4];
uint8_t IMASKCleared = false;

// DSP flags (old--have to get rid of this crap)

#define CINT0FLAG			0x00200
#define CINT1FLAG			0x00400
#define CINT2FLAG			0x00800
#define CINT3FLAG			0x01000
#define CINT4FLAG			0x02000
#define CINT04FLAGS			(CINT0FLAG | CINT1FLAG | CINT2FLAG | CINT3FLAG | CINT4FLAG)
#define CINT5FLAG			0x20000		/* DSP only */

// DSP_FLAGS bits

#define ZERO_FLAG		0x00001
#define CARRY_FLAG		0x00002
#define NEGA_FLAG		0x00004
#define IMASK			0x00008
#define INT_ENA0		0x00010
#define INT_ENA1		0x00020
#define INT_ENA2		0x00040
#define INT_ENA3		0x00080
#define INT_ENA4		0x00100
#define INT_CLR0		0x00200
#define INT_CLR1		0x00400
#define INT_CLR2		0x00800
#define INT_CLR3		0x01000
#define INT_CLR4		0x02000
#define REGPAGE			0x04000
#define DMAEN			0x08000
#define INT_ENA5		0x10000
#define INT_CLR5		0x20000

// DSP_CTRL bits

#define DSPGO			0x00001
#define CPUINT			0x00002
#define DSPINT0			0x00004
#define SINGLE_STEP		0x00008
#define SINGLE_GO		0x00010
// Bit 5 is unused!
#define INT_LAT0		0x00040
#define INT_LAT1		0x00080
#define INT_LAT2		0x00100
#define INT_LAT3		0x00200
#define INT_LAT4		0x00400
#define BUS_HOG			0x00800
#define VERSION			0x0F000
#define INT_LAT5		0x10000

extern uint32_t jaguar_mainRom_crc32;

// Is opcode 62 *really* a NOP? Seems like it...
static void dsp_opcode_abs(void);
static void dsp_opcode_add(void);
static void dsp_opcode_addc(void);
static void dsp_opcode_addq(void);
static void dsp_opcode_addqmod(void);	
static void dsp_opcode_addqt(void);
static void dsp_opcode_and(void);
static void dsp_opcode_bclr(void);
static void dsp_opcode_bset(void);
static void dsp_opcode_btst(void);
static void dsp_opcode_cmp(void);
static void dsp_opcode_cmpq(void);
static void dsp_opcode_div(void);
static void dsp_opcode_imacn(void);
static void dsp_opcode_imult(void);
static void dsp_opcode_imultn(void);
static void dsp_opcode_jr(void);
static void dsp_opcode_jump(void);
static void dsp_opcode_load(void);
static void dsp_opcode_loadb(void);
static void dsp_opcode_loadw(void);
static void dsp_opcode_load_r14_indexed(void);
static void dsp_opcode_load_r14_ri(void);
static void dsp_opcode_load_r15_indexed(void);
static void dsp_opcode_load_r15_ri(void);
static void dsp_opcode_mirror(void);	
static void dsp_opcode_mmult(void);
static void dsp_opcode_move(void);
static void dsp_opcode_movei(void);
static void dsp_opcode_movefa(void);
static void dsp_opcode_move_pc(void);
static void dsp_opcode_moveq(void);
static void dsp_opcode_moveta(void);
static void dsp_opcode_mtoi(void);
static void dsp_opcode_mult(void);
static void dsp_opcode_neg(void);
static void dsp_opcode_nop(void);
static void dsp_opcode_normi(void);
static void dsp_opcode_not(void);
static void dsp_opcode_or(void);
static void dsp_opcode_resmac(void);
static void dsp_opcode_ror(void);
static void dsp_opcode_rorq(void);
static void dsp_opcode_xor(void);
static void dsp_opcode_sat16s(void);	
static void dsp_opcode_sat32s(void);	
static void dsp_opcode_sh(void);
static void dsp_opcode_sha(void);
static void dsp_opcode_sharq(void);
static void dsp_opcode_shlq(void);
static void dsp_opcode_shrq(void);
static void dsp_opcode_store(void);
static void dsp_opcode_storeb(void);
static void dsp_opcode_storew(void);
static void dsp_opcode_store_r14_indexed(void);
static void dsp_opcode_store_r14_ri(void);
static void dsp_opcode_store_r15_indexed(void);
static void dsp_opcode_store_r15_ri(void);
static void dsp_opcode_sub(void);
static void dsp_opcode_subc(void);
static void dsp_opcode_subq(void);
static void dsp_opcode_subqmod(void);	
static void dsp_opcode_subqt(void);

uint8_t dsp_opcode_cycles[64] =
{
	3,  3,  3,  3,  3,  3,  3,  3,  
	3,  3,  3,  3,  3,  3,  3,  3,
	3,  3,  1,  3,  1, 18,  3,  3,  
	3,  3,  3,  3,  3,  3,  3,  3,
	3,  3,  2,  2,  2,  2,  3,  4,  
	5,  4,  5,  6,  6,  1,  1,  1,
	1,  2,  2,  2,  1,  1,  9,  3,  
	3,  1,  6,  6,  2,  2,  3,  3
};//*/
//Here's a QnD kludge...
//This is wrong, wrong, WRONG, but it seems to work for the time being...
//(That is, it fixes Flip Out which relies on GPU timing rather than semaphores. Bad developers! Bad!)
//What's needed here is a way to take pipeline effects into account (including pipeline stalls!)...
/*uint8_t dsp_opcode_cycles[64] = 
{
	1,  1,  1,  1,  1,  1,  1,  1,
	1,  1,  1,  1,  1,  1,  1,  1,
	1,  1,  1,  1,  1,  9,  1,  1,
	1,  1,  1,  1,  1,  1,  1,  1,
	1,  1,  1,  1,  1,  1,  1,  2,
	2,  2,  2,  3,  3,  1,  1,  1,
	1,  1,  1,  1,  1,  1,  4,  1,
	1,  1,  3,  3,  1,  1,  1,  1
};//*/

void (* dsp_opcode[64])() =
{	
	dsp_opcode_add,					dsp_opcode_addc,				dsp_opcode_addq,				dsp_opcode_addqt,
	dsp_opcode_sub,					dsp_opcode_subc,				dsp_opcode_subq,				dsp_opcode_subqt,
	dsp_opcode_neg,					dsp_opcode_and,					dsp_opcode_or,					dsp_opcode_xor,
	dsp_opcode_not,					dsp_opcode_btst,				dsp_opcode_bset,				dsp_opcode_bclr,
	dsp_opcode_mult,				dsp_opcode_imult,				dsp_opcode_imultn,				dsp_opcode_resmac,
	dsp_opcode_imacn,				dsp_opcode_div,					dsp_opcode_abs,					dsp_opcode_sh,
	dsp_opcode_shlq,				dsp_opcode_shrq,				dsp_opcode_sha,					dsp_opcode_sharq,
	dsp_opcode_ror,					dsp_opcode_rorq,				dsp_opcode_cmp,					dsp_opcode_cmpq,
	dsp_opcode_subqmod,				dsp_opcode_sat16s,				dsp_opcode_move,				dsp_opcode_moveq,
	dsp_opcode_moveta,				dsp_opcode_movefa,				dsp_opcode_movei,				dsp_opcode_loadb,
	dsp_opcode_loadw,				dsp_opcode_load,				dsp_opcode_sat32s,				dsp_opcode_load_r14_indexed,
	dsp_opcode_load_r15_indexed,	dsp_opcode_storeb,				dsp_opcode_storew,				dsp_opcode_store,
	dsp_opcode_mirror,				dsp_opcode_store_r14_indexed,	dsp_opcode_store_r15_indexed,	dsp_opcode_move_pc,
	dsp_opcode_jump,				dsp_opcode_jr,					dsp_opcode_mmult,				dsp_opcode_mtoi,
	dsp_opcode_normi,				dsp_opcode_nop,					dsp_opcode_load_r14_ri,			dsp_opcode_load_r15_ri,
	dsp_opcode_store_r14_ri,		dsp_opcode_store_r15_ri,		dsp_opcode_nop,					dsp_opcode_addqmod,
};

uint32_t dsp_pc;
static uint64_t dsp_acc;								// 40 bit register, NOT 32!
static uint32_t dsp_remain;
static uint32_t dsp_modulo;
static uint32_t dsp_flags;
static uint32_t dsp_matrix_control;
static uint32_t dsp_pointer_to_matrix;
static uint32_t dsp_data_organization;
uint32_t dsp_control;
static uint32_t dsp_div_control;
static uint8_t dsp_flag_z, dsp_flag_n, dsp_flag_c;    
static uint32_t * dsp_reg, * dsp_alternate_reg;
static uint32_t * dsp_reg_bank_0, * dsp_reg_bank_1;

static uint32_t dsp_opcode_first_parameter;
static uint32_t dsp_opcode_second_parameter;

#define DSP_RUNNING			(dsp_control & 0x01)

#define RM					dsp_reg[dsp_opcode_first_parameter]
#define RN					dsp_reg[dsp_opcode_second_parameter]
#define ALTERNATE_RM		dsp_alternate_reg[dsp_opcode_first_parameter]
#define ALTERNATE_RN		dsp_alternate_reg[dsp_opcode_second_parameter]
#define IMM_1				dsp_opcode_first_parameter
#define IMM_2				dsp_opcode_second_parameter

#define CLR_Z				(dsp_flag_z = 0)
#define CLR_ZN				(dsp_flag_z = dsp_flag_n = 0)
#define CLR_ZNC				(dsp_flag_z = dsp_flag_n = dsp_flag_c = 0)
#define SET_Z(r)			(dsp_flag_z = ((r) == 0))
#define SET_N(r)			(dsp_flag_n = (((uint32_t)(r) >> 31) & 0x01))
#define SET_C_ADD(a,b)		(dsp_flag_c = ((uint32_t)(b) > (uint32_t)(~(a))))
#define SET_C_SUB(a,b)		(dsp_flag_c = ((uint32_t)(b) > (uint32_t)(a)))
#define SET_ZN(r)			SET_N(r); SET_Z(r)
#define SET_ZNC_ADD(a,b,r)	SET_N(r); SET_Z(r); SET_C_ADD(a,b)
#define SET_ZNC_SUB(a,b,r)	SET_N(r); SET_Z(r); SET_C_SUB(a,b)

uint32_t dsp_convert_zero[32] = { 32,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31 };
uint8_t * dsp_branch_condition_table = NULL;
static uint16_t * mirror_table = NULL;
static uint8_t * dsp_ram_8 = NULL;

#define BRANCH_CONDITION(x)		dsp_branch_condition_table[(x) + ((jaguar_flags & 7) << 5)]

FILE * dsp_fp;

// Private function prototypes

void DSPDumpRegisters(void);
void DSPDumpDisassembly(void);
void FlushDSPPipeline(void);


//
// Update the DSP register file pointers depending on REGPAGE bit
//
static void DSPUpdateRegisterBanks(void)
{
	int bank = (dsp_flags & REGPAGE);

	if (dsp_flags & IMASK)
		bank = 0;							// IMASK forces main bank to be bank 0

	if (bank)
		dsp_reg = dsp_reg_bank_1, dsp_alternate_reg = dsp_reg_bank_0;
	else
		dsp_reg = dsp_reg_bank_0, dsp_alternate_reg = dsp_reg_bank_1;
}


void dsp_build_branch_condition_table(void)
{
	// Allocate the mirror table
	if (!mirror_table)
		mirror_table = (uint16_t *)malloc(65536 * sizeof(mirror_table[0]));

	// Fill in the mirror table
	if (mirror_table)
		for(int i=0; i<65536; i++)
			mirror_table[i] = ((i >> 15) & 0x0001) | ((i >> 13) & 0x0002) |
			                  ((i >> 11) & 0x0004) | ((i >> 9)  & 0x0008) |
			                  ((i >> 7)  & 0x0010) | ((i >> 5)  & 0x0020) |
			                  ((i >> 3)  & 0x0040) | ((i >> 1)  & 0x0080) |
			                  ((i << 1)  & 0x0100) | ((i << 3)  & 0x0200) |
			                  ((i << 5)  & 0x0400) | ((i << 7)  & 0x0800) |
			                  ((i << 9)  & 0x1000) | ((i << 11) & 0x2000) |
			                  ((i << 13) & 0x4000) | ((i << 15) & 0x8000);

	if (!dsp_branch_condition_table)
	{
		dsp_branch_condition_table = (uint8_t *)malloc(32 * 8 * sizeof(dsp_branch_condition_table[0]));

		// Fill in the condition table
		if (dsp_branch_condition_table)
		{
			for(int i=0; i<8; i++)
			{
				for(int j=0; j<32; j++)
				{
					int result = 1;
					if (j & 1)
						if (i & ZERO_FLAG)
							result = 0;
					if (j & 2)
						if (!(i & ZERO_FLAG))
							result = 0;
					if (j & 4)
						if (i & (CARRY_FLAG << (j >> 4)))
							result = 0;
					if (j & 8)
						if (!(i & (CARRY_FLAG << (j >> 4))))
							result = 0;
					dsp_branch_condition_table[i * 32 + j] = result;
				}
			}
		}
	}
}

uint8_t DSPReadByte(uint32_t offset, uint32_t who/*=UNKNOWN*/)
{
	who = UNKNOWN;
	
	if (offset >= DSP_WORK_RAM_BASE && offset <= (DSP_WORK_RAM_BASE + 0x1FFF))
		return dsp_ram_8[offset - DSP_WORK_RAM_BASE];

	if (offset >= DSP_CONTROL_RAM_BASE && offset <= (DSP_CONTROL_RAM_BASE + 0x1F))
	{
		uint32_t data = DSPReadLong(offset & 0xFFFFFFFC, who);

		if ((offset&0x03)==0)
			return(data>>24);
		else
		if ((offset&0x03)==1)
			return((data>>16)&0xff);
		else
		if ((offset&0x03)==2)
			return((data>>8)&0xff);
		else
		if ((offset&0x03)==3)
			return(data&0xff);
	}

	return JaguarReadByte(offset, who);
} 

uint16_t DSPReadWord(uint32_t offset, uint32_t who/*=UNKNOWN*/)
{
	who = UNKNOWN;
	offset &= 0xFFFFFFFE;

#ifndef DSP_EMU
/*
	if (jaguar_mainRom_crc32==0x7ae20823)
	{
		if (offset==0xF1B9D8) return(0x0000);
		if (offset==0xF1B9Da) return(0x0000);
		if (offset==0xF1B2C0) return(0x0000);
		if (offset==0xF1B2C2) return(0x0000);
	}
*/
	// Hack to allow Wolfenstein 3D to play without DSP emulation
	if ((offset==0xF1B0D0)||(offset==0xF1B0D2))
		return(0);

	// Hack to allow NBA Jam to play without DSP emulation
	if (jaguar_mainRom_crc32==0x4faddb18)
	{
		if (offset==0xf1b2c0) return(0);
		if (offset==0xf1b2c2) return(0);
		if (offset==0xf1b240) return(0);
		if (offset==0xf1b242) return(0);
		if (offset==0xF1B340) return(0);
		if (offset==0xF1B342) return(0);
		if (offset==0xF1BAD8) return(0);
		if (offset==0xF1BADA) return(0);
		if (offset==0xF1B040) return(0);
		if (offset==0xF1B042) return(0);
		if (offset==0xF1B0C0) return(0);
		if (offset==0xF1B0C2) return(0);
		if (offset==0xF1B140) return(0);
		if (offset==0xF1B142) return(0);
		if (offset==0xF1B1C0) return(0);
		if (offset==0xF1B1C2) return(0);
	}
#endif

	if (offset >= DSP_WORK_RAM_BASE && offset <= DSP_WORK_RAM_BASE+0x1FFF)
	{
		offset -= DSP_WORK_RAM_BASE;
		return GET16(dsp_ram_8, offset);
	}
	else if ((offset>=DSP_CONTROL_RAM_BASE)&&(offset<DSP_CONTROL_RAM_BASE+0x20))
	{
		uint32_t data = DSPReadLong(offset & 0xFFFFFFFC, who);

		if (offset & 0x03)
			return data & 0xFFFF;
		else
			return data >> 16;
	}

	return JaguarReadWord(offset, who);
}

uint32_t DSPReadLong(uint32_t offset, uint32_t who/*=UNKNOWN*/)
{
	who = UNKNOWN;
	// ??? WHY ???
	offset &= 0xFFFFFFFC;

	if (offset >= DSP_WORK_RAM_BASE && offset <= DSP_WORK_RAM_BASE + 0x1FFF)
	{
		offset -= DSP_WORK_RAM_BASE;
		return GET32(dsp_ram_8, offset);
	}
	//NOTE: Didn't return DSP_ACCUM!!!
	//Mebbe it's not 'spose to! Yes, it is!
	if (offset >= DSP_CONTROL_RAM_BASE && offset <= DSP_CONTROL_RAM_BASE + 0x23)
	{
		offset &= 0x3F;
		switch (offset)
		{
		case 0x00:	/*dsp_flag_c?(dsp_flag_c=1):(dsp_flag_c=0);
					dsp_flag_z?(dsp_flag_z=1):(dsp_flag_z=0);
					dsp_flag_n?(dsp_flag_n=1):(dsp_flag_n=0);*/

					dsp_flags = (dsp_flags & 0xFFFFFFF8) | (dsp_flag_n << 2) | (dsp_flag_c << 1) | dsp_flag_z;
					return dsp_flags & 0xFFFFC1FF;
		case 0x04: return dsp_matrix_control;
		case 0x08: return dsp_pointer_to_matrix;
		case 0x0C: return dsp_data_organization;
		case 0x10: return dsp_pc;
		case 0x14: return dsp_control;
		case 0x18: return dsp_modulo;
		case 0x1C: return dsp_remain;
		case 0x20:
			return (int32_t)((int8_t)(dsp_acc >> 32));	// Top 8 bits of 40-bit accumulator, sign extended
		}
		// unaligned long read-- !!! FIX !!!
		return 0xFFFFFFFF;
	}

	return JaguarReadLong(offset, who);
}

void DSPWriteByte(uint32_t offset, uint8_t data, uint32_t who/*=UNKNOWN*/)
{
	who = UNKNOWN;
	if ((offset >= DSP_WORK_RAM_BASE) && (offset < DSP_WORK_RAM_BASE+0x2000))
	{
		offset -= DSP_WORK_RAM_BASE;
		dsp_ram_8[offset] = data;
		return;
	}
	if ((offset >= DSP_CONTROL_RAM_BASE) && (offset < DSP_CONTROL_RAM_BASE+0x20))
	{
		uint32_t reg = offset & 0x1C;
		int bytenum = offset & 0x03;
		
		if ((reg >= 0x1C) && (reg <= 0x1F))
			dsp_div_control = (dsp_div_control & (~(0xFF << (bytenum << 3)))) | (data << (bytenum << 3));
		else
		{
			//This looks funky. !!! FIX !!!
			uint32_t old_data = DSPReadLong(offset&0xFFFFFFC, who);
			bytenum = 3 - bytenum; // convention motorola !!!
			old_data = (old_data & (~(0xFF << (bytenum << 3)))) | (data << (bytenum << 3));	
			DSPWriteLong(offset & 0xFFFFFFC, old_data, who);
		}
		return;
	}
	JaguarWriteByte(offset, data, who);
}

void DSPWriteWord(uint32_t offset, uint16_t data, uint32_t who/*=UNKNOWN*/)
{
	who = UNKNOWN;
	offset &= 0xFFFFFFFE;
	
	if ((offset >= DSP_WORK_RAM_BASE) && (offset < DSP_WORK_RAM_BASE+0x2000))
	{
		offset -= DSP_WORK_RAM_BASE;
		dsp_ram_8[offset] = data >> 8;
		dsp_ram_8[offset+1] = data & 0xFF;
		return;
	}
	else if ((offset >= DSP_CONTROL_RAM_BASE) && (offset < DSP_CONTROL_RAM_BASE+0x20))
	{
		if ((offset & 0x1C) == 0x1C)
		{
			if (offset & 0x03)
				dsp_div_control = (dsp_div_control&0xffff0000)|(data&0xffff);
			else
				dsp_div_control = (dsp_div_control&0xffff)|((data&0xffff)<<16);
		}
		else
		{
			uint32_t old_data = DSPReadLong(offset & 0xffffffc, who);
			if (offset & 0x03)
				old_data = (old_data&0xffff0000)|(data&0xffff);
			else
				old_data = (old_data&0xffff)|((data&0xffff)<<16);
			DSPWriteLong(offset & 0xffffffc, old_data, who);
		}
		return;
	}

	JaguarWriteWord(offset, data, who);
}

void DSPWriteLong(uint32_t offset, uint32_t data, uint32_t who/*=UNKNOWN*/)
{
	who = UNKNOWN;
	offset &= 0xFFFFFFFC;

	if (offset >= DSP_WORK_RAM_BASE && offset <= DSP_WORK_RAM_BASE + 0x1FFF)
	{
		offset -= DSP_WORK_RAM_BASE;
		SET32(dsp_ram_8, offset, data);
		return;
	}
	else if (offset >= DSP_CONTROL_RAM_BASE && offset <= (DSP_CONTROL_RAM_BASE + 0x1F))
	{
		offset &= 0x1F;
		switch (offset)
		{
		case 0x00:
		{
			IMASKCleared = (dsp_flags & IMASK) && !(data & IMASK);
			dsp_flags = data;
			dsp_flag_z = dsp_flags & 0x01;
			dsp_flag_c = (dsp_flags >> 1) & 0x01;
			dsp_flag_n = (dsp_flags >> 2) & 0x01;
			DSPUpdateRegisterBanks();
			dsp_control &= ~((dsp_flags & CINT04FLAGS) >> 3);
			dsp_control &= ~((dsp_flags & CINT5FLAG) >> 1);
			break;
		}
		case 0x04:
			dsp_matrix_control = data;
			break;
		case 0x08:
			// According to JTRM, only lines 2-11 are addressable, the rest being
			// hardwired to $F1Bxxx.
			dsp_pointer_to_matrix = 0xF1B000 | (data & 0x000FFC);
			break;
		case 0x0C:
			dsp_data_organization = data;
			break;
		case 0x10:
			dsp_pc = data;
			break;
		case 0x14:
		{	
			uint32_t wasRunning = DSP_RUNNING;
			// Check for DSP -> CPU interrupt
			if (data & CPUINT)
			{
// This was WRONG
// Why do we check for a valid handler at 64? Isn't that the Jag programmer's responsibility?
				if (JERRYIRQEnabled(IRQ2_DSP))// && jaguar_interrupt_handler_is_valid(64))
				{
					JERRYSetPendingIRQ(IRQ2_DSP);
					m68k_set_irq(2);			// Set 68000 NMI...
				}
				data &= ~CPUINT;
			}
			// Check for CPU -> DSP interrupt
			if (data & DSPINT0)
			{
				m68k_end_timeslice();
				DSPSetIRQLine(DSPIRQ_CPU, ASSERT_LINE);
				data &= ~DSPINT0;
			}

			// Protect writes to VERSION and the interrupt latches...
			uint32_t mask = VERSION | INT_LAT0 | INT_LAT1 | INT_LAT2 | INT_LAT3 | INT_LAT4 | INT_LAT5;
			dsp_control = (dsp_control & mask) | (data & ~mask);


			// if dsp wasn't running but is now running
			// execute a few cycles
//This is just plain wrong, wrong, WRONG!
#ifdef DSP_SINGLE_STEPPING
			if (dsp_control & 0x18)
				DSPExec(1);
#endif
			if (DSP_RUNNING)
			{
				if (who == M68K)
					m68k_end_timeslice();
				else if (who == GPU)
					GPUReleaseTimeslice();

				if (!wasRunning)
					FlushDSPPipeline();
			}
			break;
		}
		case 0x18:
			dsp_modulo = data;
			break;
		case 0x1C:
			dsp_div_control = data;
			break;
//		default:   // unaligned long read
				   //__asm int 3
		}
		return;
	}
	JaguarWriteLong(offset, data, who);
}

//
// Check for and handle any asserted DSP IRQs
//
static void DSPHandleIRQs(void)
{
	if (dsp_flags & IMASK) 							// Bail if we're already inside an interrupt
		return;

	// Get the active interrupt bits (latches) & interrupt mask (enables)
	uint32_t bits = ((dsp_control >> 10) & 0x20) | ((dsp_control >> 6) & 0x1F),
		mask = ((dsp_flags >> 11) & 0x20) | ((dsp_flags >> 4) & 0x1F);

//	//WriteLog("dsp: bits=%.2x mask=%.2x\n",bits,mask);
	bits &= mask;

	if (!bits)										// Bail if nothing is enabled
		return;

	int which = 0;									// Determine which interrupt 
	if (bits & 0x01)
		which = 0;
	if (bits & 0x02)
		which = 1;
	if (bits & 0x04)
		which = 2;
	if (bits & 0x08)
		which = 3;
	if (bits & 0x10)
		which = 4;
	if (bits & 0x20)
		which = 5;

	if (pipeline[plPtrWrite].opcode != PIPELINE_STALL)
	{
		if (pipeline[plPtrWrite].writebackRegister != 0xFF)
		{
			if (pipeline[plPtrWrite].writebackRegister != 0xFE)
				dsp_reg[pipeline[plPtrWrite].writebackRegister] = pipeline[plPtrWrite].result;
			else
			{
				if (pipeline[plPtrWrite].type == TYPE_BYTE)
					JaguarWriteByte(pipeline[plPtrWrite].address, pipeline[plPtrWrite].value, UNKNOWN);
				else if (pipeline[plPtrWrite].type == TYPE_WORD)
					JaguarWriteWord(pipeline[plPtrWrite].address, pipeline[plPtrWrite].value, UNKNOWN);
				else
					JaguarWriteLong(pipeline[plPtrWrite].address, pipeline[plPtrWrite].value, UNKNOWN);
			}
		}

#ifndef NEW_SCOREBOARD
		if (affectsScoreboard[pipeline[plPtrWrite].opcode])
			scoreboard[pipeline[plPtrWrite].operand2] = false;
#else
//Yup, sequential MOVEQ # problem fixing (I hope!)...
		if (affectsScoreboard[pipeline[plPtrWrite].opcode])
			if (scoreboard[pipeline[plPtrWrite].operand2])
				scoreboard[pipeline[plPtrWrite].operand2]--;
#endif
	}

	dsp_flags |= IMASK;
	DSPUpdateRegisterBanks();
	// subqt  #4,r31		; pre-decrement stack pointer 
	// move   pc,r30		; address of interrupted code 
	// store  r30,(r31)     ; store return address
	dsp_reg[31] -= 4;
	DSPWriteLong(dsp_reg[31], dsp_pc - 2 - (pipeline[plPtrExec].opcode == 38 ? 6 : (pipeline[plPtrExec].opcode == PIPELINE_STALL ? 0 : 2)), DSP);
	// movei  #service_address,r30  ; pointer to ISR entry 
	// jump  (r30)					; jump to ISR 
	// nop
	dsp_pc = dsp_reg[30] = DSP_WORK_RAM_BASE + (which * 0x10);
	FlushDSPPipeline();
}

//
// Non-pipelined version...
//
void DSPHandleIRQsNP(void)
{
	if (dsp_flags & IMASK) 							// Bail if we're already inside an interrupt
		return;

	// Get the active interrupt bits (latches) & interrupt mask (enables)
	uint32_t bits = ((dsp_control >> 10) & 0x20) | ((dsp_control >> 6) & 0x1F),
		mask = ((dsp_flags >> 11) & 0x20) | ((dsp_flags >> 4) & 0x1F);

//	//WriteLog("dsp: bits=%.2x mask=%.2x\n",bits,mask);
	bits &= mask;

	if (!bits)										// Bail if nothing is enabled
		return;

	int which = 0;									// Determine which interrupt 
	if (bits & 0x01)
		which = 0;
	if (bits & 0x02)
		which = 1;
	if (bits & 0x04)
		which = 2;
	if (bits & 0x08)
		which = 3;
	if (bits & 0x10)
		which = 4;
	if (bits & 0x20)
		which = 5;

	dsp_flags |= IMASK;
	DSPUpdateRegisterBanks();
	// subqt  #4,r31		; pre-decrement stack pointer 
	// move   pc,r30		; address of interrupted code 
	// store  r30,(r31)     ; store return address
	dsp_reg[31] -= 4;
	DSPWriteLong(dsp_reg[31], dsp_pc - 2, DSP);

	// movei  #service_address,r30  ; pointer to ISR entry 
	// jump  (r30)					; jump to ISR 
	// nop
	dsp_pc = dsp_reg[30] = DSP_WORK_RAM_BASE + (which * 0x10);
}

//
// Set the specified DSP IRQ line to a given state
//
void DSPSetIRQLine(int irqline, int state)
{
//NOTE: This doesn't take INT_LAT5 into account. !!! FIX !!!
	uint32_t mask = INT_LAT0 << irqline;
	dsp_control &= ~mask;							// Clear the latch bit

	if (state)
	{
		dsp_control |= mask;						// Set the latch bit
		DSPHandleIRQs();
	}

	// Not sure if this is correct behavior, but according to JTRM,
	// the IRQ output of JERRY is fed to this IRQ in the GPU...
// Not sure this is right--DSP interrupts seem to be different from the JERRY interrupts!
//	GPUSetIRQLine(GPUIRQ_DSP, ASSERT_LINE);
}

void DSPInit(void)
{
	memory_malloc_secure((void **)&dsp_ram_8, 0x2000, "DSP work RAM");
	memory_malloc_secure((void **)&dsp_reg_bank_0, 32 * sizeof(int32_t), "DSP bank 0 regs");
	memory_malloc_secure((void **)&dsp_reg_bank_1, 32 * sizeof(int32_t), "DSP bank 1 regs");

	dsp_build_branch_condition_table();
	DSPReset();
}

void DSPReset(void)
{
	dsp_pc				  = 0x00F1B000;
	dsp_acc				  = 0x00000000;
	dsp_remain			  = 0x00000000;
	dsp_modulo			  = 0xFFFFFFFF;
	dsp_flags			  = 0x00040000;
	dsp_matrix_control    = 0x00000000;
	dsp_pointer_to_matrix = 0x00000000;
	dsp_data_organization = 0xFFFFFFFF;
	dsp_control			  = 0x00002000;				// Report DSP version 2
	dsp_div_control		  = 0x00000000;

	dsp_reg = dsp_reg_bank_0;
	dsp_alternate_reg = dsp_reg_bank_1;

	for(int i=0; i<32; i++)
		dsp_reg[i] = dsp_alternate_reg[i] = 0x00000000;

	CLR_ZNC;
	IMASKCleared = false;
	FlushDSPPipeline();
	memset(dsp_ram_8, 0xFF, 0x2000);
}

void DSPDumpDisassembly(void)
{
}

void DSPDumpRegisters(void)
{
}

void DSPDone(void)
{
	memory_free(dsp_ram_8);
	memory_free(dsp_reg_bank_0);
	memory_free(dsp_reg_bank_1);
}

//
// DSP execution core
//
void DSPExec(int32_t cycles)
{
	while (cycles > 0 && DSP_RUNNING)
	{
		if (IMASKCleared)						// If IMASK was cleared,
		{
			DSPHandleIRQsNP();					// See if any other interrupts are pending!
			IMASKCleared = false;
		}

		uint16_t opcode = DSPReadWord(dsp_pc, DSP);
		uint32_t index = opcode >> 10;
		dsp_opcode_first_parameter = (opcode >> 5) & 0x1F;
		dsp_opcode_second_parameter = opcode & 0x1F;
		dsp_pc += 2;
		dsp_opcode[index]();
		cycles -= dsp_opcode_cycles[index];
	}
}

//
// DSP opcode handlers
//

// There is a problem here with interrupt handlers the JUMP and JR instructions that
// can cause trouble because an interrupt can occur *before* the instruction following the
// jump can execute... !!! FIX !!!
static void dsp_opcode_jump(void)
{
	uint32_t jaguar_flags = (dsp_flag_n << 2) | (dsp_flag_c << 1) | dsp_flag_z;

	if (BRANCH_CONDITION(IMM_2))
	{
		uint32_t delayed_pc = RM;
		DSPExec(1);
		dsp_pc = delayed_pc;
	}
}

static void dsp_opcode_jr(void)
{
	uint32_t jaguar_flags = (dsp_flag_n << 2) | (dsp_flag_c << 1) | dsp_flag_z;

	if (BRANCH_CONDITION(IMM_2))
	{
		int32_t offset = (IMM_1 & 0x10 ? 0xFFFFFFF0 | IMM_1 : IMM_1);		// Sign extend IMM_1
		int32_t delayed_pc = dsp_pc + (offset * 2);
		DSPExec(1);
		dsp_pc = delayed_pc;
	}
}

static void dsp_opcode_add(void)
{
	uint32_t res = RN + RM;
	SET_ZNC_ADD(RN, RM, res);
	RN = res;
}

static void dsp_opcode_addc(void)
{
	uint32_t res = RN + RM + dsp_flag_c;
	uint32_t carry = dsp_flag_c;
	SET_ZNC_ADD(RN + carry, RM, res);
	RN = res;
}

static void dsp_opcode_addq(void)
{
	uint32_t r1 = dsp_convert_zero[IMM_1];
	uint32_t res = RN + r1;
	CLR_ZNC; SET_ZNC_ADD(RN, r1, res);
	RN = res;
}

static void dsp_opcode_sub(void)
{
	uint32_t res = RN - RM;
	SET_ZNC_SUB(RN, RM, res);
	RN = res;
}

static void dsp_opcode_subc(void)
{
	uint32_t res = RN - RM - dsp_flag_c;
	uint32_t borrow = dsp_flag_c;
	SET_ZNC_SUB(RN - borrow, RM, res);
	RN = res;
}

static void dsp_opcode_subq(void)
{
	uint32_t r1 = dsp_convert_zero[IMM_1];
	uint32_t res = RN - r1;
	SET_ZNC_SUB(RN, r1, res);
	RN = res;
}

static void dsp_opcode_cmp(void)
{
	uint32_t res = RN - RM;
	SET_ZNC_SUB(RN, RM, res);
}

static void dsp_opcode_cmpq(void)
{
	static int32_t sqtable[32] =
		{ 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,-16,-15,-14,-13,-12,-11,-10,-9,-8,-7,-6,-5,-4,-3,-2,-1 };
	uint32_t r1 = sqtable[IMM_1 & 0x1F]; // I like this better -> (int8_t)(jaguar.op >> 2) >> 3;
	uint32_t res = RN - r1;
	SET_ZNC_SUB(RN, r1, res);
}

static void dsp_opcode_and(void)
{
	RN = RN & RM;
	SET_ZN(RN);
}

static void dsp_opcode_or(void)
{
	RN = RN | RM;
	SET_ZN(RN);
}

static void dsp_opcode_xor(void)
{
	RN = RN ^ RM;
	SET_ZN(RN);
}

static void dsp_opcode_not(void)
{
	RN = ~RN;
	SET_ZN(RN);
}

static void dsp_opcode_move_pc(void)
{
	RN = dsp_pc - 2;
}

static void dsp_opcode_store_r14_indexed(void)
{
	DSPWriteLong(dsp_reg[14] + (dsp_convert_zero[IMM_1] << 2), RN, DSP);
}

static void dsp_opcode_store_r15_indexed(void)
{
	DSPWriteLong(dsp_reg[15] + (dsp_convert_zero[IMM_1] << 2), RN, DSP);
}

static void dsp_opcode_load_r14_ri(void)
{
	RN = DSPReadLong(dsp_reg[14] + RM, DSP);
}

static void dsp_opcode_load_r15_ri(void)
{
	RN = DSPReadLong(dsp_reg[15] + RM, DSP);
}

static void dsp_opcode_store_r14_ri(void)
{
	DSPWriteLong(dsp_reg[14] + RM, RN, DSP);
}

static void dsp_opcode_store_r15_ri(void)
{
	DSPWriteLong(dsp_reg[15] + RM, RN, DSP);
}

static void dsp_opcode_nop(void)
{
}

static void dsp_opcode_storeb(void)
{
	if (RM >= DSP_WORK_RAM_BASE && RM <= (DSP_WORK_RAM_BASE + 0x1FFF))
		DSPWriteLong(RM, RN & 0xFF, DSP);
	else
		JaguarWriteByte(RM, RN, DSP);
}

static void dsp_opcode_storew(void)
{
	if (RM >= DSP_WORK_RAM_BASE && RM <= (DSP_WORK_RAM_BASE + 0x1FFF))
		DSPWriteLong(RM, RN & 0xFFFF, DSP);
	else
		JaguarWriteWord(RM, RN, DSP);
}

static void dsp_opcode_store(void)
{
	DSPWriteLong(RM, RN, DSP);
}

static void dsp_opcode_loadb(void)
{
	if (RM >= DSP_WORK_RAM_BASE && RM <= (DSP_WORK_RAM_BASE + 0x1FFF))
		RN = DSPReadLong(RM, DSP) & 0xFF;
	else
		RN = JaguarReadByte(RM, DSP);
}

static void dsp_opcode_loadw(void)
{
	if (RM >= DSP_WORK_RAM_BASE && RM <= (DSP_WORK_RAM_BASE + 0x1FFF))
		RN = DSPReadLong(RM, DSP) & 0xFFFF;
	else
		RN = JaguarReadWord(RM, DSP);
}

static void dsp_opcode_load(void)
{
	RN = DSPReadLong(RM, DSP);
}

static void dsp_opcode_load_r14_indexed(void)
{
	RN = DSPReadLong(dsp_reg[14] + (dsp_convert_zero[IMM_1] << 2), DSP);
}

static void dsp_opcode_load_r15_indexed(void)
{
	RN = DSPReadLong(dsp_reg[15] + (dsp_convert_zero[IMM_1] << 2), DSP);
}

static void dsp_opcode_movei(void)
{
	// This instruction is followed by 32-bit value in LSW / MSW format...
	RN = (uint32_t)DSPReadWord(dsp_pc, DSP) | ((uint32_t)DSPReadWord(dsp_pc + 2, DSP) << 16);
	dsp_pc += 4;
}

static void dsp_opcode_moveta(void)
{
	ALTERNATE_RN = RM;
}

static void dsp_opcode_movefa(void)
{
	RN = ALTERNATE_RM;
}

static void dsp_opcode_move(void)
{
	RN = RM;
}

static void dsp_opcode_moveq(void)
{
	RN = IMM_1;
}

static void dsp_opcode_resmac(void)
{
	RN = (uint32_t)dsp_acc;
}

static void dsp_opcode_imult(void)
{
	RN = (int16_t)RN * (int16_t)RM;
	SET_ZN(RN);
}

static void dsp_opcode_mult(void)
{
	RN = (uint16_t)RM * (uint16_t)RN;
	SET_ZN(RN);
}

static void dsp_opcode_bclr(void)
{
	uint32_t res = RN & ~(1 << IMM_1);
	RN = res;
	SET_ZN(res);
}

static void dsp_opcode_btst(void)
{
	dsp_flag_z = (~RN >> IMM_1) & 1;
}

static void dsp_opcode_bset(void)
{
	uint32_t res = RN | (1 << IMM_1);
	RN = res;
	SET_ZN(res);
}

static void dsp_opcode_subqt(void)
{
	RN -= dsp_convert_zero[IMM_1];
}

static void dsp_opcode_addqt(void)
{
	RN += dsp_convert_zero[IMM_1];
}

static void dsp_opcode_imacn(void)
{
	int32_t res = (int16_t)RM * (int16_t)RN;
	dsp_acc += (int64_t) res;
//Should we AND the result to fit into 40 bits here???
} 

static void dsp_opcode_mtoi(void)
{
	RN = (((int32_t)RM >> 8) & 0xFF800000) | (RM & 0x007FFFFF);
	SET_ZN(RN);
}

static void dsp_opcode_normi(void)
{
	uint32_t _Rm = RM;
	uint32_t res = 0;

	if (_Rm)
	{
		while ((_Rm & 0xffc00000) == 0)
		{
			_Rm <<= 1;
			res--;
		}
		while ((_Rm & 0xff800000) != 0)
		{
			_Rm >>= 1;
			res++;
		}
	}
	RN = res;
	SET_ZN(RN);
}

static void dsp_opcode_mmult(void)
{
	int count	= dsp_matrix_control&0x0f;
	uint32_t addr = dsp_pointer_to_matrix; // in the gpu ram
	int64_t  accum = 0;
	uint32_t res;

	if (!(dsp_matrix_control & 0x10))
	{
		for (int i = 0; i < count; i++)
		{ 
			int16_t a;
			if (i&0x01)
				a=(int16_t)((dsp_alternate_reg[dsp_opcode_first_parameter + (i>>1)]>>16)&0xffff);
			else
				a=(int16_t)(dsp_alternate_reg[dsp_opcode_first_parameter + (i>>1)]&0xffff);
			int16_t b=((int16_t)DSPReadWord(addr + 2, DSP));
			accum += a*b;
			addr += 4;
		}
	}
	else
	{
		for (int i = 0; i < count; i++)
		{
			int16_t a;
			if (i&0x01)
				a=(int16_t)((dsp_alternate_reg[dsp_opcode_first_parameter + (i>>1)]>>16)&0xffff);
			else
				a=(int16_t)(dsp_alternate_reg[dsp_opcode_first_parameter + (i>>1)]&0xffff);
			int16_t b=((int16_t)DSPReadWord(addr + 2, DSP));
			accum += a*b;
			addr += 4 * count;
		}
	}
	RN = res = (int32_t)accum;
	// carry flag to do
//NOTE: The flags are set based upon the last add/multiply done...
	SET_ZN(RN);
}

static void dsp_opcode_abs(void)
{
	uint32_t _Rn = RN;
	uint32_t res;
	
	if (_Rn == 0x80000000)
		dsp_flag_n = 1;
	else
	{
		dsp_flag_c = ((_Rn & 0x80000000) >> 31);
		res = RN = (_Rn & 0x80000000 ? -_Rn : _Rn);
		CLR_ZN; SET_Z(res);
	}
}

static void dsp_opcode_div(void)
{
	uint32_t _Rm=RM;
	uint32_t _Rn=RN;

	if (_Rm)
	{
		if (dsp_div_control & 1)
		{
			dsp_remain = (((uint64_t)_Rn) << 16) % _Rm;
			if (dsp_remain&0x80000000)
				dsp_remain-=_Rm;
			RN = (((uint64_t)_Rn) << 16) / _Rm;
		}
		else
		{
			dsp_remain = _Rn % _Rm;
			if (dsp_remain&0x80000000)
				dsp_remain-=_Rm;
			RN/=_Rm;
		}
	}
	else
		RN=0xffffffff;
}

static void dsp_opcode_imultn(void)
{
	// This is OK, since this multiply won't overflow 32 bits...
	int32_t res = (int32_t)((int16_t)RN * (int16_t)RM);
	dsp_acc = (int64_t) res;
	SET_ZN(res);
}

static void dsp_opcode_neg(void)
{
	uint32_t res = -RN;
	SET_ZNC_SUB(0, RN, res);
	RN = res;
}

static void dsp_opcode_shlq(void)
{
	int32_t r1 = 32 - IMM_1;
	uint32_t res = RN << r1;
	SET_ZN(res); dsp_flag_c = (RN >> 31) & 1;
	RN = res;
}

static void dsp_opcode_shrq(void)
{
	int32_t r1 = dsp_convert_zero[IMM_1];
	uint32_t res = RN >> r1;
	SET_ZN(res); dsp_flag_c = RN & 1;
	RN = res;
}

static void dsp_opcode_ror(void)
{
	uint32_t r1 = RM & 0x1F;
	uint32_t res = (RN >> r1) | (RN << (32 - r1));
	SET_ZN(res); dsp_flag_c = (RN >> 31) & 1;
	RN = res;
}

static void dsp_opcode_rorq(void)
{
	uint32_t r1 = dsp_convert_zero[IMM_1 & 0x1F];
	uint32_t r2 = RN;
	uint32_t res = (r2 >> r1) | (r2 << (32 - r1));
	RN = res;
	SET_ZN(res); dsp_flag_c = (r2 >> 31) & 0x01;
}

static void dsp_opcode_sha(void)
{
	int32_t sRm=(int32_t)RM;
	uint32_t _Rn=RN;

	if (sRm<0)
	{
		uint32_t shift=-sRm;
		if (shift>=32) shift=32;
		dsp_flag_c=(_Rn&0x80000000)>>31;
		while (shift)
		{
			_Rn<<=1;
			shift--;
		}
	}
	else
	{
		uint32_t shift=sRm;
		if (shift>=32) shift=32;
		dsp_flag_c=_Rn&0x1;
		while (shift)
		{
			_Rn=((int32_t)_Rn)>>1;
			shift--;
		}
	}
	RN = _Rn;
	SET_ZN(RN);
}

static void dsp_opcode_sharq(void)
{
	uint32_t res = (int32_t)RN >> dsp_convert_zero[IMM_1];
	SET_ZN(res); dsp_flag_c = RN & 0x01;
	RN = res;
}

static void dsp_opcode_sh(void)
{
	int32_t sRm=(int32_t)RM;
	uint32_t _Rn=RN;

	if (sRm<0)
	{
		uint32_t shift=(-sRm);
		if (shift>=32) shift=32;
		dsp_flag_c=(_Rn&0x80000000)>>31;
		while (shift)
		{
			_Rn<<=1;
			shift--;
		}
	}
	else
	{
		uint32_t shift=sRm;
		if (shift>=32) shift=32;
		dsp_flag_c=_Rn&0x1;
		while (shift)
		{
			_Rn>>=1;
			shift--;
		}
	}
	RN = _Rn;
	SET_ZN(RN);
}

void dsp_opcode_addqmod(void)
{
	uint32_t r1 = dsp_convert_zero[IMM_1];
	uint32_t r2 = RN;
	uint32_t res = r2 + r1;
	res = (res & (~dsp_modulo)) | (r2 & dsp_modulo);
	RN = res;
	SET_ZNC_ADD(r2, r1, res);
}

void dsp_opcode_subqmod(void)	
{
	uint32_t r1 = dsp_convert_zero[IMM_1];
	uint32_t r2 = RN;
	uint32_t res = r2 - r1;
	res = (res & (~dsp_modulo)) | (r2 & dsp_modulo);
	RN = res;
	
	SET_ZNC_SUB(r2, r1, res);
}

void dsp_opcode_mirror(void)	
{
	uint32_t r1 = RN;
	RN = (mirror_table[r1 & 0xFFFF] << 16) | mirror_table[r1 >> 16];
	SET_ZN(RN);
}

void dsp_opcode_sat32s(void)		
{
	int32_t r2 = (uint32_t)RN;
	int32_t temp = dsp_acc >> 32;
	uint32_t res = (temp < -1) ? (int32_t)0x80000000 : (temp > 0) ? (int32_t)0x7FFFFFFF : r2;
	RN = res;
	SET_ZN(res);
}

void dsp_opcode_sat16s(void)		
{
	int32_t r2 = RN;
	uint32_t res = (r2 < -32768) ? -32768 : (r2 > 32767) ? 32767 : r2;
	RN = res;
	SET_ZN(res);
}

//
// New pipelined DSP core
//

static void DSP_abs(void);
static void DSP_add(void);
static void DSP_addc(void);
static void DSP_addq(void);
static void DSP_addqmod(void);	
static void DSP_addqt(void);
static void DSP_and(void);
static void DSP_bclr(void);
static void DSP_bset(void);
static void DSP_btst(void);
static void DSP_cmp(void);
static void DSP_cmpq(void);
static void DSP_div(void);
static void DSP_imacn(void);
static void DSP_imult(void);
static void DSP_imultn(void);
static void DSP_illegal(void);
static void DSP_jr(void);
static void DSP_jump(void);
static void DSP_load(void);
static void DSP_loadb(void);
static void DSP_loadw(void);
static void DSP_load_r14_i(void);
static void DSP_load_r14_r(void);
static void DSP_load_r15_i(void);
static void DSP_load_r15_r(void);
static void DSP_mirror(void);	
static void DSP_mmult(void);
static void DSP_move(void);
static void DSP_movefa(void);
static void DSP_movei(void);
static void DSP_movepc(void);
static void DSP_moveq(void);
static void DSP_moveta(void);
static void DSP_mtoi(void);
static void DSP_mult(void);
static void DSP_neg(void);
static void DSP_nop(void);
static void DSP_normi(void);
static void DSP_not(void);
static void DSP_or(void);
static void DSP_resmac(void);
static void DSP_ror(void);
static void DSP_rorq(void);
static void DSP_sat16s(void);	
static void DSP_sat32s(void);	
static void DSP_sh(void);
static void DSP_sha(void);
static void DSP_sharq(void);
static void DSP_shlq(void);
static void DSP_shrq(void);
static void DSP_store(void);
static void DSP_storeb(void);
static void DSP_storew(void);
static void DSP_store_r14_i(void);
static void DSP_store_r14_r(void);
static void DSP_store_r15_i(void);
static void DSP_store_r15_r(void);
static void DSP_sub(void);
static void DSP_subc(void);
static void DSP_subq(void);
static void DSP_subqmod(void);	
static void DSP_subqt(void);
static void DSP_xor(void);

void (* DSPOpcode[64])() =
{
	DSP_add,			DSP_addc,			DSP_addq,			DSP_addqt,
	DSP_sub,			DSP_subc,			DSP_subq,			DSP_subqt,
	DSP_neg,			DSP_and,			DSP_or,				DSP_xor,
	DSP_not,			DSP_btst,			DSP_bset,			DSP_bclr,

	DSP_mult,			DSP_imult,			DSP_imultn,			DSP_resmac,
	DSP_imacn,			DSP_div,			DSP_abs,			DSP_sh,
	DSP_shlq,			DSP_shrq,			DSP_sha,			DSP_sharq,
	DSP_ror,			DSP_rorq,			DSP_cmp,			DSP_cmpq,

	DSP_subqmod,		DSP_sat16s,			DSP_move,			DSP_moveq,
	DSP_moveta,			DSP_movefa,			DSP_movei,			DSP_loadb,
	DSP_loadw,			DSP_load,			DSP_sat32s,			DSP_load_r14_i,
	DSP_load_r15_i,		DSP_storeb,			DSP_storew,			DSP_store,

	DSP_mirror,			DSP_store_r14_i,	DSP_store_r15_i,	DSP_movepc,
	DSP_jump,			DSP_jr,				DSP_mmult,			DSP_mtoi,
	DSP_normi,			DSP_nop,			DSP_load_r14_r,		DSP_load_r15_r,
	DSP_store_r14_r,	DSP_store_r15_r,	DSP_illegal,		DSP_addqmod
};

uint8_t readAffected[64][2] =
{
	{ true,  true}, { true,  true}, {false,  true}, {false,  true},
	{ true,  true}, { true,  true}, {false,  true}, {false,  true},
	{false,  true}, { true,  true}, { true,  true}, { true,  true},
	{false,  true}, {false,  true}, {false,  true}, {false,  true},

	{ true,  true}, { true,  true}, { true,  true}, {false,  true},
	{ true,  true}, { true,  true}, {false,  true}, { true,  true},
	{false,  true}, {false,  true}, { true,  true}, {false,  true},
	{ true,  true}, {false,  true}, { true,  true}, {false,  true},

	{false,  true}, {false,  true}, { true, false}, {false, false},
	{ true, false}, {false, false}, {false, false}, { true, false},
	{ true, false}, { true, false}, {false,  true}, { true, false},
	{ true, false}, { true,  true}, { true,  true}, { true,  true},

	{false,  true}, { true,  true}, { true,  true}, {false,  true},
	{ true, false}, { true, false}, { true,  true}, { true, false},
	{ true, false}, {false, false}, { true, false}, { true, false},
	{ true,  true}, { true,  true}, {false, false}, {false,  true}
};

uint8_t isLoadStore[65] =
{
	false, false, false, false, false, false, false, false,
	false, false, false, false, false, false, false, false,

	false, false, false, false, false, false, false, false,
	false, false, false, false, false, false, false, false,

	false, false, false, false, false, false, false,  true,
	 true,  true, false,  true,  true,  true,  true,  true,

	false,  true,  true, false, false, false, false, false,
	false, false,  true,  true,  true,  true, false, false, false
};

void FlushDSPPipeline(void)
{
	plPtrFetch = 3, plPtrRead = 2, plPtrExec = 1, plPtrWrite = 0;

	for(int i=0; i<4; i++)
		pipeline[i].opcode = PIPELINE_STALL;

	for(int i=0; i<32; i++)
		scoreboard[i] = 0;
}

uint32_t pcQueue1[0x400];
uint32_t pcQPtr1 = 0;

//Let's try a 3 stage pipeline....
//Looks like 3 stage is correct, otherwise bad things happen...
void DSPExecP2(int32_t cycles)
{
	while (cycles > 0 && DSP_RUNNING)
	{
		pcQueue1[pcQPtr1++] = dsp_pc;
		pcQPtr1 &= 0x3FF;

		if (IMASKCleared)						// If IMASK was cleared,
		{
			DSPHandleIRQs();					// See if any other interrupts are pending!
			IMASKCleared = false;
		}
		// Stage 1a: Instruction fetch
		pipeline[plPtrRead].instruction = DSPReadWord(dsp_pc, DSP);
		pipeline[plPtrRead].opcode = pipeline[plPtrRead].instruction >> 10;
		pipeline[plPtrRead].operand1 = (pipeline[plPtrRead].instruction >> 5) & 0x1F;
		pipeline[plPtrRead].operand2 = pipeline[plPtrRead].instruction & 0x1F;
		if (pipeline[plPtrRead].opcode == 38)
			pipeline[plPtrRead].result = (uint32_t)DSPReadWord(dsp_pc + 2, DSP)
				| ((uint32_t)DSPReadWord(dsp_pc + 4, DSP) << 16);

		if ((scoreboard[pipeline[plPtrRead].operand1] && readAffected[pipeline[plPtrRead].opcode][0])
			|| (scoreboard[pipeline[plPtrRead].operand2] && readAffected[pipeline[plPtrRead].opcode][1])
			|| ((pipeline[plPtrRead].opcode == 43 || pipeline[plPtrRead].opcode == 58) && scoreboard[14])
			|| ((pipeline[plPtrRead].opcode == 44 || pipeline[plPtrRead].opcode == 59) && scoreboard[15])
			|| (isLoadStore[pipeline[plPtrRead].opcode] && isLoadStore[pipeline[plPtrExec].opcode]))
			// We have a hit in the scoreboard, so we have to stall the pipeline...
			pipeline[plPtrRead].opcode = PIPELINE_STALL;

		else
		{
			pipeline[plPtrRead].reg1 = dsp_reg[pipeline[plPtrRead].operand1];
			pipeline[plPtrRead].reg2 = dsp_reg[pipeline[plPtrRead].operand2];
			pipeline[plPtrRead].writebackRegister = pipeline[plPtrRead].operand2;	// Set it to RN

			// Shouldn't we be more selective with the register scoreboarding?
			// Yes, we should. !!! FIX !!! Kinda [DONE]
#ifndef NEW_SCOREBOARD
			scoreboard[pipeline[plPtrRead].operand2] = affectsScoreboard[pipeline[plPtrRead].opcode];
#else
//Hopefully this will fix the dual MOVEQ # problem...
			scoreboard[pipeline[plPtrRead].operand2] += (affectsScoreboard[pipeline[plPtrRead].opcode] ? 1 : 0);
#endif
//Advance PC here??? Yes.
			dsp_pc += (pipeline[plPtrRead].opcode == 38 ? 6 : 2);
		}

		// Stage 2: Execute
		if (pipeline[plPtrExec].opcode != PIPELINE_STALL)
		{
			cycles -= dsp_opcode_cycles[pipeline[plPtrExec].opcode];
			DSPOpcode[pipeline[plPtrExec].opcode]();
		}
		else
		{
		// Stage 3: Write back register/memory address
		if (pipeline[plPtrWrite].opcode != PIPELINE_STALL)
		{
			if (pipeline[plPtrWrite].writebackRegister != 0xFF)
			{
				if (pipeline[plPtrWrite].writebackRegister != 0xFE)
					dsp_reg[pipeline[plPtrWrite].writebackRegister] = pipeline[plPtrWrite].result;
				else
				{
					if (pipeline[plPtrWrite].type == TYPE_BYTE)
						JaguarWriteByte(pipeline[plPtrWrite].address, pipeline[plPtrWrite].value, UNKNOWN);
					else if (pipeline[plPtrWrite].type == TYPE_WORD)
						JaguarWriteWord(pipeline[plPtrWrite].address, pipeline[plPtrWrite].value, UNKNOWN);
					else
						JaguarWriteLong(pipeline[plPtrWrite].address, pipeline[plPtrWrite].value, UNKNOWN);
				}
			}

#ifndef NEW_SCOREBOARD
			if (affectsScoreboard[pipeline[plPtrWrite].opcode])
				scoreboard[pipeline[plPtrWrite].operand2] = false;
#else
//Yup, sequential MOVEQ # problem fixing (I hope!)...
			if (affectsScoreboard[pipeline[plPtrWrite].opcode])
				if (scoreboard[pipeline[plPtrWrite].operand2])
					scoreboard[pipeline[plPtrWrite].operand2]--;
#endif
		}

		// Push instructions through the pipeline...
		plPtrRead = (++plPtrRead) & 0x03;
		plPtrExec = (++plPtrExec) & 0x03;
		plPtrWrite = (++plPtrWrite) & 0x03;
		}
	}
}

//
// DSP pipelined opcode handlers
//

#define PRM				pipeline[plPtrExec].reg1
#define PRN				pipeline[plPtrExec].reg2
#define PIMM1			pipeline[plPtrExec].operand1
#define PIMM2			pipeline[plPtrExec].operand2
#define PRES			pipeline[plPtrExec].result
#define PWBR			pipeline[plPtrExec].writebackRegister
#define NO_WRITEBACK	pipeline[plPtrExec].writebackRegister = 0xFF
//#define DSP_PPC			dsp_pc - (pipeline[plPtrRead].opcode == 38 ? 6 : 2) - (pipeline[plPtrExec].opcode == 38 ? 6 : 2)
#define DSP_PPC			dsp_pc - (pipeline[plPtrRead].opcode == 38 ? 6 : (pipeline[plPtrRead].opcode == PIPELINE_STALL ? 0 : 2)) - (pipeline[plPtrExec].opcode == 38 ? 6 : (pipeline[plPtrExec].opcode == PIPELINE_STALL ? 0 : 2))
#define WRITEBACK_ADDR	pipeline[plPtrExec].writebackRegister = 0xFE

static void DSP_abs(void)
{
	uint32_t _Rn = PRN;
	
	if (_Rn == 0x80000000)
		dsp_flag_n = 1;
	else
	{
		dsp_flag_c = ((_Rn & 0x80000000) >> 31);
		PRES = (_Rn & 0x80000000 ? -_Rn : _Rn);
		CLR_ZN; SET_Z(PRES);
	}
}

static void DSP_add(void)
{
	uint32_t res = PRN + PRM;
	SET_ZNC_ADD(PRN, PRM, res);
	PRES = res;
}

static void DSP_addc(void)
{
	uint32_t res = PRN + PRM + dsp_flag_c;
	uint32_t carry = dsp_flag_c;
	SET_ZNC_ADD(PRN + carry, PRM, res);
	PRES = res;
}

static void DSP_addq(void)
{
	uint32_t r1 = dsp_convert_zero[PIMM1];
	uint32_t res = PRN + r1;
	CLR_ZNC; SET_ZNC_ADD(PRN, r1, res);
	PRES = res;
}

static void DSP_addqmod(void)
{
	uint32_t r1 = dsp_convert_zero[PIMM1];
	uint32_t r2 = PRN;
	uint32_t res = r2 + r1;
	res = (res & (~dsp_modulo)) | (r2 & dsp_modulo);
	PRES = res;
	SET_ZNC_ADD(r2, r1, res);
}

static void DSP_addqt(void)
{
	PRES = PRN + dsp_convert_zero[PIMM1];
}

static void DSP_and(void)
{
	PRES = PRN & PRM;
	SET_ZN(PRES);
}

static void DSP_bclr(void)
{
	PRES = PRN & ~(1 << PIMM1);
	SET_ZN(PRES);
}

static void DSP_bset(void)
{
	PRES = PRN | (1 << PIMM1);
	SET_ZN(PRES);
}

static void DSP_btst(void)
{
	dsp_flag_z = (~PRN >> PIMM1) & 1;
	NO_WRITEBACK;
}

static void DSP_cmp(void)
{
	uint32_t res = PRN - PRM;
	SET_ZNC_SUB(PRN, PRM, res);
	NO_WRITEBACK;
}

static void DSP_cmpq(void)
{
	static int32_t sqtable[32] =
		{ 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,-16,-15,-14,-13,-12,-11,-10,-9,-8,-7,-6,-5,-4,-3,-2,-1 };
	uint32_t r1 = sqtable[PIMM1 & 0x1F]; // I like this better -> (int8_t)(jaguar.op >> 2) >> 3;
	uint32_t res = PRN - r1;
	SET_ZNC_SUB(PRN, r1, res);
	NO_WRITEBACK;
}

static void DSP_div(void)
{
	uint32_t _Rm = PRM, _Rn = PRN;

	if (_Rm)
	{
		if (dsp_div_control & 1)
		{
			dsp_remain = (((uint64_t)_Rn) << 16) % _Rm;
			if (dsp_remain & 0x80000000)
				dsp_remain -= _Rm;
			PRES = (((uint64_t)_Rn) << 16) / _Rm;
		}
		else
		{
			dsp_remain = _Rn % _Rm;
			if (dsp_remain & 0x80000000)
				dsp_remain -= _Rm;
			PRES = PRN / _Rm;
		}
	}
	else
		PRES = 0xFFFFFFFF;
}

static void DSP_imacn(void)
{
	int32_t res = (int16_t)PRM * (int16_t)PRN;
	dsp_acc += (int64_t) res;
	NO_WRITEBACK;
} 

static void DSP_imult(void)
{
	PRES = (int16_t)PRN * (int16_t)PRM;
	SET_ZN(PRES);
}

static void DSP_imultn(void)
{
	// This is OK, since this multiply won't overflow 32 bits...
	int32_t res = (int32_t)((int16_t)PRN * (int16_t)PRM);
	dsp_acc = (int64_t) res;
	SET_ZN(res);
	NO_WRITEBACK;
}

static void DSP_illegal(void)
{
	NO_WRITEBACK;
}

// There is a problem here with interrupt handlers the JUMP and JR instructions that
// can cause trouble because an interrupt can occur *before* the instruction following the
// jump can execute... !!! FIX !!!
// This can probably be solved by judicious coding in the pipeline execution core...
// And should be fixed now...
static void DSP_jr(void)
{
	// KLUDGE: Used by BRANCH_CONDITION macro
	uint32_t jaguar_flags = (dsp_flag_n << 2) | (dsp_flag_c << 1) | dsp_flag_z;

	if (BRANCH_CONDITION(PIMM2))
	{
		int32_t offset = (PIMM1 & 0x10 ? 0xFFFFFFF0 | PIMM1 : PIMM1);		// Sign extend PIMM1
		//Account for pipeline effects...
		uint32_t newPC = dsp_pc + (offset * 2) - (pipeline[plPtrRead].opcode == 38 ? 6 : (pipeline[plPtrRead].opcode == PIPELINE_STALL ? 0 : 2));

		if (pipeline[plPtrWrite].opcode != PIPELINE_STALL)
		{
			if (pipeline[plPtrWrite].writebackRegister != 0xFF)
			{
				if (pipeline[plPtrWrite].writebackRegister != 0xFE)
					dsp_reg[pipeline[plPtrWrite].writebackRegister] = pipeline[plPtrWrite].result;
				else
				{
					if (pipeline[plPtrWrite].type == TYPE_BYTE)
						JaguarWriteByte(pipeline[plPtrWrite].address, pipeline[plPtrWrite].value, UNKNOWN);
					else if (pipeline[plPtrWrite].type == TYPE_WORD)
						JaguarWriteWord(pipeline[plPtrWrite].address, pipeline[plPtrWrite].value, UNKNOWN);
					else
						JaguarWriteLong(pipeline[plPtrWrite].address, pipeline[plPtrWrite].value, UNKNOWN);
				}
			}

#ifndef NEW_SCOREBOARD
			if (affectsScoreboard[pipeline[plPtrWrite].opcode])
				scoreboard[pipeline[plPtrWrite].operand2] = false;
#else
			//Yup, sequential MOVEQ # problem fixing (I hope!)...
			if (affectsScoreboard[pipeline[plPtrWrite].opcode])
				if (scoreboard[pipeline[plPtrWrite].operand2])
					scoreboard[pipeline[plPtrWrite].operand2]--;
#endif
		}

		// Step 2: Push instruction through pipeline & execute following instruction
		// NOTE: By putting our following instruction at stage 3 of the pipeline,
		//       we effectively handle the final push of the instruction through the
		//       pipeline when the new PC takes effect (since when we return, the
		//       pipeline code will be executing the writeback stage. If we reverse
		//       the execution order of the pipeline stages, this will no longer be
		//       the case!)...
		pipeline[plPtrExec] = pipeline[plPtrRead];
//This is BAD. We need to get that next opcode and execute it!
//NOTE: The problem is here because of a bad stall. Once those are fixed, we can probably
//      remove this crap.
		if (pipeline[plPtrExec].opcode == PIPELINE_STALL)
		{
		uint16_t instruction = DSPReadWord(dsp_pc, DSP);
		pipeline[plPtrExec].opcode = instruction >> 10;
		pipeline[plPtrExec].operand1 = (instruction >> 5) & 0x1F;
		pipeline[plPtrExec].operand2 = instruction & 0x1F;
			pipeline[plPtrExec].reg1 = dsp_reg[pipeline[plPtrExec].operand1];
			pipeline[plPtrExec].reg2 = dsp_reg[pipeline[plPtrExec].operand2];
			pipeline[plPtrExec].writebackRegister = pipeline[plPtrExec].operand2;	// Set it to RN
		}//*/
	dsp_pc += 2;	// For DSP_DIS_* accuracy
		DSPOpcode[pipeline[plPtrExec].opcode]();
		pipeline[plPtrWrite] = pipeline[plPtrExec];

		// Step 3: Flush pipeline & set new PC
		pipeline[plPtrRead].opcode = pipeline[plPtrExec].opcode = PIPELINE_STALL;
		dsp_pc = newPC;
	}
	else
		NO_WRITEBACK;
//	//WriteLog("  --> DSP_PC: %08X\n", dsp_pc);
}

static void DSP_jump(void)
{
	// KLUDGE: Used by BRANCH_CONDITION macro
	uint32_t jaguar_flags = (dsp_flag_n << 2) | (dsp_flag_c << 1) | dsp_flag_z;

	if (BRANCH_CONDITION(PIMM2))
	{
		uint32_t PCSave = PRM;
		// Now that we've branched, we have to make sure that the following instruction
		// is executed atomically with this one and then flush the pipeline before setting
		// the new PC.
		
		// Step 1: Handle writebacks at stage 3 of pipeline
/*		if (pipeline[plPtrWrite].opcode != PIPELINE_STALL)
		{
			if (pipeline[plPtrWrite].writebackRegister != 0xFF)
				dsp_reg[pipeline[plPtrWrite].writebackRegister] = pipeline[plPtrWrite].result;

			if (affectsScoreboard[pipeline[plPtrWrite].opcode])
				scoreboard[pipeline[plPtrWrite].operand2] = false;
		}//*/
		if (pipeline[plPtrWrite].opcode != PIPELINE_STALL)
		{
			if (pipeline[plPtrWrite].writebackRegister != 0xFF)
			{
				if (pipeline[plPtrWrite].writebackRegister != 0xFE)
					dsp_reg[pipeline[plPtrWrite].writebackRegister] = pipeline[plPtrWrite].result;
				else
				{
					if (pipeline[plPtrWrite].type == TYPE_BYTE)
						JaguarWriteByte(pipeline[plPtrWrite].address, pipeline[plPtrWrite].value, UNKNOWN);
					else if (pipeline[plPtrWrite].type == TYPE_WORD)
						JaguarWriteWord(pipeline[plPtrWrite].address, pipeline[plPtrWrite].value, UNKNOWN);
					else
						JaguarWriteLong(pipeline[plPtrWrite].address, pipeline[plPtrWrite].value, UNKNOWN);
				}
			}

#ifndef NEW_SCOREBOARD
			if (affectsScoreboard[pipeline[plPtrWrite].opcode])
				scoreboard[pipeline[plPtrWrite].operand2] = false;
#else
//Yup, sequential MOVEQ # problem fixing (I hope!)...
			if (affectsScoreboard[pipeline[plPtrWrite].opcode])
				if (scoreboard[pipeline[plPtrWrite].operand2])
					scoreboard[pipeline[plPtrWrite].operand2]--;
#endif
		}

		// Step 2: Push instruction through pipeline & execute following instruction
		// NOTE: By putting our following instruction at stage 3 of the pipeline,
		//       we effectively handle the final push of the instruction through the
		//       pipeline when the new PC takes effect (since when we return, the
		//       pipeline code will be executing the writeback stage. If we reverse
		//       the execution order of the pipeline stages, this will no longer be
		//       the case!)...
		pipeline[plPtrExec] = pipeline[plPtrRead];
//This is BAD. We need to get that next opcode and execute it!
//Also, same problem in JR!
//NOTE: The problem is here because of a bad stall. Once those are fixed, we can probably
//      remove this crap.
		if (pipeline[plPtrExec].opcode == PIPELINE_STALL)
		{
		uint16_t instruction = DSPReadWord(dsp_pc, DSP);
		pipeline[plPtrExec].opcode = instruction >> 10;
		pipeline[plPtrExec].operand1 = (instruction >> 5) & 0x1F;
		pipeline[plPtrExec].operand2 = instruction & 0x1F;
			pipeline[plPtrExec].reg1 = dsp_reg[pipeline[plPtrExec].operand1];
			pipeline[plPtrExec].reg2 = dsp_reg[pipeline[plPtrExec].operand2];
			pipeline[plPtrExec].writebackRegister = pipeline[plPtrExec].operand2;	// Set it to RN
		}//*/
	dsp_pc += 2;	// For DSP_DIS_* accuracy
		DSPOpcode[pipeline[plPtrExec].opcode]();
		pipeline[plPtrWrite] = pipeline[plPtrExec];

		// Step 3: Flush pipeline & set new PC
		pipeline[plPtrRead].opcode = pipeline[plPtrExec].opcode = PIPELINE_STALL;
		dsp_pc = PCSave;
	}
	else
		NO_WRITEBACK;
}

static void DSP_load(void)
{
	PRES = DSPReadLong(PRM, DSP);
}

static void DSP_loadb(void)
{
	if (PRM >= DSP_WORK_RAM_BASE && PRM <= (DSP_WORK_RAM_BASE + 0x1FFF))
		PRES = DSPReadLong(PRM, DSP) & 0xFF;
	else
		PRES = JaguarReadByte(PRM, DSP);
}

static void DSP_loadw(void)
{
	if (PRM >= DSP_WORK_RAM_BASE && PRM <= (DSP_WORK_RAM_BASE + 0x1FFF))
		PRES = DSPReadLong(PRM, DSP) & 0xFFFF;
	else
		PRES = JaguarReadWord(PRM, DSP);
}

static void DSP_load_r14_i(void)
{
	PRES = DSPReadLong(dsp_reg[14] + (dsp_convert_zero[PIMM1] << 2), DSP);
}

static void DSP_load_r14_r(void)
{
	PRES = DSPReadLong(dsp_reg[14] + PRM, DSP);
}

static void DSP_load_r15_i(void)
{
	PRES = DSPReadLong(dsp_reg[15] + (dsp_convert_zero[PIMM1] << 2), DSP);
}

static void DSP_load_r15_r(void)
{
	PRES = DSPReadLong(dsp_reg[15] + PRM, DSP);
}

static void DSP_mirror(void)	
{
	uint32_t r1 = PRN;
	PRES = (mirror_table[r1 & 0xFFFF] << 16) | mirror_table[r1 >> 16];
	SET_ZN(PRES);
}

static void DSP_mmult(void)
{
	int count	= dsp_matrix_control&0x0f;
	uint32_t addr = dsp_pointer_to_matrix; // in the gpu ram
	int64_t  accum = 0;
	uint32_t res;

	if (!(dsp_matrix_control & 0x10))
	{
		for (int i = 0; i < count; i++)
		{ 
			int16_t a;
			if (i&0x01)
				a=(int16_t)((dsp_alternate_reg[dsp_opcode_first_parameter + (i>>1)]>>16)&0xffff);
			else
				a=(int16_t)(dsp_alternate_reg[dsp_opcode_first_parameter + (i>>1)]&0xffff);
			int16_t b=((int16_t)DSPReadWord(addr + 2, DSP));
			accum += a*b;
			addr += 4;
		}
	}
	else
	{
		for (int i = 0; i < count; i++)
		{
			int16_t a;
			if (i&0x01)
				a=(int16_t)((dsp_alternate_reg[dsp_opcode_first_parameter + (i>>1)]>>16)&0xffff);
			else
				a=(int16_t)(dsp_alternate_reg[dsp_opcode_first_parameter + (i>>1)]&0xffff);
			int16_t b=((int16_t)DSPReadWord(addr + 2, DSP));
			accum += a*b;
			addr += 4 * count;
		}
	}

	PRES = res = (int32_t)accum;
	// carry flag to do
//NOTE: The flags are set based upon the last add/multiply done...
	SET_ZN(PRES);
}

static void DSP_move(void)
{
	PRES = PRM;
}

static void DSP_movefa(void)
{
	PRES = dsp_alternate_reg[PIMM1];
}

static void DSP_movei(void)
{
//	// This instruction is followed by 32-bit value in LSW / MSW format...
//	PRES = (uint32_t)DSPReadWord(dsp_pc, DSP) | ((uint32_t)DSPReadWord(dsp_pc + 2, DSP) << 16);
//	dsp_pc += 4;
}

static void DSP_movepc(void)
{
//Need to fix this to take into account pipelining effects... !!! FIX !!! [DONE]
//	PRES = dsp_pc - 2;
//Account for pipeline effects...
	PRES = dsp_pc - 2 - (pipeline[plPtrRead].opcode == 38 ? 6 : (pipeline[plPtrRead].opcode == PIPELINE_STALL ? 0 : 2));
}

static void DSP_moveq(void)
{
	PRES = PIMM1;
}

static void DSP_moveta(void)
{
//	ALTERNATE_RN = PRM;
	dsp_alternate_reg[PIMM2] = PRM;
	NO_WRITEBACK;
}

static void DSP_mtoi(void)
{
	PRES = (((int32_t)PRM >> 8) & 0xFF800000) | (PRM & 0x007FFFFF);
	SET_ZN(PRES);
}

static void DSP_mult(void)
{
	PRES = (uint16_t)PRM * (uint16_t)PRN;
	SET_ZN(PRES);
}

static void DSP_neg(void)
{
	uint32_t res = -PRN;
	SET_ZNC_SUB(0, PRN, res);
	PRES = res;
}

static void DSP_nop(void)
{
	NO_WRITEBACK;
}

static void DSP_normi(void)
{
	uint32_t _Rm = PRM;
	uint32_t res = 0;

	if (_Rm)
	{
		while ((_Rm & 0xffc00000) == 0)
		{
			_Rm <<= 1;
			res--;
		}
		while ((_Rm & 0xff800000) != 0)
		{
			_Rm >>= 1;
			res++;
		}
	}
	PRES = res;
	SET_ZN(PRES);
}

static void DSP_not(void)
{
	PRES = ~PRN;
	SET_ZN(PRES);
}

static void DSP_or(void)
{
	PRES = PRN | PRM;
	SET_ZN(PRES);
}

static void DSP_resmac(void)
{
	PRES = (uint32_t)dsp_acc;
}

static void DSP_ror(void)
{
	uint32_t r1 = PRM & 0x1F;
	uint32_t res = (PRN >> r1) | (PRN << (32 - r1));
	SET_ZN(res); dsp_flag_c = (PRN >> 31) & 1;
	PRES = res;
}

static void DSP_rorq(void)
{
	uint32_t r1 = dsp_convert_zero[PIMM1 & 0x1F];
	uint32_t r2 = PRN;
	uint32_t res = (r2 >> r1) | (r2 << (32 - r1));
	PRES = res;
	SET_ZN(res); dsp_flag_c = (r2 >> 31) & 0x01;
}

static void DSP_sat16s(void)		
{
	int32_t r2 = PRN;
	uint32_t res = (r2 < -32768) ? -32768 : (r2 > 32767) ? 32767 : r2;
	PRES = res;
	SET_ZN(res);
}

static void DSP_sat32s(void)		
{
	int32_t r2 = (uint32_t)PRN;
	int32_t temp = dsp_acc >> 32;
	uint32_t res = (temp < -1) ? (int32_t)0x80000000 : (temp > 0) ? (int32_t)0x7FFFFFFF : r2;
	PRES = res;
	SET_ZN(res);
}

static void DSP_sh(void)
{
	int32_t sRm = (int32_t)PRM;
	uint32_t _Rn = PRN;

	if (sRm < 0)
	{
		uint32_t shift = -sRm;

		if (shift >= 32)
			shift = 32;

		dsp_flag_c = (_Rn & 0x80000000) >> 31;

		while (shift)
		{
			_Rn <<= 1;
			shift--;
		}
	}
	else
	{
		uint32_t shift = sRm;

		if (shift >= 32)
			shift = 32;

		dsp_flag_c = _Rn & 0x1;

		while (shift)
		{
			_Rn >>= 1;
			shift--;
		}
	}

	PRES = _Rn;
	SET_ZN(PRES);
}

static void DSP_sha(void)
{
	int32_t sRm = (int32_t)PRM;
	uint32_t _Rn = PRN;

	if (sRm < 0)
	{
		uint32_t shift = -sRm;

		if (shift >= 32)
			shift = 32;

		dsp_flag_c = (_Rn & 0x80000000) >> 31;

		while (shift)
		{
			_Rn <<= 1;
			shift--;
		}
	}
	else
	{
		uint32_t shift = sRm;

		if (shift >= 32)
			shift = 32;

		dsp_flag_c = _Rn & 0x1;

		while (shift)
		{
			_Rn = ((int32_t)_Rn) >> 1;
			shift--;
		}
	}

	PRES = _Rn;
	SET_ZN(PRES);
}

static void DSP_sharq(void)
{
	uint32_t res = (int32_t)PRN >> dsp_convert_zero[PIMM1];
	SET_ZN(res); dsp_flag_c = PRN & 0x01;
	PRES = res;
}

static void DSP_shlq(void)
{
	int32_t r1 = 32 - PIMM1;
	uint32_t res = PRN << r1;
	SET_ZN(res); dsp_flag_c = (PRN >> 31) & 1;
	PRES = res;
}

static void DSP_shrq(void)
{
	int32_t r1 = dsp_convert_zero[PIMM1];
	uint32_t res = PRN >> r1;
	SET_ZN(res); dsp_flag_c = PRN & 1;
	PRES = res;
}

static void DSP_store(void)
{
//	DSPWriteLong(PRM, PRN, DSP);
//	NO_WRITEBACK;
	pipeline[plPtrExec].address = PRM;
	pipeline[plPtrExec].value = PRN;
	pipeline[plPtrExec].type = TYPE_DWORD;
	WRITEBACK_ADDR;
}

static void DSP_storeb(void)
{
	pipeline[plPtrExec].address = PRM;

	if (PRM >= DSP_WORK_RAM_BASE && PRM <= (DSP_WORK_RAM_BASE + 0x1FFF))
	{
		pipeline[plPtrExec].value = PRN & 0xFF;
		pipeline[plPtrExec].type = TYPE_DWORD;
	}
	else
	{
		pipeline[plPtrExec].value = PRN;
		pipeline[plPtrExec].type = TYPE_BYTE;
	}

	WRITEBACK_ADDR;
}

static void DSP_storew(void)
{
	pipeline[plPtrExec].address = PRM;

	if (PRM >= DSP_WORK_RAM_BASE && PRM <= (DSP_WORK_RAM_BASE + 0x1FFF))
	{
		pipeline[plPtrExec].value = PRN & 0xFFFF;
		pipeline[plPtrExec].type = TYPE_DWORD;
	}
	else
	{
		pipeline[plPtrExec].value = PRN;
		pipeline[plPtrExec].type = TYPE_WORD;
	}
	WRITEBACK_ADDR;
}

static void DSP_store_r14_i(void)
{
	pipeline[plPtrExec].address = dsp_reg[14] + (dsp_convert_zero[PIMM1] << 2);
	pipeline[plPtrExec].value = PRN;
	pipeline[plPtrExec].type = TYPE_DWORD;
	WRITEBACK_ADDR;
}

static void DSP_store_r14_r(void)
{
	pipeline[plPtrExec].address = dsp_reg[14] + PRM;
	pipeline[plPtrExec].value = PRN;
	pipeline[plPtrExec].type = TYPE_DWORD;
	WRITEBACK_ADDR;
}

static void DSP_store_r15_i(void)
{
	pipeline[plPtrExec].address = dsp_reg[15] + (dsp_convert_zero[PIMM1] << 2);
	pipeline[plPtrExec].value = PRN;
	pipeline[plPtrExec].type = TYPE_DWORD;
	WRITEBACK_ADDR;
}

static void DSP_store_r15_r(void)
{
	pipeline[plPtrExec].address = dsp_reg[15] + PRM;
	pipeline[plPtrExec].value = PRN;
	pipeline[plPtrExec].type = TYPE_DWORD;
	WRITEBACK_ADDR;
}

static void DSP_sub(void)
{
	uint32_t res = PRN - PRM;
	SET_ZNC_SUB(PRN, PRM, res);
	PRES = res;
}

static void DSP_subc(void)
{
	uint32_t res = PRN - PRM - dsp_flag_c;
	uint32_t borrow = dsp_flag_c;
	SET_ZNC_SUB(PRN - borrow, PRM, res);
	PRES = res;
}

static void DSP_subq(void)
{
	uint32_t r1 = dsp_convert_zero[PIMM1];
	uint32_t res = PRN - r1;
	SET_ZNC_SUB(PRN, r1, res);
	PRES = res;
}

static void DSP_subqmod(void)	
{
	uint32_t r1 = dsp_convert_zero[PIMM1];
	uint32_t r2 = PRN;
	uint32_t res = r2 - r1;
	res = (res & (~dsp_modulo)) | (r2 & dsp_modulo);
	PRES = res;
	SET_ZNC_SUB(r2, r1, res);
}

static void DSP_subqt(void)
{
	PRES = PRN - dsp_convert_zero[PIMM1];
}

static void DSP_xor(void)
{
	PRES = PRN ^ PRM;
}

