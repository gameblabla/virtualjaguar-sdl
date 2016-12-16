//
// GPU Core
//
// Originally by David Raingeard (Cal2)
// GCC/SDL port by Niels Wagenaar (Linux/WIN32) and Caz (BeOS)
// Cleanups, endian wrongness, and bad ASM amelioration by James Hammons
// (C) 2010 Underground Software
//
// JLH = James Hammons <jlhamm@acm.org>
//
// Who  When        What
// ---  ----------  -------------------------------------------------------------
// JLH  01/16/2010  Created this log ;-)
// JLH  11/26/2011  Added fixes for LOAD/STORE alignment issues

//
// Note: Endian wrongness probably stems from the MAME origins of this emu and
//       the braindead way in which MAME handles memory. :-)
//
// Problem with not booting the BIOS was the incorrect way that the
// SUBC instruction set the carry when the carry was set going in...
// Same problem with ADDC...
//

#include "gpu.h"

#include <stdlib.h>
#include <string.h>	
#include <stdint.h>							// For memset
#include "dsp.h"
#include "jaguar.h"
//#include "memory.h"
#include "tom.h"

void GPUReset(void);
void GPUResetStats();

// Seems alignment in loads & stores was off...
#define GPU_CORRECT_ALIGNMENT
//#define GPU_DEBUG

/*
GPU opcodes use (BIOS flying ATARI logo):
+	              add 357416
+	             addq 538030
+	            addqt 6999
+	              sub 116663
+	             subq 188059
+	            subqt 15086
+	              neg 36097
+	              and 233993
+	               or 109332
+	              xor 1384
+	             btst 111924
+	             bset 25029
+	             bclr 10551
+	             mult 28147
+	            imult 69148
+	              div 64102
+	              abs 159394
+	             shlq 194690
+	             shrq 292587
+	            sharq 192649
+	             rorq 58672
+	              cmp 244963
+	             cmpq 114834
+	             move 833472
+	            moveq 56427
+	           moveta 220814
+	           movefa 170678
+	            movei 152025
+	            loadw 108220
+	             load 430936
+	           storew 3036
+	            store 372490
+	          move_pc 2330
+	             jump 349134
+	               jr 529171
	            mmult 64904
+	              nop 432179
*/

// Various bits

#define CINT0FLAG			0x0200
#define CINT1FLAG			0x0400
#define CINT2FLAG			0x0800
#define CINT3FLAG			0x1000
#define CINT4FLAG			0x2000
#define CINT04FLAGS			(CINT0FLAG | CINT1FLAG | CINT2FLAG | CINT3FLAG | CINT4FLAG)

// GPU_FLAGS bits

#define ZERO_FLAG		0x0001
#define CARRY_FLAG		0x0002
#define NEGA_FLAG		0x0004
#define IMASK			0x0008
#define INT_ENA0		0x0010
#define INT_ENA1		0x0020
#define INT_ENA2		0x0040
#define INT_ENA3		0x0080
#define INT_ENA4		0x0100
#define INT_CLR0		0x0200
#define INT_CLR1		0x0400
#define INT_CLR2		0x0800
#define INT_CLR3		0x1000
#define INT_CLR4		0x2000
#define REGPAGE			0x4000
#define DMAEN			0x8000

// Private function prototypes

void GPUUpdateRegisterBanks(void);
void GPUDumpDisassembly(void);
void GPUDumpRegisters(void);
void GPUDumpMemory(void);

static void gpu_opcode_add(void);
static void gpu_opcode_addc(void);
static void gpu_opcode_addq(void);
static void gpu_opcode_addqt(void);
static void gpu_opcode_sub(void);
static void gpu_opcode_subc(void);
static void gpu_opcode_subq(void);
static void gpu_opcode_subqt(void);
static void gpu_opcode_neg(void);
static void gpu_opcode_and(void);
static void gpu_opcode_or(void);
static void gpu_opcode_xor(void);
static void gpu_opcode_not(void);
static void gpu_opcode_btst(void);
static void gpu_opcode_bset(void);
static void gpu_opcode_bclr(void);
static void gpu_opcode_mult(void);
static void gpu_opcode_imult(void);
static void gpu_opcode_imultn(void);
static void gpu_opcode_resmac(void);
static void gpu_opcode_imacn(void);
static void gpu_opcode_div(void);
static void gpu_opcode_abs(void);
static void gpu_opcode_sh(void);
static void gpu_opcode_shlq(void);
static void gpu_opcode_shrq(void);
static void gpu_opcode_sha(void);
static void gpu_opcode_sharq(void);
static void gpu_opcode_ror(void);
static void gpu_opcode_rorq(void);
static void gpu_opcode_cmp(void);
static void gpu_opcode_cmpq(void);
static void gpu_opcode_sat8(void);
static void gpu_opcode_sat16(void);
static void gpu_opcode_move(void);
static void gpu_opcode_moveq(void);
static void gpu_opcode_moveta(void);
static void gpu_opcode_movefa(void);
static void gpu_opcode_movei(void);
static void gpu_opcode_loadb(void);
static void gpu_opcode_loadw(void);
static void gpu_opcode_load(void);
static void gpu_opcode_loadp(void);
static void gpu_opcode_load_r14_indexed(void);
static void gpu_opcode_load_r15_indexed(void);
static void gpu_opcode_storeb(void);
static void gpu_opcode_storew(void);
static void gpu_opcode_store(void);
static void gpu_opcode_storep(void);
static void gpu_opcode_store_r14_indexed(void);
static void gpu_opcode_store_r15_indexed(void);
static void gpu_opcode_move_pc(void);
static void gpu_opcode_jump(void);
static void gpu_opcode_jr(void);
static void gpu_opcode_mmult(void);
static void gpu_opcode_mtoi(void);
static void gpu_opcode_normi(void);
static void gpu_opcode_nop(void);
static void gpu_opcode_load_r14_ri(void);
static void gpu_opcode_load_r15_ri(void);
static void gpu_opcode_store_r14_ri(void);
static void gpu_opcode_store_r15_ri(void);
static void gpu_opcode_sat24(void);
static void gpu_opcode_pack(void);

// This is wrong, since it doesn't take pipeline effects into account. !!! FIX !!!
/*uint8_t gpu_opcode_cycles[64] =
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
/*uint8_t gpu_opcode_cycles[64] =
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
uint8_t gpu_opcode_cycles[64] =
{
	1,  1,  1,  1,  1,  1,  1,  1,
	1,  1,  1,  1,  1,  1,  1,  1,
	1,  1,  1,  1,  1,  1,  1,  1,
	1,  1,  1,  1,  1,  1,  1,  1,
	1,  1,  1,  1,  1,  1,  1,  1,
	1,  1,  1,  1,  1,  1,  1,  1,
	1,  1,  1,  1,  1,  1,  1,  1,
	1,  1,  1,  1,  1,  1,  1,  1
};//*/

void (*gpu_opcode[64])()=
{
	gpu_opcode_add,					gpu_opcode_addc,				gpu_opcode_addq,				gpu_opcode_addqt,
	gpu_opcode_sub,					gpu_opcode_subc,				gpu_opcode_subq,				gpu_opcode_subqt,
	gpu_opcode_neg,					gpu_opcode_and,					gpu_opcode_or,					gpu_opcode_xor,
	gpu_opcode_not,					gpu_opcode_btst,				gpu_opcode_bset,				gpu_opcode_bclr,
	gpu_opcode_mult,				gpu_opcode_imult,				gpu_opcode_imultn,				gpu_opcode_resmac,
	gpu_opcode_imacn,				gpu_opcode_div,					gpu_opcode_abs,					gpu_opcode_sh,
	gpu_opcode_shlq,				gpu_opcode_shrq,				gpu_opcode_sha,					gpu_opcode_sharq,
	gpu_opcode_ror,					gpu_opcode_rorq,				gpu_opcode_cmp,					gpu_opcode_cmpq,
	gpu_opcode_sat8,				gpu_opcode_sat16,				gpu_opcode_move,				gpu_opcode_moveq,
	gpu_opcode_moveta,				gpu_opcode_movefa,				gpu_opcode_movei,				gpu_opcode_loadb,
	gpu_opcode_loadw,				gpu_opcode_load,				gpu_opcode_loadp,				gpu_opcode_load_r14_indexed,
	gpu_opcode_load_r15_indexed,	gpu_opcode_storeb,				gpu_opcode_storew,				gpu_opcode_store,
	gpu_opcode_storep,				gpu_opcode_store_r14_indexed,	gpu_opcode_store_r15_indexed,	gpu_opcode_move_pc,
	gpu_opcode_jump,				gpu_opcode_jr,					gpu_opcode_mmult,				gpu_opcode_mtoi,
	gpu_opcode_normi,				gpu_opcode_nop,					gpu_opcode_load_r14_ri,			gpu_opcode_load_r15_ri,
	gpu_opcode_store_r14_ri,		gpu_opcode_store_r15_ri,		gpu_opcode_sat24,				gpu_opcode_pack,
};

static uint8_t gpu_ram_8[0x1000];
uint32_t gpu_pc;
static uint32_t gpu_acc;
static uint32_t gpu_remain;
static uint32_t gpu_hidata;
static uint32_t gpu_flags;
static uint32_t gpu_matrix_control;
static uint32_t gpu_pointer_to_matrix;
static uint32_t gpu_data_organization;
static uint32_t gpu_control;
static uint32_t gpu_div_control;
// There is a distinct advantage to having these separated out--there's no need to clear
// a bit before writing a result. I.e., if the result of an operation leaves a zero in
// the carry flag, you don't have to zero gpu_flag_c before you can write that zero!
static uint8_t gpu_flag_z, gpu_flag_n, gpu_flag_c;
uint32_t gpu_reg_bank_0[32];
uint32_t gpu_reg_bank_1[32];
static uint32_t * gpu_reg;
static uint32_t * gpu_alternate_reg;

static uint32_t gpu_instruction;
static uint32_t gpu_opcode_first_parameter;
static uint32_t gpu_opcode_second_parameter;

#define GPU_RUNNING		(gpu_control & 0x01)

#define RM				gpu_reg[gpu_opcode_first_parameter]
#define RN				gpu_reg[gpu_opcode_second_parameter]
#define ALTERNATE_RM	gpu_alternate_reg[gpu_opcode_first_parameter]
#define ALTERNATE_RN	gpu_alternate_reg[gpu_opcode_second_parameter]
#define IMM_1			gpu_opcode_first_parameter
#define IMM_2			gpu_opcode_second_parameter

#define SET_FLAG_Z(r)	(gpu_flag_z = ((r) == 0));
#define SET_FLAG_N(r)	(gpu_flag_n = (((uint32_t)(r) >> 31) & 0x01));

#define RESET_FLAG_Z()	gpu_flag_z = 0;
#define RESET_FLAG_N()	gpu_flag_n = 0;
#define RESET_FLAG_C()	gpu_flag_c = 0;

#define CLR_Z				(gpu_flag_z = 0)
#define CLR_ZN				(gpu_flag_z = gpu_flag_n = 0)
#define CLR_ZNC				(gpu_flag_z = gpu_flag_n = gpu_flag_c = 0)
#define SET_Z(r)			(gpu_flag_z = ((r) == 0))
#define SET_N(r)			(gpu_flag_n = (((uint32_t)(r) >> 31) & 0x01))
#define SET_C_ADD(a,b)		(gpu_flag_c = ((uint32_t)(b) > (uint32_t)(~(a))))
#define SET_C_SUB(a,b)		(gpu_flag_c = ((uint32_t)(b) > (uint32_t)(a)))
#define SET_ZN(r)			SET_N(r); SET_Z(r)
#define SET_ZNC_ADD(a,b,r)	SET_N(r); SET_Z(r); SET_C_ADD(a,b)
#define SET_ZNC_SUB(a,b,r)	SET_N(r); SET_Z(r); SET_C_SUB(a,b)

uint32_t gpu_convert_zero[32] =
	{ 32,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31 };

uint8_t * branch_condition_table = 0;
#define BRANCH_CONDITION(x)	branch_condition_table[(x) + ((jaguar_flags & 7) << 5)]

uint32_t gpu_opcode_use[64];

const char * gpu_opcode_str[64]=
{
	"add",				"addc",				"addq",				"addqt",
	"sub",				"subc",				"subq",				"subqt",
	"neg",				"and",				"or",				"xor",
	"not",				"btst",				"bset",				"bclr",
	"mult",				"imult",			"imultn",			"resmac",
	"imacn",			"div",				"abs",				"sh",
	"shlq",				"shrq",				"sha",				"sharq",
	"ror",				"rorq",				"cmp",				"cmpq",
	"sat8",				"sat16",			"move",				"moveq",
	"moveta",			"movefa",			"movei",			"loadb",
	"loadw",			"load",				"loadp",			"load_r14_indexed",
	"load_r15_indexed",	"storeb",			"storew",			"store",
	"storep",			"store_r14_indexed","store_r15_indexed","move_pc",
	"jump",				"jr",				"mmult",			"mtoi",
	"normi",			"nop",				"load_r14_ri",		"load_r15_ri",
	"store_r14_ri",		"store_r15_ri",		"sat24",			"pack",
};

static uint32_t gpu_in_exec = 0;
static uint32_t gpu_releaseTimeSlice_flag = 0;

void GPUReleaseTimeslice(void)
{
	gpu_releaseTimeSlice_flag = 1;
}

uint32_t GPUGetPC(void)
{
	return gpu_pc;
}

void build_branch_condition_table(void)
{
	if (!branch_condition_table)
	{
		branch_condition_table = (uint8_t *)malloc(32 * 8 * sizeof(branch_condition_table[0]));

		if (branch_condition_table)
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
					branch_condition_table[i * 32 + j] = result;
				}
			}
		}
	}
}

//
// GPU byte access (read)
//
uint8_t GPUReadByte(uint32_t offset, uint32_t who)
{

	if ((offset >= GPU_WORK_RAM_BASE) && (offset < GPU_WORK_RAM_BASE+0x1000))
		return gpu_ram_8[offset & 0xFFF];
	else if ((offset >= GPU_CONTROL_RAM_BASE) && (offset < GPU_CONTROL_RAM_BASE+0x20))
	{
		uint32_t data = GPUReadLong(offset & 0xFFFFFFFC, who);

		if ((offset & 0x03) == 0)
			return data >> 24;
		else if ((offset & 0x03) == 1)
			return (data >> 16) & 0xFF;
		else if ((offset & 0x03) == 2)
			return (data >> 8) & 0xFF;
		else if ((offset & 0x03) == 3)
			return data & 0xFF;
	}

	return JaguarReadByte(offset, who);
}

//
// GPU word access (read)
//
uint16_t GPUReadWord(uint32_t offset, uint32_t who/*=UNKNOWN*/)
{
	if ((offset >= GPU_WORK_RAM_BASE) && (offset < GPU_WORK_RAM_BASE+0x1000))
	{
		offset &= 0xFFF;
		uint16_t data = ((uint16_t)gpu_ram_8[offset] << 8) | (uint16_t)gpu_ram_8[offset+1];
		return data;
	}
	else if ((offset >= GPU_CONTROL_RAM_BASE) && (offset < GPU_CONTROL_RAM_BASE+0x20))
	{
// This looks and smells wrong...
// But it *might* be OK...
		if (offset & 0x01)			// Catch cases 1 & 3... (unaligned read)
			return (GPUReadByte(offset, who) << 8) | GPUReadByte(offset+1, who);

		uint32_t data = GPUReadLong(offset & 0xFFFFFFFC, who);

		if (offset & 0x02)			// Cases 0 & 2...
			return data & 0xFFFF;
		else
			return data >> 16;
	}

//TEMP--Mirror of F03000? No. Writes only...
//if (offset >= 0xF0B000 && offset <= 0xF0BFFF)
////WriteLog("[GPUR16] --> Possible GPU RAM mirror access by %s!", whoName[who]);

	return JaguarReadWord(offset, who);
}

//
// GPU dword access (read)
//
uint32_t GPUReadLong(uint32_t offset, uint32_t who/*=UNKNOWN*/)
{
	if (offset >= 0xF02000 && offset <= 0xF020FF)
	{
		uint32_t reg = (offset & 0xFC) >> 2;
		return (reg < 32 ? gpu_reg_bank_0[reg] : gpu_reg_bank_1[reg - 32]); 
	}

//	if ((offset >= GPU_WORK_RAM_BASE) && (offset < GPU_WORK_RAM_BASE + 0x1000))
	if ((offset >= GPU_WORK_RAM_BASE) && (offset <= GPU_WORK_RAM_BASE + 0x0FFC))
	{
		offset &= 0xFFF;
		return ((uint32_t)gpu_ram_8[offset] << 24) | ((uint32_t)gpu_ram_8[offset+1] << 16)
			| ((uint32_t)gpu_ram_8[offset+2] << 8) | (uint32_t)gpu_ram_8[offset+3];//*/
//		return GET32(gpu_ram_8, offset);
	}
//	else if ((offset >= GPU_CONTROL_RAM_BASE) && (offset < GPU_CONTROL_RAM_BASE+0x20))
	else if ((offset >= GPU_CONTROL_RAM_BASE) && (offset <= GPU_CONTROL_RAM_BASE + 0x1C))
	{
		offset &= 0x1F;
		switch (offset)
		{
		case 0x00:
			gpu_flag_c = (gpu_flag_c ? 1 : 0);
			gpu_flag_z = (gpu_flag_z ? 1 : 0);
			gpu_flag_n = (gpu_flag_n ? 1 : 0);

			gpu_flags = (gpu_flags & 0xFFFFFFF8) | (gpu_flag_n << 2) | (gpu_flag_c << 1) | gpu_flag_z;

			return gpu_flags & 0xFFFFC1FF;
		case 0x04:
			return gpu_matrix_control;
		case 0x08:
			return gpu_pointer_to_matrix;
		case 0x0C:
			return gpu_data_organization;
		case 0x10:
			return gpu_pc;
		case 0x14:
			return gpu_control;
		case 0x18:
			return gpu_hidata;
		case 0x1C:
			return gpu_remain;
		default:								// unaligned long read
			return 0;
		}
	}
//TEMP--Mirror of F03000? No. Writes only...
//if (offset >= 0xF0B000 && offset <= 0xF0BFFF)
//	//WriteLog("[GPUR32] --> Possible GPU RAM mirror access by %s!\n", whoName[who]);
/*if (offset >= 0xF1D000 && offset <= 0xF1DFFF)
	//WriteLog("[GPUR32] --> Reading from Wavetable ROM!\n");//*/

	return (JaguarReadWord(offset, who) << 16) | JaguarReadWord(offset + 2, who);
}

//
// GPU byte access (write)
//
void GPUWriteByte(uint32_t offset, uint8_t data, uint32_t who/*=UNKNOWN*/)
{
	if ((offset >= GPU_WORK_RAM_BASE) && (offset <= GPU_WORK_RAM_BASE + 0x0FFF))
	{
		gpu_ram_8[offset & 0xFFF] = data;

//This is the same stupid worthless code that was in the DSP!!! AARRRGGGGHHHHH!!!!!!
/*		if (!gpu_in_exec)
		{
			m68k_end_timeslice();
			dsp_releaseTimeslice();
		}*/
		return;
	}
	else if ((offset >= GPU_CONTROL_RAM_BASE) && (offset <= GPU_CONTROL_RAM_BASE + 0x1F))
	{
		uint32_t reg = offset & 0x1C;
		int bytenum = offset & 0x03;

//This is definitely wrong!
		if ((reg >= 0x1C) && (reg <= 0x1F))
			gpu_div_control = (gpu_div_control & (~(0xFF << (bytenum << 3)))) | (data << (bytenum << 3));
		else
		{
			uint32_t old_data = GPUReadLong(offset & 0xFFFFFFC, who);
			bytenum = 3 - bytenum; // convention motorola !!!
			old_data = (old_data & (~(0xFF << (bytenum << 3)))) | (data << (bytenum << 3));
			GPUWriteLong(offset & 0xFFFFFFC, old_data, who);
		}
		return;
	}
//	//WriteLog("gpu: writing %.2x at 0x%.8x\n",data,offset);
	JaguarWriteByte(offset, data, who);
}

//
// GPU word access (write)
//
void GPUWriteWord(uint32_t offset, uint16_t data, uint32_t who/*=UNKNOWN*/)
{
	if ((offset >= GPU_WORK_RAM_BASE) && (offset <= GPU_WORK_RAM_BASE + 0x0FFE))
	{
		gpu_ram_8[offset & 0xFFF] = (data>>8) & 0xFF;
		gpu_ram_8[(offset+1) & 0xFFF] = data & 0xFF;//*/
/*		offset &= 0xFFF;
		SET16(gpu_ram_8, offset, data);//*/

/*if (offset >= 0xF03214 && offset < 0xF0321F)
	//WriteLog("GPU: Writing WORD (%04X) to GPU RAM (%08X)...\n", data, offset);//*/


//This is the same stupid worthless code that was in the DSP!!! AARRRGGGGHHHHH!!!!!!
/*		if (!gpu_in_exec)
		{
			m68k_end_timeslice();
			dsp_releaseTimeslice();
		}*/
		return;
	}
	else if ((offset >= GPU_CONTROL_RAM_BASE) && (offset <= GPU_CONTROL_RAM_BASE + 0x1E))
	{
		if (offset & 0x01)		// This is supposed to weed out unaligned writes, but does nothing...
		{
#ifdef GPU_DEBUG
			//WriteLog("GPU: Write16--unaligned write @ %08X [%04X]\n", offset, data);
			GPUDumpRegisters();
#endif	// GPU_DEBUG
			return;
		}
//Dual locations in this range: $1C Divide unit remainder/Divide unit control (R/W)
//This just literally sucks.
		if ((offset & 0x1C) == 0x1C)
		{
//This doesn't look right either--handles cases 1, 2, & 3 all the same!
			if (offset & 0x02)
				gpu_div_control = (gpu_div_control & 0xFFFF0000) | (data & 0xFFFF);
			else
				gpu_div_control = (gpu_div_control & 0x0000FFFF) | ((data & 0xFFFF) << 16);
		}
		else
		{
////WriteLog("[GPU W16:%08X,%04X]", offset, data);
			uint32_t old_data = GPUReadLong(offset & 0xFFFFFFC, who);

			if (offset & 0x02)
				old_data = (old_data & 0xFFFF0000) | (data & 0xFFFF);
			else
				old_data = (old_data & 0x0000FFFF) | ((data & 0xFFFF) << 16);

			GPUWriteLong(offset & 0xFFFFFFC, old_data, who);
		}

		return;
	}
	else if ((offset == GPU_WORK_RAM_BASE + 0x0FFF) || (GPU_CONTROL_RAM_BASE + 0x1F))
	{
		return;
	}

	// Have to be careful here--this can cause an infinite loop!
	JaguarWriteWord(offset, data, who);
}

//
// GPU dword access (write)
//
void GPUWriteLong(uint32_t offset, uint32_t data, uint32_t who/*=UNKNOWN*/)
{
//	if ((offset >= GPU_WORK_RAM_BASE) && (offset < GPU_WORK_RAM_BASE + 0x1000))
	if ((offset >= GPU_WORK_RAM_BASE) && (offset <= GPU_WORK_RAM_BASE + 0x0FFC))
	{
		offset &= 0xFFF;
		SET32(gpu_ram_8, offset, data);
		return;
	}
//	else if ((offset >= GPU_CONTROL_RAM_BASE) && (offset < GPU_CONTROL_RAM_BASE+0x20))
	else if ((offset >= GPU_CONTROL_RAM_BASE) && (offset <= GPU_CONTROL_RAM_BASE + 0x1C))
	{
		offset &= 0x1F;
		switch (offset)
		{
		case 0x00:
		{
			bool IMASKCleared = (gpu_flags & IMASK) && !(data & IMASK);
			// NOTE: According to the JTRM, writing a 1 to IMASK has no effect; only the
			//       IRQ logic can set it. So we mask it out here to prevent problems...
			gpu_flags = data & (~IMASK);
			gpu_flag_z = gpu_flags & ZERO_FLAG;
			gpu_flag_c = (gpu_flags & CARRY_FLAG) >> 1;
			gpu_flag_n = (gpu_flags & NEGA_FLAG) >> 2;
			GPUUpdateRegisterBanks();
			gpu_control &= ~((gpu_flags & CINT04FLAGS) >> 3);	// Interrupt latch clear bits
//Writing here is only an interrupt enable--this approach is just plain wrong!
//			GPUHandleIRQs();
//This, however, is A-OK! ;-)
			if (IMASKCleared)						// If IMASK was cleared,
				GPUHandleIRQs();					// see if any other interrupts need servicing!
			break;
		}
		case 0x04:
			gpu_matrix_control = data;
			break;
		case 0x08:
			// This can only point to long aligned addresses
			gpu_pointer_to_matrix = data & 0xFFFFFFFC;
			break;
		case 0x0C:
			gpu_data_organization = data;
			break;
		case 0x10:
			gpu_pc = data;
			break;
		case 0x14:
		{
//			uint32_t gpu_was_running = GPU_RUNNING;
			data &= ~0xF7C0;		// Disable writes to INT_LAT0-4 & TOM version number

			// check for GPU -> CPU interrupt
			if (data & 0x02)
			{
////WriteLog("GPU->CPU interrupt\n");
				if (tom_irq_enabled(IRQ_GPU))
				{
//This is the programmer's responsibility, to make sure the handler is valid, not ours!
//					if ((TOMIRQEnabled(IRQ_GPU))// && (JaguarInterruptHandlerIsValid(64)))
					{
						tom_set_pending_gpu_int();
						m68k_set_irq(2);			// Set 68000 IPL 2
						GPUReleaseTimeslice();
					}
				}
				data &= ~0x02;
			}

			// check for CPU -> GPU interrupt #0
			if (data & 0x04)
			{
////WriteLog("CPU->GPU interrupt\n");
				GPUSetIRQLine(0, ASSERT_LINE);
				m68k_end_timeslice();
				#ifdef DSP_EMU
				dsp_releaseTimeslice();
				#endif
				data &= ~0x04;
			}

			// single stepping
			/*if (data & 0x10)
			{
				//WriteLog("asked to perform a single step (single step is %senabled)\n",(data&0x8)?"":"not ");
			}*/

			gpu_control = (gpu_control & 0xF7C0) | (data & (~0xF7C0));

			// if gpu wasn't running but is now running, execute a few cycles
#ifndef GPU_SINGLE_STEPPING
/*			if (!gpu_was_running && GPU_RUNNING)
#ifdef GPU_DEBUG
			{
				//WriteLog("GPU: Write32--About to do stupid braindead GPU execution for 200 cycles.\n");
#endif	// GPU_DEBUG
				GPUExec(200);
#ifdef GPU_DEBUG
			}
#endif	// GPU_DEBUG//*/
#else
			if (gpu_control & 0x18)
				GPUExec(1);
#endif	// #ifndef GPU_SINGLE_STEPPING
//if (GPU_RUNNING)
//	GPUDumpDisassembly();
/*if (GPU_RUNNING)
{
	if (gpu_pc == 0xF035D8)
	{
//		GPUDumpDisassembly();
//		log_done();
//		exit(1);
		gpu_control &= 0xFFFFFFFE;	// Don't run it and let's see what happens!
//Hmm. Seems to lock up when going into the demo...
//Try to disable the collision altogether!
	}
}//*/
extern int effect_start5;
//if (GPU_RUNNING && effect_start5 && !finished)
/*
if (GPU_RUNNING && effect_start5 && gpu_pc == 0xF035D8)
{
}*/

			// (?) If we're set running by the M68K (or DSP?) then end its timeslice to
			// allow the GPU a chance to run...
			// Yes! This partially fixed Trevor McFur...
			if (GPU_RUNNING)
				m68k_end_timeslice();
			break;
		}
		case 0x18:
			gpu_hidata = data;
			break;
		case 0x1C:
			gpu_div_control = data;
			break;
//		default:   // unaligned long write
			//exit(0);
			//__asm int 3
		}
		return;
	}

//	JaguarWriteWord(offset, (data >> 16) & 0xFFFF, who);
//	JaguarWriteWord(offset+2, data & 0xFFFF, who);
// We're a 32-bit processor, we can do a long write...!
	JaguarWriteLong(offset, data, who);
}

//
// Change register banks if necessary
//
void GPUUpdateRegisterBanks(void)
{
	int bank = (gpu_flags & REGPAGE);		// REGPAGE bit

	if (gpu_flags & IMASK)					// IMASK bit
		bank = 0;							// IMASK forces main bank to be bank 0

	if (bank)
		gpu_reg = gpu_reg_bank_1, gpu_alternate_reg = gpu_reg_bank_0;
	else
		gpu_reg = gpu_reg_bank_0, gpu_alternate_reg = gpu_reg_bank_1;
}

void GPUHandleIRQs(void)
{
	// Bail out if we're already in an interrupt!
	if (gpu_flags & IMASK)
		return;

	// Get the interrupt latch & enable bits
	uint32_t bits = (gpu_control >> 6) & 0x1F, mask = (gpu_flags >> 4) & 0x1F;

	// Bail out if latched interrupts aren't enabled
	bits &= mask;
	if (!bits)
		return;

	// Determine which interrupt to service
	uint32_t which = 0; //Isn't there a #pragma to disable this warning???
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

	// set the interrupt flag
	gpu_flags |= IMASK;
	GPUUpdateRegisterBanks();

	// subqt  #4,r31		; pre-decrement stack pointer
	// move  pc,r30			; address of interrupted code
	// store  r30,(r31)     ; store return address
	gpu_reg[31] -= 4;
	GPUWriteLong(gpu_reg[31], gpu_pc - 2, GPU);

	// movei  #service_address,r30  ; pointer to ISR entry
	// jump  (r30)					; jump to ISR
	// nop
	gpu_pc = gpu_reg[30] = GPU_WORK_RAM_BASE + (which * 0x10);
}

void GPUSetIRQLine(int irqline, int state)
{
	uint32_t mask = 0x0040 << irqline;
	gpu_control &= ~mask;				// Clear the interrupt latch

	if (state)
	{
		gpu_control |= mask;			// Assert the interrupt latch
		GPUHandleIRQs();				// And handle the interrupt...
	}
}

//TEMPORARY: Testing only!
//#include "gpu2.h"
//#include "gpu3.h"

void GPUInit(void)
{
//	memory_malloc_secure((void **)&gpu_ram_8, 0x1000, "GPU work RAM");
//	memory_malloc_secure((void **)&gpu_reg_bank_0, 32 * sizeof(int32_t), "GPU bank 0 regs");
//	memory_malloc_secure((void **)&gpu_reg_bank_1, 32 * sizeof(int32_t), "GPU bank 1 regs");

	build_branch_condition_table();

	GPUReset();

//TEMPORARY: Testing only!
//	gpu2_init();
//	gpu3_init();
}

void GPUReset(void)
{
	// GPU registers (directly visible)
	gpu_flags			  = 0x00000000;
	gpu_matrix_control    = 0x00000000;
	gpu_pointer_to_matrix = 0x00000000;
	gpu_data_organization = 0xFFFFFFFF;
	gpu_pc				  = 0x00F03000;
	gpu_control			  = 0x00002800;			// Correctly sets this as TOM Rev. 2
	gpu_hidata			  = 0x00000000;
	gpu_remain			  = 0x00000000;			// These two registers are RO/WO
	gpu_div_control		  = 0x00000000;

	// GPU internal register
	gpu_acc				  = 0x00000000;

	gpu_reg = gpu_reg_bank_0;
	gpu_alternate_reg = gpu_reg_bank_1;

	for(int i=0; i<32; i++)
		gpu_reg[i] = gpu_alternate_reg[i] = 0x00000000;

	CLR_ZNC;
	memset(gpu_ram_8, 0xFF, 0x1000);
	gpu_in_exec = 0;
//not needed	GPUInterruptPending = false;
	GPUResetStats();

	// Contents of local RAM are quasi-stable; we simulate this by randomizing RAM contents
	for(uint32_t i=0; i<4096; i+=4)
		*((uint32_t *)(&gpu_ram_8[i])) = rand();
}

uint32_t GPUReadPC(void)
{
	return gpu_pc;
}

void GPUResetStats(void)
{
	for(uint32_t i=0; i<64; i++)
		gpu_opcode_use[i] = 0;
	//WriteLog("--> GPU stats were reset!\n");
}

void GPUDumpDisassembly(void)
{
}

void GPUDumpRegisters(void)
{

}

void GPUDumpMemory(void)
{

}

void GPUDone(void)
{
//	memory_free(gpu_ram_8);
//	memory_free(gpu_reg_bank_0);
//	memory_free(gpu_reg_bank_1);
}

//
// Main GPU execution core
//
void GPUExec(int32_t cycles)
{
	if (!GPU_RUNNING)
		return;

#ifdef GPU_SINGLE_STEPPING
	if (gpu_control & 0x18)
	{
		cycles = 1;
		gpu_control &= ~0x10;
	}
#endif
	GPUHandleIRQs();
	gpu_releaseTimeSlice_flag = 0;
	gpu_in_exec++;

	while (cycles > 0 && GPU_RUNNING)
	{
if (gpu_ram_8[0x054] == 0x98 && gpu_ram_8[0x055] == 0x0A && gpu_ram_8[0x056] == 0x03
	&& gpu_ram_8[0x057] == 0x00 && gpu_ram_8[0x058] == 0x00 && gpu_ram_8[0x059] == 0x00)
{
	if (gpu_pc == 0xF03000)
	{
		extern uint32_t starCount;
		starCount = 0;
	}
	{
	}
}
		uint16_t opcode = GPUReadWord(gpu_pc, GPU);
		uint32_t index = opcode >> 10;
		gpu_instruction = opcode;				// Added for GPU #3...
		gpu_opcode_first_parameter = (opcode >> 5) & 0x1F;
		gpu_opcode_second_parameter = opcode & 0x1F;
		
		gpu_pc += 2;
		gpu_opcode[index]();
		
		cycles -= gpu_opcode_cycles[index];
		gpu_opcode_use[index]++;
/*
if ((gpu_pc < 0xF03000 || gpu_pc > 0xF03FFF) && !tripwire)
{
	//WriteLog("GPU: Executing outside local RAM! GPU_PC: %08X\n", gpu_pc);
	tripwire = true;
}*/
	}

	gpu_in_exec--;
}

//
// GPU opcodes
//

/*
GPU opcodes use (offset punch--vertically below bad guy):
	              add 18686
	             addq 32621
	              sub 7483
	             subq 10252
	              and 21229
	               or 15003
	             btst 1822
	             bset 2072
	             mult 141
	              div 2392
	             shlq 13449
	             shrq 10297
	            sharq 11104
	              cmp 6775
	             cmpq 5944
	             move 31259
	            moveq 4473
	            movei 23277
	            loadb 46
	            loadw 4201
	             load 28580
	 load_r14_indexed 1183
	 load_r15_indexed 1125
	           storew 178
	            store 10144
	store_r14_indexed 320
	store_r15_indexed 1
	          move_pc 1742
	             jump 24467
	               jr 18090
	              nop 41362
*/


static void gpu_opcode_jump(void)
{
#ifdef GPU_DIS_JUMP
const char * condition[32] =
{	"T", "nz", "z", "???", "nc", "nc nz", "nc z", "???", "c", "c nz",
	"c z", "???", "???", "???", "???", "???", "???", "???", "???",
	"???", "nn", "nn nz", "nn z", "???", "n", "n nz", "n z", "???",
	"???", "???", "???", "F" };
	if (doGPUDis)
		//WriteLog("%06X: JUMP   %s, (R%02u) [NCZ:%u%u%u, R%02u=%08X] ", gpu_pc-2, condition[IMM_2], IMM_1, gpu_flag_n, gpu_flag_c, gpu_flag_z, IMM_1, RM);
#endif
	// normalize flags
/*	gpu_flag_c = (gpu_flag_c ? 1 : 0);
	gpu_flag_z = (gpu_flag_z ? 1 : 0);
	gpu_flag_n = (gpu_flag_n ? 1 : 0);*/
	// KLUDGE: Used by BRANCH_CONDITION
	uint32_t jaguar_flags = (gpu_flag_n << 2) | (gpu_flag_c << 1) | gpu_flag_z;

	if (BRANCH_CONDITION(IMM_2))
	{
#ifdef GPU_DIS_JUMP
	if (doGPUDis)
		//WriteLog("Branched!\n");
#endif
		uint32_t delayed_pc = RM;
		GPUExec(1);
		gpu_pc = delayed_pc;
/*		uint16_t opcode = GPUReadWord(gpu_pc, GPU);
		gpu_opcode_first_parameter = (opcode >> 5) & 0x1F;
		gpu_opcode_second_parameter = opcode & 0x1F;

		gpu_pc = delayed_pc;
		gpu_opcode[opcode>>10]();//*/
	}
#ifdef GPU_DIS_JUMP
	else
		if (doGPUDis)
			//WriteLog("Branch NOT taken.\n");
#endif
}


static void gpu_opcode_jr(void)
{
#ifdef GPU_DIS_JR
const char * condition[32] =
{	"T", "nz", "z", "???", "nc", "nc nz", "nc z", "???", "c", "c nz",
	"c z", "???", "???", "???", "???", "???", "???", "???", "???",
	"???", "nn", "nn nz", "nn z", "???", "n", "n nz", "n z", "???",
	"???", "???", "???", "F" };
	if (doGPUDis)
		//WriteLog("%06X: JR     %s, %06X [NCZ:%u%u%u] ", gpu_pc-2, condition[IMM_2], gpu_pc+((IMM_1 & 0x10 ? 0xFFFFFFF0 | IMM_1 : IMM_1) * 2), gpu_flag_n, gpu_flag_c, gpu_flag_z);
#endif
/*	if (CONDITION(jaguar.op & 31))
	{
		int32_t r1 = (INT8)((jaguar.op >> 2) & 0xF8) >> 2;
		uint32_t newpc = jaguar.PC + r1;
		CALL_MAME_DEBUG;
		jaguar.op = ROPCODE(jaguar.PC);
		jaguar.PC = newpc;
		(*jaguar.table[jaguar.op >> 10])();

		jaguar_icount -= 3;	// 3 wait states guaranteed
	}*/
	// normalize flags
/*	gpu_flag_n = (gpu_flag_n ? 1 : 0);
	gpu_flag_c = (gpu_flag_c ? 1 : 0);
	gpu_flag_z = (gpu_flag_z ? 1 : 0);*/
	// KLUDGE: Used by BRANCH_CONDITION
	uint32_t jaguar_flags = (gpu_flag_n << 2) | (gpu_flag_c << 1) | gpu_flag_z;

	if (BRANCH_CONDITION(IMM_2))
	{
#ifdef GPU_DIS_JR
	if (doGPUDis)
		//WriteLog("Branched!\n");
#endif
		int32_t offset = (IMM_1 & 0x10 ? 0xFFFFFFF0 | IMM_1 : IMM_1);		// Sign extend IMM_1
		int32_t delayed_pc = gpu_pc + (offset * 2);
		GPUExec(1);
		gpu_pc = delayed_pc;
/*		uint16_t opcode = GPUReadWord(gpu_pc, GPU);
		gpu_opcode_first_parameter = (opcode >> 5) & 0x1F;
		gpu_opcode_second_parameter = opcode & 0x1F;

		gpu_pc = delayed_pc;
		gpu_opcode[opcode>>10]();//*/
	}
#ifdef GPU_DIS_JR
	else
		if (doGPUDis)
			//WriteLog("Branch NOT taken.\n");
#endif
}


static void gpu_opcode_add(void)
{
#ifdef GPU_DIS_ADD
	if (doGPUDis)
		//WriteLog("%06X: ADD    R%02u, R%02u [NCZ:%u%u%u, R%02u=%08X, R%02u=%08X] -> ", gpu_pc-2, IMM_1, IMM_2, gpu_flag_n, gpu_flag_c, gpu_flag_z, IMM_1, RM, IMM_2, RN);
#endif
	uint32_t res = RN + RM;
	CLR_ZNC; SET_ZNC_ADD(RN, RM, res);
	RN = res;
#ifdef GPU_DIS_ADD
	if (doGPUDis)
		//WriteLog("[NCZ:%u%u%u, R%02u=%08X, R%02u=%08X]\n", gpu_flag_n, gpu_flag_c, gpu_flag_z, IMM_1, RM, IMM_2, RN);
#endif
}


static void gpu_opcode_addc(void)
{
#ifdef GPU_DIS_ADDC
	if (doGPUDis)
		//WriteLog("%06X: ADDC   R%02u, R%02u [NCZ:%u%u%u, R%02u=%08X, R%02u=%08X] -> ", gpu_pc-2, IMM_1, IMM_2, gpu_flag_n, gpu_flag_c, gpu_flag_z, IMM_1, RM, IMM_2, RN);
#endif
/*	int dreg = jaguar.op & 31;
	uint32_t r1 = jaguar.r[(jaguar.op >> 5) & 31];
	uint32_t r2 = jaguar.r[dreg];
	uint32_t res = r2 + r1 + ((jaguar.FLAGS >> 1) & 1);
	jaguar.r[dreg] = res;
	CLR_ZNC; SET_ZNC_ADD(r2,r1,res);*/

	uint32_t res = RN + RM + gpu_flag_c;
	uint32_t carry = gpu_flag_c;
//	SET_ZNC_ADD(RN, RM, res); //???BUG??? Yes!
	SET_ZNC_ADD(RN + carry, RM, res);
//	SET_ZNC_ADD(RN, RM + carry, res);
	RN = res;
#ifdef GPU_DIS_ADDC
	if (doGPUDis)
		//WriteLog("[NCZ:%u%u%u, R%02u=%08X, R%02u=%08X]\n", gpu_flag_n, gpu_flag_c, gpu_flag_z, IMM_1, RM, IMM_2, RN);
#endif
}


static void gpu_opcode_addq(void)
{
#ifdef GPU_DIS_ADDQ
	if (doGPUDis)
		//WriteLog("%06X: ADDQ   #%u, R%02u [NCZ:%u%u%u, R%02u=%08X] -> ", gpu_pc-2, gpu_convert_zero[IMM_1], IMM_2, gpu_flag_n, gpu_flag_c, gpu_flag_z, IMM_2, RN);
#endif
	uint32_t r1 = gpu_convert_zero[IMM_1];
	uint32_t res = RN + r1;
	CLR_ZNC; SET_ZNC_ADD(RN, r1, res);
	RN = res;
#ifdef GPU_DIS_ADDQ
	if (doGPUDis)
		//WriteLog("[NCZ:%u%u%u, R%02u=%08X]\n", gpu_flag_n, gpu_flag_c, gpu_flag_z, IMM_2, RN);
#endif
}


static void gpu_opcode_addqt(void)
{
#ifdef GPU_DIS_ADDQT
	if (doGPUDis)
		//WriteLog("%06X: ADDQT  #%u, R%02u [NCZ:%u%u%u, R%02u=%08X] -> ", gpu_pc-2, gpu_convert_zero[IMM_1], IMM_2, gpu_flag_n, gpu_flag_c, gpu_flag_z, IMM_2, RN);
#endif
	RN += gpu_convert_zero[IMM_1];
#ifdef GPU_DIS_ADDQT
	if (doGPUDis)
		//WriteLog("[NCZ:%u%u%u, R%02u=%08X]\n", gpu_flag_n, gpu_flag_c, gpu_flag_z, IMM_2, RN);
#endif
}


static void gpu_opcode_sub(void)
{
#ifdef GPU_DIS_SUB
	if (doGPUDis)
		//WriteLog("%06X: SUB    R%02u, R%02u [NCZ:%u%u%u, R%02u=%08X, R%02u=%08X] -> ", gpu_pc-2, IMM_1, IMM_2, gpu_flag_n, gpu_flag_c, gpu_flag_z, IMM_1, RM, IMM_2, RN);
#endif
	uint32_t res = RN - RM;
	SET_ZNC_SUB(RN, RM, res);
	RN = res;
#ifdef GPU_DIS_SUB
	if (doGPUDis)
		//WriteLog("[NCZ:%u%u%u, R%02u=%08X, R%02u=%08X]\n", gpu_flag_n, gpu_flag_c, gpu_flag_z, IMM_1, RM, IMM_2, RN);
#endif
}


static void gpu_opcode_subc(void)
{
#ifdef GPU_DIS_SUBC
	if (doGPUDis)
		//WriteLog("%06X: SUBC   R%02u, R%02u [NCZ:%u%u%u, R%02u=%08X, R%02u=%08X] -> ", gpu_pc-2, IMM_1, IMM_2, gpu_flag_n, gpu_flag_c, gpu_flag_z, IMM_1, RM, IMM_2, RN);
#endif
	// This is how the GPU ALU does it--Two's complement with inverted carry
	uint64_t res = (uint64_t)RN + (uint64_t)(RM ^ 0xFFFFFFFF) + (gpu_flag_c ^ 1);
	// Carry out of the result is inverted too
	gpu_flag_c = ((res >> 32) & 0x01) ^ 1;
	RN = (res & 0xFFFFFFFF);
	SET_ZN(RN);
#ifdef GPU_DIS_SUBC
	if (doGPUDis)
		//WriteLog("[NCZ:%u%u%u, R%02u=%08X, R%02u=%08X]\n", gpu_flag_n, gpu_flag_c, gpu_flag_z, IMM_1, RM, IMM_2, RN);
#endif
}


static void gpu_opcode_subq(void)
{
#ifdef GPU_DIS_SUBQ
	if (doGPUDis)
		//WriteLog("%06X: SUBQ   #%u, R%02u [NCZ:%u%u%u, R%02u=%08X] -> ", gpu_pc-2, gpu_convert_zero[IMM_1], IMM_2, gpu_flag_n, gpu_flag_c, gpu_flag_z, IMM_2, RN);
#endif
	uint32_t r1 = gpu_convert_zero[IMM_1];
	uint32_t res = RN - r1;
	SET_ZNC_SUB(RN, r1, res);
	RN = res;
#ifdef GPU_DIS_SUBQ
	if (doGPUDis)
		//WriteLog("[NCZ:%u%u%u, R%02u=%08X]\n", gpu_flag_n, gpu_flag_c, gpu_flag_z, IMM_2, RN);
#endif
}


static void gpu_opcode_subqt(void)
{
#ifdef GPU_DIS_SUBQT
	if (doGPUDis)
		//WriteLog("%06X: SUBQT  #%u, R%02u [NCZ:%u%u%u, R%02u=%08X] -> ", gpu_pc-2, gpu_convert_zero[IMM_1], IMM_2, gpu_flag_n, gpu_flag_c, gpu_flag_z, IMM_2, RN);
#endif
	RN -= gpu_convert_zero[IMM_1];
#ifdef GPU_DIS_SUBQT
	if (doGPUDis)
		//WriteLog("[NCZ:%u%u%u, R%02u=%08X]\n", gpu_flag_n, gpu_flag_c, gpu_flag_z, IMM_2, RN);
#endif
}


static void gpu_opcode_cmp(void)
{
#ifdef GPU_DIS_CMP
	if (doGPUDis)
		//WriteLog("%06X: CMP    R%02u, R%02u [NCZ:%u%u%u, R%02u=%08X, R%02u=%08X] -> ", gpu_pc-2, IMM_1, IMM_2, gpu_flag_n, gpu_flag_c, gpu_flag_z, IMM_1, RM, IMM_2, RN);
#endif
	uint32_t res = RN - RM;
	SET_ZNC_SUB(RN, RM, res);
#ifdef GPU_DIS_CMP
	if (doGPUDis)
		//WriteLog("[NCZ:%u%u%u]\n", gpu_flag_n, gpu_flag_c, gpu_flag_z);
#endif
}


static void gpu_opcode_cmpq(void)
{
	static int32_t sqtable[32] =
		{ 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,-16,-15,-14,-13,-12,-11,-10,-9,-8,-7,-6,-5,-4,-3,-2,-1 };
#ifdef GPU_DIS_CMPQ
	if (doGPUDis)
		//WriteLog("%06X: CMPQ   #%d, R%02u [NCZ:%u%u%u, R%02u=%08X] -> ", gpu_pc-2, sqtable[IMM_1], IMM_2, gpu_flag_n, gpu_flag_c, gpu_flag_z, IMM_2, RN);
#endif
	uint32_t r1 = sqtable[IMM_1 & 0x1F]; // I like this better -> (INT8)(jaguar.op >> 2) >> 3;
	uint32_t res = RN - r1;
	SET_ZNC_SUB(RN, r1, res);
#ifdef GPU_DIS_CMPQ
	if (doGPUDis)
		//WriteLog("[NCZ:%u%u%u]\n", gpu_flag_n, gpu_flag_c, gpu_flag_z);
#endif
}


static void gpu_opcode_and(void)
{
#ifdef GPU_DIS_AND
	if (doGPUDis)
		//WriteLog("%06X: AND    R%02u, R%02u [NCZ:%u%u%u, R%02u=%08X, R%02u=%08X] -> ", gpu_pc-2, IMM_1, IMM_2, gpu_flag_n, gpu_flag_c, gpu_flag_z, IMM_1, RM, IMM_2, RN);
#endif
	RN = RN & RM;
	SET_ZN(RN);
#ifdef GPU_DIS_AND
	if (doGPUDis)
		//WriteLog("[NCZ:%u%u%u, R%02u=%08X, R%02u=%08X]\n", gpu_flag_n, gpu_flag_c, gpu_flag_z, IMM_1, RM, IMM_2, RN);
#endif
}


static void gpu_opcode_or(void)
{
#ifdef GPU_DIS_OR
	if (doGPUDis)
		//WriteLog("%06X: OR     R%02u, R%02u [NCZ:%u%u%u, R%02u=%08X, R%02u=%08X] -> ", gpu_pc-2, IMM_1, IMM_2, gpu_flag_n, gpu_flag_c, gpu_flag_z, IMM_1, RM, IMM_2, RN);
#endif
	RN = RN | RM;
	SET_ZN(RN);
#ifdef GPU_DIS_OR
	if (doGPUDis)
		//WriteLog("[NCZ:%u%u%u, R%02u=%08X, R%02u=%08X]\n", gpu_flag_n, gpu_flag_c, gpu_flag_z, IMM_1, RM, IMM_2, RN);
#endif
}


static void gpu_opcode_xor(void)
{
#ifdef GPU_DIS_XOR
	if (doGPUDis)
		//WriteLog("%06X: XOR    R%02u, R%02u [NCZ:%u%u%u, R%02u=%08X, R%02u=%08X] -> ", gpu_pc-2, IMM_1, IMM_2, gpu_flag_n, gpu_flag_c, gpu_flag_z, IMM_1, RM, IMM_2, RN);
#endif
	RN = RN ^ RM;
	SET_ZN(RN);
#ifdef GPU_DIS_XOR
	if (doGPUDis)
		//WriteLog("[NCZ:%u%u%u, R%02u=%08X, R%02u=%08X]\n", gpu_flag_n, gpu_flag_c, gpu_flag_z, IMM_1, RM, IMM_2, RN);
#endif
}


static void gpu_opcode_not(void)
{
#ifdef GPU_DIS_NOT
	if (doGPUDis)
		//WriteLog("%06X: NOT    R%02u, R%02u [NCZ:%u%u%u, R%02u=%08X, R%02u=%08X] -> ", gpu_pc-2, IMM_1, IMM_2, gpu_flag_n, gpu_flag_c, gpu_flag_z, IMM_1, RM, IMM_2, RN);
#endif
	RN = ~RN;
	SET_ZN(RN);
#ifdef GPU_DIS_NOT
	if (doGPUDis)
		//WriteLog("[NCZ:%u%u%u, R%02u=%08X, R%02u=%08X]\n", gpu_flag_n, gpu_flag_c, gpu_flag_z, IMM_1, RM, IMM_2, RN);
#endif
}


static void gpu_opcode_move_pc(void)
{
#ifdef GPU_DIS_MOVEPC
	if (doGPUDis)
		//WriteLog("%06X: MOVE   PC, R%02u [NCZ:%u%u%u, PC=%08X, R%02u=%08X] -> ", gpu_pc-2, IMM_2, gpu_flag_n, gpu_flag_c, gpu_flag_z, gpu_pc-2, IMM_2, RN);
#endif
	// Should be previous PC--this might not always be previous instruction!
	// Then again, this will point right at the *current* instruction, i.e., MOVE PC,R!
	RN = gpu_pc - 2;
#ifdef GPU_DIS_MOVEPC
	if (doGPUDis)
		//WriteLog("[NCZ:%u%u%u, R%02u=%08X]\n", gpu_flag_n, gpu_flag_c, gpu_flag_z, IMM_2, RN);
#endif
}


static void gpu_opcode_sat8(void)
{
#ifdef GPU_DIS_SAT8
	if (doGPUDis)
		//WriteLog("%06X: SAT8   R%02u [NCZ:%u%u%u, R%02u=%08X] -> ", gpu_pc-2, IMM_2, gpu_flag_n, gpu_flag_c, gpu_flag_z, IMM_2, RN);
#endif
	RN = ((int32_t)RN < 0 ? 0 : (RN > 0xFF ? 0xFF : RN));
	SET_ZN(RN);
#ifdef GPU_DIS_SAT8
	if (doGPUDis)
		//WriteLog("[NCZ:%u%u%u, R%02u=%08X]\n", gpu_flag_n, gpu_flag_c, gpu_flag_z, IMM_2, RN);
#endif
}


static void gpu_opcode_sat16(void)
{
	RN = ((int32_t)RN < 0 ? 0 : (RN > 0xFFFF ? 0xFFFF : RN));
	SET_ZN(RN);
}

static void gpu_opcode_sat24(void)
{
	RN = ((int32_t)RN < 0 ? 0 : (RN > 0xFFFFFF ? 0xFFFFFF : RN));
	SET_ZN(RN);
}


static void gpu_opcode_store_r14_indexed(void)
{
#ifdef GPU_DIS_STORE14I
	if (doGPUDis)
		//WriteLog("%06X: STORE  R%02u, (R14+$%02X) [NCZ:%u%u%u, R%02u=%08X, R14+$%02X=%08X]\n", gpu_pc-2, IMM_2, gpu_convert_zero[IMM_1] << 2, gpu_flag_n, gpu_flag_c, gpu_flag_z, IMM_2, RN, gpu_convert_zero[IMM_1] << 2, gpu_reg[14]+(gpu_convert_zero[IMM_1] << 2));
#endif
#ifdef GPU_CORRECT_ALIGNMENT
	uint32_t address = gpu_reg[14] + (gpu_convert_zero[IMM_1] << 2);
	
	if (address >= 0xF03000 && address <= 0xF03FFF)
		GPUWriteLong(address & 0xFFFFFFFC, RN, GPU);
	else
		GPUWriteLong(address, RN, GPU);
#else
	GPUWriteLong(gpu_reg[14] + (gpu_convert_zero[IMM_1] << 2), RN, GPU);
#endif
}


static void gpu_opcode_store_r15_indexed(void)
{
#ifdef GPU_DIS_STORE15I
	if (doGPUDis)
		//WriteLog("%06X: STORE  R%02u, (R15+$%02X) [NCZ:%u%u%u, R%02u=%08X, R15+$%02X=%08X]\n", gpu_pc-2, IMM_2, gpu_convert_zero[IMM_1] << 2, gpu_flag_n, gpu_flag_c, gpu_flag_z, IMM_2, RN, gpu_convert_zero[IMM_1] << 2, gpu_reg[15]+(gpu_convert_zero[IMM_1] << 2));
#endif
#ifdef GPU_CORRECT_ALIGNMENT
	uint32_t address = gpu_reg[15] + (gpu_convert_zero[IMM_1] << 2);

	if (address >= 0xF03000 && address <= 0xF03FFF)
		GPUWriteLong(address & 0xFFFFFFFC, RN, GPU);
	else
		GPUWriteLong(address, RN, GPU);
#else
	GPUWriteLong(gpu_reg[15] + (gpu_convert_zero[IMM_1] << 2), RN, GPU);
#endif
}


static void gpu_opcode_load_r14_ri(void)
{
#ifdef GPU_DIS_LOAD14R
	if (doGPUDis)
		//WriteLog("%06X: LOAD   (R14+R%02u), R%02u [NCZ:%u%u%u, R14+R%02u=%08X, R%02u=%08X] -> ", gpu_pc-2, IMM_1, IMM_2, gpu_flag_n, gpu_flag_c, gpu_flag_z, IMM_1, RM+gpu_reg[14], IMM_2, RN);
#endif
#ifdef GPU_CORRECT_ALIGNMENT
	uint32_t address = gpu_reg[14] + RM;

	if (address >= 0xF03000 && address <= 0xF03FFF)
		RN = GPUReadLong(address & 0xFFFFFFFC, GPU);
	else
		RN = GPUReadLong(address, GPU);
#else
	RN = GPUReadLong(gpu_reg[14] + RM, GPU);
#endif
#ifdef GPU_DIS_LOAD14R
	if (doGPUDis)
		//WriteLog("[NCZ:%u%u%u, R%02u=%08X]\n", gpu_flag_n, gpu_flag_c, gpu_flag_z, IMM_2, RN);
#endif
}


static void gpu_opcode_load_r15_ri(void)
{
#ifdef GPU_DIS_LOAD15R
	if (doGPUDis)
		//WriteLog("%06X: LOAD   (R15+R%02u), R%02u [NCZ:%u%u%u, R15+R%02u=%08X, R%02u=%08X] -> ", gpu_pc-2, IMM_1, IMM_2, gpu_flag_n, gpu_flag_c, gpu_flag_z, IMM_1, RM+gpu_reg[15], IMM_2, RN);
#endif
#ifdef GPU_CORRECT_ALIGNMENT
	uint32_t address = gpu_reg[15] + RM;

	if (address >= 0xF03000 && address <= 0xF03FFF)
		RN = GPUReadLong(address & 0xFFFFFFFC, GPU);
	else
		RN = GPUReadLong(address, GPU);
#else
	RN = GPUReadLong(gpu_reg[15] + RM, GPU);
#endif
#ifdef GPU_DIS_LOAD15R
	if (doGPUDis)
		//WriteLog("[NCZ:%u%u%u, R%02u=%08X]\n", gpu_flag_n, gpu_flag_c, gpu_flag_z, IMM_2, RN);
#endif
}


static void gpu_opcode_store_r14_ri(void)
{
#ifdef GPU_DIS_STORE14R
	if (doGPUDis)
		//WriteLog("%06X: STORE  R%02u, (R14+R%02u) [NCZ:%u%u%u, R%02u=%08X, R14+R%02u=%08X]\n", gpu_pc-2, IMM_2, IMM_1, gpu_flag_n, gpu_flag_c, gpu_flag_z, IMM_2, RN, IMM_1, RM+gpu_reg[14]);
#endif
#ifdef GPU_CORRECT_ALIGNMENT
	uint32_t address = gpu_reg[14] + RM;

	if (address >= 0xF03000 && address <= 0xF03FFF)
		GPUWriteLong(address & 0xFFFFFFFC, RN, GPU);
	else
		GPUWriteLong(address, RN, GPU);
#else
	GPUWriteLong(gpu_reg[14] + RM, RN, GPU);
#endif
}


static void gpu_opcode_store_r15_ri(void)
{
#ifdef GPU_DIS_STORE15R
	if (doGPUDis)
		//WriteLog("%06X: STORE  R%02u, (R15+R%02u) [NCZ:%u%u%u, R%02u=%08X, R15+R%02u=%08X]\n", gpu_pc-2, IMM_2, IMM_1, gpu_flag_n, gpu_flag_c, gpu_flag_z, IMM_2, RN, IMM_1, RM+gpu_reg[15]);
#endif
#ifdef GPU_CORRECT_ALIGNMENT_STORE
	uint32_t address = gpu_reg[15] + RM;

	if (address >= 0xF03000 && address <= 0xF03FFF)
		GPUWriteLong(address & 0xFFFFFFFC, RN, GPU);
	else
		GPUWriteLong(address, RN, GPU);
#else
	GPUWriteLong(gpu_reg[15] + RM, RN, GPU);
#endif
}


static void gpu_opcode_nop(void)
{
#ifdef GPU_DIS_NOP
	if (doGPUDis)
		//WriteLog("%06X: NOP    [NCZ:%u%u%u]\n", gpu_pc-2, gpu_flag_n, gpu_flag_c, gpu_flag_z);
#endif
}


static void gpu_opcode_pack(void)
{
#ifdef GPU_DIS_PACK
	if (doGPUDis)
		//WriteLog("%06X: %s R%02u [NCZ:%u%u%u, R%02u=%08X, R%02u=%08X] -> ", gpu_pc-2, (!IMM_1 ? "PACK  " : "UNPACK"), IMM_2, gpu_flag_n, gpu_flag_c, gpu_flag_z, IMM_2, RN);
#endif
	uint32_t val = RN;

//BUG!	if (RM == 0)				// Pack
	if (IMM_1 == 0)				// Pack
		RN = ((val >> 10) & 0x0000F000) | ((val >> 5) & 0x00000F00) | (val & 0x000000FF);
	else						// Unpack
		RN = ((val & 0x0000F000) << 10) | ((val & 0x00000F00) << 5) | (val & 0x000000FF);
#ifdef GPU_DIS_PACK
	if (doGPUDis)
		//WriteLog("[NCZ:%u%u%u, R%02u=%08X]\n", gpu_flag_n, gpu_flag_c, gpu_flag_z, IMM_2, RN);
#endif
}


static void gpu_opcode_storeb(void)
{
#ifdef GPU_DIS_STOREB
	if (doGPUDis)
		//WriteLog("%06X: STOREB R%02u, (R%02u) [NCZ:%u%u%u, R%02u=%08X, R%02u=%08X]\n", gpu_pc-2, IMM_2, IMM_1, gpu_flag_n, gpu_flag_c, gpu_flag_z, IMM_2, RN, IMM_1, RM);
#endif
//Is this right???
// Would appear to be so...!
	if ((RM >= 0xF03000) && (RM <= 0xF03FFF))
		GPUWriteLong(RM, RN & 0xFF, GPU);
	else
		JaguarWriteByte(RM, RN, GPU);
}


static void gpu_opcode_storew(void)
{
#ifdef GPU_DIS_STOREW
	if (doGPUDis)
		//WriteLog("%06X: STOREW R%02u, (R%02u) [NCZ:%u%u%u, R%02u=%08X, R%02u=%08X]\n", gpu_pc-2, IMM_2, IMM_1, gpu_flag_n, gpu_flag_c, gpu_flag_z, IMM_2, RN, IMM_1, RM);
#endif
#ifdef GPU_CORRECT_ALIGNMENT
	if ((RM >= 0xF03000) && (RM <= 0xF03FFF))
		GPUWriteLong(RM & 0xFFFFFFFE, RN & 0xFFFF, GPU);
	else
		JaguarWriteWord(RM, RN, GPU);
#else
	if ((RM >= 0xF03000) && (RM <= 0xF03FFF))
		GPUWriteLong(RM, RN & 0xFFFF, GPU);
	else
		JaguarWriteWord(RM, RN, GPU);
#endif
}


static void gpu_opcode_store(void)
{
#ifdef GPU_DIS_STORE
	if (doGPUDis)
		//WriteLog("%06X: STORE  R%02u, (R%02u) [NCZ:%u%u%u, R%02u=%08X, R%02u=%08X]\n", gpu_pc-2, IMM_2, IMM_1, gpu_flag_n, gpu_flag_c, gpu_flag_z, IMM_2, RN, IMM_1, RM);
#endif
#ifdef GPU_CORRECT_ALIGNMENT
	if ((RM >= 0xF03000) && (RM <= 0xF03FFF))
		GPUWriteLong(RM & 0xFFFFFFFC, RN, GPU);
	else
		GPUWriteLong(RM, RN, GPU);
#else
	GPUWriteLong(RM, RN, GPU);
#endif
}


static void gpu_opcode_storep(void)
{
#ifdef GPU_CORRECT_ALIGNMENT
	if ((RM >= 0xF03000) && (RM <= 0xF03FFF))
	{
		GPUWriteLong((RM & 0xFFFFFFF8) + 0, gpu_hidata, GPU);
		GPUWriteLong((RM & 0xFFFFFFF8) + 4, RN, GPU);
	}
	else
	{
		GPUWriteLong(RM + 0, gpu_hidata, GPU);
		GPUWriteLong(RM + 4, RN, GPU);
	}
#else
	GPUWriteLong(RM + 0, gpu_hidata, GPU);
	GPUWriteLong(RM + 4, RN, GPU);
#endif
}

static void gpu_opcode_loadb(void)
{
#ifdef GPU_DIS_LOADB
	if (doGPUDis)
		//WriteLog("%06X: LOADB  (R%02u), R%02u [NCZ:%u%u%u, R%02u=%08X, R%02u=%08X] -> ", gpu_pc-2, IMM_1, IMM_2, gpu_flag_n, gpu_flag_c, gpu_flag_z, IMM_1, RM, IMM_2, RN);
#endif
	if ((RM >= 0xF03000) && (RM <= 0xF03FFF))
		RN = GPUReadLong(RM, GPU) & 0xFF;
	else
		RN = JaguarReadByte(RM, GPU);
#ifdef GPU_DIS_LOADB
	if (doGPUDis)
		//WriteLog("[NCZ:%u%u%u, R%02u=%08X]\n", gpu_flag_n, gpu_flag_c, gpu_flag_z, IMM_2, RN);
#endif
}


static void gpu_opcode_loadw(void)
{
#ifdef GPU_DIS_LOADW
	if (doGPUDis)
		//WriteLog("%06X: LOADW  (R%02u), R%02u [NCZ:%u%u%u, R%02u=%08X, R%02u=%08X] -> ", gpu_pc-2, IMM_1, IMM_2, gpu_flag_n, gpu_flag_c, gpu_flag_z, IMM_1, RM, IMM_2, RN);
#endif
#ifdef GPU_CORRECT_ALIGNMENT
	if ((RM >= 0xF03000) && (RM <= 0xF03FFF))
		RN = GPUReadLong(RM & 0xFFFFFFFE, GPU) & 0xFFFF;
	else
		RN = JaguarReadWord(RM, GPU);
#else
	if ((RM >= 0xF03000) && (RM <= 0xF03FFF))
		RN = GPUReadLong(RM, GPU) & 0xFFFF;
	else
		RN = JaguarReadWord(RM, GPU);
#endif
#ifdef GPU_DIS_LOADW
	if (doGPUDis)
		//WriteLog("[NCZ:%u%u%u, R%02u=%08X]\n", gpu_flag_n, gpu_flag_c, gpu_flag_z, IMM_2, RN);
#endif
}


// According to the docs, & "Do The Same", this address is long aligned...
// So let's try it:
// And it works!!! Need to fix all instances...
// Also, Power Drive Rally seems to contradict the idea that only LOADs in
// the $F03000-$F03FFF range are aligned...
#warning "!!! Alignment issues, need to find definitive final word on this !!!"
/*
Preliminary testing on real hardware seems to confirm that something strange goes on
with unaligned reads in main memory. When the address is off by 1, the result is the
same as the long address with the top byte replaced by something. So if the read is
from $401, and $400 has 12 34 56 78, the value read will be $nn345678, where nn is a currently unknown vlaue.
When the address is off by 2, the result would be $nnnn5678, where nnnn is unknown.
When the address is off by 3, the result would be $nnnnnn78, where nnnnnn is unknown.
It may be that the "unknown" values come from the prefetch queue, but not sure how
to test that. They seem to be stable, though, which would indicate such a mechanism.
Sometimes, however, the off by 2 case returns $12345678!
*/
static void gpu_opcode_load(void)
{
#ifdef GPU_DIS_LOAD
	if (doGPUDis)
		//WriteLog("%06X: LOAD   (R%02u), R%02u [NCZ:%u%u%u, R%02u=%08X, R%02u=%08X] -> ", gpu_pc-2, IMM_1, IMM_2, gpu_flag_n, gpu_flag_c, gpu_flag_z, IMM_1, RM, IMM_2, RN);
#endif
#ifdef GPU_CORRECT_ALIGNMENT
	//uint32_t mask[4] = { 0x00000000, 0xFF000000, 0xFFFF0000, 0xFFFFFF00 };
//	if ((RM >= 0xF03000) && (RM <= 0xF03FFF))
		RN = GPUReadLong(RM & 0xFFFFFFFC, GPU);
//		RN = GPUReadLong(RM & 0x00FFFFFC, GPU);
//	else
//		RN = GPUReadLong(RM, GPU);
	// Simulate garbage in unaligned reads...
//seems that this behavior is different in GPU mem vs. main mem...
//	if ((RM < 0xF03000) || (RM > 0xF0BFFF))
//		RN |= mask[RM & 0x03];
#else
	RN = GPUReadLong(RM, GPU);
#endif
#ifdef GPU_DIS_LOAD
	if (doGPUDis)
		//WriteLog("[NCZ:%u%u%u, R%02u=%08X]\n", gpu_flag_n, gpu_flag_c, gpu_flag_z, IMM_2, RN);
#endif
}


static void gpu_opcode_loadp(void)
{
#ifdef GPU_CORRECT_ALIGNMENT
	if ((RM >= 0xF03000) && (RM <= 0xF03FFF))
	{
		gpu_hidata = GPUReadLong((RM & 0xFFFFFFF8) + 0, GPU);
		RN		   = GPUReadLong((RM & 0xFFFFFFF8) + 4, GPU);
	}
	else
	{
		gpu_hidata = GPUReadLong(RM + 0, GPU);
		RN		   = GPUReadLong(RM + 4, GPU);
	}
#else
	gpu_hidata = GPUReadLong(RM + 0, GPU);
	RN		   = GPUReadLong(RM + 4, GPU);
#endif
}


static void gpu_opcode_load_r14_indexed(void)
{
#ifdef GPU_DIS_LOAD14I
	if (doGPUDis)
		//WriteLog("%06X: LOAD   (R14+$%02X), R%02u [NCZ:%u%u%u, R14+$%02X=%08X, R%02u=%08X] -> ", gpu_pc-2, gpu_convert_zero[IMM_1] << 2, IMM_2, gpu_flag_n, gpu_flag_c, gpu_flag_z, gpu_convert_zero[IMM_1] << 2, gpu_reg[14]+(gpu_convert_zero[IMM_1] << 2), IMM_2, RN);
#endif
#ifdef GPU_CORRECT_ALIGNMENT
	uint32_t address = gpu_reg[14] + (gpu_convert_zero[IMM_1] << 2);

	if ((RM >= 0xF03000) && (RM <= 0xF03FFF))
		RN = GPUReadLong(address & 0xFFFFFFFC, GPU);
	else
		RN = GPUReadLong(address, GPU);
#else
	RN = GPUReadLong(gpu_reg[14] + (gpu_convert_zero[IMM_1] << 2), GPU);
#endif
#ifdef GPU_DIS_LOAD14I
	if (doGPUDis)
		//WriteLog("[NCZ:%u%u%u, R%02u=%08X]\n", gpu_flag_n, gpu_flag_c, gpu_flag_z, IMM_2, RN);
#endif
}


static void gpu_opcode_load_r15_indexed(void)
{
#ifdef GPU_DIS_LOAD15I
	if (doGPUDis)
		//WriteLog("%06X: LOAD   (R15+$%02X), R%02u [NCZ:%u%u%u, R15+$%02X=%08X, R%02u=%08X] -> ", gpu_pc-2, gpu_convert_zero[IMM_1] << 2, IMM_2, gpu_flag_n, gpu_flag_c, gpu_flag_z, gpu_convert_zero[IMM_1] << 2, gpu_reg[15]+(gpu_convert_zero[IMM_1] << 2), IMM_2, RN);
#endif
#ifdef GPU_CORRECT_ALIGNMENT
	uint32_t address = gpu_reg[15] + (gpu_convert_zero[IMM_1] << 2);

	if ((RM >= 0xF03000) && (RM <= 0xF03FFF))
		RN = GPUReadLong(address & 0xFFFFFFFC, GPU);
	else
		RN = GPUReadLong(address, GPU);
#else
	RN = GPUReadLong(gpu_reg[15] + (gpu_convert_zero[IMM_1] << 2), GPU);
#endif
#ifdef GPU_DIS_LOAD15I
	if (doGPUDis)
		//WriteLog("[NCZ:%u%u%u, R%02u=%08X]\n", gpu_flag_n, gpu_flag_c, gpu_flag_z, IMM_2, RN);
#endif
}


static void gpu_opcode_movei(void)
{
#ifdef GPU_DIS_MOVEI
	if (doGPUDis)
		//WriteLog("%06X: MOVEI  #$%08X, R%02u [NCZ:%u%u%u, R%02u=%08X] -> ", gpu_pc-2, (uint32_t)GPUReadWord(gpu_pc) | ((uint32_t)GPUReadWord(gpu_pc + 2) << 16), IMM_2, gpu_flag_n, gpu_flag_c, gpu_flag_z, IMM_2, RN);
#endif
	// This instruction is followed by 32-bit value in LSW / MSW format...
	RN = (uint32_t)GPUReadWord(gpu_pc, GPU) | ((uint32_t)GPUReadWord(gpu_pc + 2, GPU) << 16);
	gpu_pc += 4;
#ifdef GPU_DIS_MOVEI
	if (doGPUDis)
		//WriteLog("[NCZ:%u%u%u, R%02u=%08X]\n", gpu_flag_n, gpu_flag_c, gpu_flag_z, IMM_2, RN);
#endif
}


static void gpu_opcode_moveta(void)
{
#ifdef GPU_DIS_MOVETA
	if (doGPUDis)
		//WriteLog("%06X: MOVETA R%02u, R%02u [NCZ:%u%u%u, R%02u=%08X, R%02u(alt)=%08X] -> ", gpu_pc-2, IMM_1, IMM_2, gpu_flag_n, gpu_flag_c, gpu_flag_z, IMM_1, RM, IMM_2, ALTERNATE_RN);
#endif
	ALTERNATE_RN = RM;
#ifdef GPU_DIS_MOVETA
	if (doGPUDis)
		//WriteLog("[NCZ:%u%u%u, R%02u=%08X, R%02u(alt)=%08X]\n", gpu_flag_n, gpu_flag_c, gpu_flag_z, IMM_1, RM, IMM_2, ALTERNATE_RN);
#endif
}


static void gpu_opcode_movefa(void)
{
#ifdef GPU_DIS_MOVEFA
	if (doGPUDis)
		//WriteLog("%06X: MOVEFA R%02u, R%02u [NCZ:%u%u%u, R%02u(alt)=%08X, R%02u=%08X] -> ", gpu_pc-2, IMM_1, IMM_2, gpu_flag_n, gpu_flag_c, gpu_flag_z, IMM_1, ALTERNATE_RM, IMM_2, RN);
#endif
	RN = ALTERNATE_RM;
#ifdef GPU_DIS_MOVEFA
	if (doGPUDis)
		//WriteLog("[NCZ:%u%u%u, R%02u(alt)=%08X, R%02u=%08X]\n", gpu_flag_n, gpu_flag_c, gpu_flag_z, IMM_1, ALTERNATE_RM, IMM_2, RN);
#endif
}


static void gpu_opcode_move(void)
{
#ifdef GPU_DIS_MOVE
	if (doGPUDis)
		//WriteLog("%06X: MOVE   R%02u, R%02u [NCZ:%u%u%u, R%02u=%08X, R%02u=%08X] -> ", gpu_pc-2, IMM_1, IMM_2, gpu_flag_n, gpu_flag_c, gpu_flag_z, IMM_1, RM, IMM_2, RN);
#endif
	RN = RM;
#ifdef GPU_DIS_MOVE
	if (doGPUDis)
		//WriteLog("[NCZ:%u%u%u, R%02u=%08X, R%02u=%08X]\n", gpu_flag_n, gpu_flag_c, gpu_flag_z, IMM_1, RM, IMM_2, RN);
#endif
}


static void gpu_opcode_moveq(void)
{
#ifdef GPU_DIS_MOVEQ
	if (doGPUDis)
		//WriteLog("%06X: MOVEQ  #%u, R%02u [NCZ:%u%u%u, R%02u=%08X] -> ", gpu_pc-2, IMM_1, IMM_2, gpu_flag_n, gpu_flag_c, gpu_flag_z, IMM_2, RN);
#endif
	RN = IMM_1;
#ifdef GPU_DIS_MOVEQ
	if (doGPUDis)
		//WriteLog("[NCZ:%u%u%u, R%02u=%08X]\n", gpu_flag_n, gpu_flag_c, gpu_flag_z, IMM_2, RN);
#endif
}


static void gpu_opcode_resmac(void)
{
	RN = gpu_acc;
}


static void gpu_opcode_imult(void)
{
#ifdef GPU_DIS_IMULT
	if (doGPUDis)
		//WriteLog("%06X: IMULT  R%02u, R%02u [NCZ:%u%u%u, R%02u=%08X, R%02u=%08X] -> ", gpu_pc-2, IMM_1, IMM_2, gpu_flag_n, gpu_flag_c, gpu_flag_z, IMM_1, RM, IMM_2, RN);
#endif
	RN = (int16_t)RN * (int16_t)RM;
	SET_ZN(RN);
#ifdef GPU_DIS_IMULT
	if (doGPUDis)
		//WriteLog("[NCZ:%u%u%u, R%02u=%08X, R%02u=%08X]\n", gpu_flag_n, gpu_flag_c, gpu_flag_z, IMM_1, RM, IMM_2, RN);
#endif
}


static void gpu_opcode_mult(void)
{
#ifdef GPU_DIS_MULT
	if (doGPUDis)
		//WriteLog("%06X: MULT   R%02u, R%02u [NCZ:%u%u%u, R%02u=%08X, R%02u=%08X] -> ", gpu_pc-2, IMM_1, IMM_2, gpu_flag_n, gpu_flag_c, gpu_flag_z, IMM_1, RM, IMM_2, RN);
#endif
	RN = (uint16_t)RM * (uint16_t)RN;
//	RN = (RM & 0xFFFF) * (RN & 0xFFFF);
	SET_ZN(RN);
#ifdef GPU_DIS_MULT
	if (doGPUDis)
		//WriteLog("[NCZ:%u%u%u, R%02u=%08X, R%02u=%08X]\n", gpu_flag_n, gpu_flag_c, gpu_flag_z, IMM_1, RM, IMM_2, RN);
#endif
}


static void gpu_opcode_bclr(void)
{
#ifdef GPU_DIS_BCLR
	if (doGPUDis)
		//WriteLog("%06X: BCLR   #%u, R%02u [NCZ:%u%u%u, R%02u=%08X] -> ", gpu_pc-2, IMM_1, IMM_2, gpu_flag_n, gpu_flag_c, gpu_flag_z, IMM_2, RN);
#endif
	uint32_t res = RN & ~(1 << IMM_1);
	RN = res;
	SET_ZN(res);
#ifdef GPU_DIS_BCLR
	if (doGPUDis)
		//WriteLog("[NCZ:%u%u%u, R%02u=%08X]\n", gpu_flag_n, gpu_flag_c, gpu_flag_z, IMM_2, RN);
#endif
}


static void gpu_opcode_btst(void)
{
#ifdef GPU_DIS_BTST
	if (doGPUDis)
		//WriteLog("%06X: BTST   #%u, R%02u [NCZ:%u%u%u, R%02u=%08X] -> ", gpu_pc-2, IMM_1, IMM_2, gpu_flag_n, gpu_flag_c, gpu_flag_z, IMM_2, RN);
#endif
	gpu_flag_z = (~RN >> IMM_1) & 1;
#ifdef GPU_DIS_BTST
	if (doGPUDis)
		//WriteLog("[NCZ:%u%u%u, R%02u=%08X]\n", gpu_flag_n, gpu_flag_c, gpu_flag_z, IMM_2, RN);
#endif
}


static void gpu_opcode_bset(void)
{
#ifdef GPU_DIS_BSET
	if (doGPUDis)
		//WriteLog("%06X: BSET   #%u, R%02u [NCZ:%u%u%u, R%02u=%08X] -> ", gpu_pc-2, IMM_1, IMM_2, gpu_flag_n, gpu_flag_c, gpu_flag_z, IMM_2, RN);
#endif
	uint32_t res = RN | (1 << IMM_1);
	RN = res;
	SET_ZN(res);
#ifdef GPU_DIS_BSET
	if (doGPUDis)
		//WriteLog("[NCZ:%u%u%u, R%02u=%08X]\n", gpu_flag_n, gpu_flag_c, gpu_flag_z, IMM_2, RN);
#endif
}


static void gpu_opcode_imacn(void)
{
	uint32_t res = (int16_t)RM * (int16_t)(RN);
	gpu_acc += res;
}


static void gpu_opcode_mtoi(void)
{
	uint32_t _RM = RM;
	uint32_t res = RN = (((int32_t)_RM >> 8) & 0xFF800000) | (_RM & 0x007FFFFF);
	SET_ZN(res);
}


static void gpu_opcode_normi(void)
{
	uint32_t _RM = RM;
	uint32_t res = 0;

	if (_RM)
	{
		while ((_RM & 0xFFC00000) == 0)
		{
			_RM <<= 1;
			res--;
		}
		while ((_RM & 0xFF800000) != 0)
		{
			_RM >>= 1;
			res++;
		}
	}
	RN = res;
	SET_ZN(res);
}

static void gpu_opcode_mmult(void)
{
	int count	= gpu_matrix_control & 0x0F;	// Matrix width
	uint32_t addr = gpu_pointer_to_matrix;		// In the GPU's RAM
	int64_t accum = 0;
	uint32_t res;

	if (gpu_matrix_control & 0x10)				// Column stepping
	{
		for(int i=0; i<count; i++)
		{
			int16_t a;
			if (i & 0x01)
				a = (int16_t)((gpu_alternate_reg[IMM_1 + (i >> 1)] >> 16) & 0xFFFF);
			else
				a = (int16_t)(gpu_alternate_reg[IMM_1 + (i >> 1)] & 0xFFFF);

			int16_t b = ((int16_t)GPUReadWord(addr + 2, GPU));
			accum += a * b;
			addr += 4 * count;
		}
	}
	else										// Row stepping
	{
		for(int i=0; i<count; i++)
		{
			int16_t a;
			if (i & 0x01)
				a = (int16_t)((gpu_alternate_reg[IMM_1 + (i >> 1)] >> 16) & 0xFFFF);
			else
				a = (int16_t)(gpu_alternate_reg[IMM_1 + (i >> 1)] & 0xFFFF);

			int16_t b = ((int16_t)GPUReadWord(addr + 2, GPU));
			accum += a * b;
			addr += 4;
		}
	}
	RN = res = (int32_t)accum;
	// carry flag to do (out of the last add)
	SET_ZN(res);
}


static void gpu_opcode_abs(void)
{
#ifdef GPU_DIS_ABS
	if (doGPUDis)
		//WriteLog("%06X: ABS    R%02u [NCZ:%u%u%u, R%02u=%08X] -> ", gpu_pc-2, IMM_2, gpu_flag_n, gpu_flag_c, gpu_flag_z, IMM_2, RN);
#endif
	gpu_flag_c = RN >> 31;
	if (RN == 0x80000000)
	//Is 0x80000000 a positive number? If so, then we need to set C to 0 as well!
		gpu_flag_n = 1, gpu_flag_z = 0;
	else
	{
		if (gpu_flag_c)
			RN = -RN;
		gpu_flag_n = 0; SET_FLAG_Z(RN);
	}
#ifdef GPU_DIS_ABS
	if (doGPUDis)
		//WriteLog("[NCZ:%u%u%u, R%02u=%08X]\n", gpu_flag_n, gpu_flag_c, gpu_flag_z, IMM_2, RN);
#endif
}


static void gpu_opcode_div(void)	// RN / RM
{
#ifdef GPU_DIS_DIV
	if (doGPUDis)
		//WriteLog("%06X: DIV    R%02u, R%02u (%s) [NCZ:%u%u%u, R%02u=%08X, R%02u=%08X] -> ", gpu_pc-2, IMM_1, IMM_2, (gpu_div_control & 0x01 ? "16.16" : "32"), gpu_flag_n, gpu_flag_c, gpu_flag_z, IMM_1, RM, IMM_2, RN);
#endif
#if 0
	if (RM)
	{
		if (gpu_div_control & 0x01)		// 16.16 division
		{
			gpu_remain = ((uint64_t)RN << 16) % RM;
			RN = ((uint64_t)RN << 16) / RM;
		}
		else
		{
			// We calculate the remainder first because we destroy RN after
			// this by assigning it to itself.
			gpu_remain = RN % RM;
			RN = RN / RM;
		}
	}
	else
	{
		// This is what happens according to SCPCD. NYAN!
		RN = 0xFFFFFFFF;
		gpu_remain = 0;
	}
#else
	// Real algorithm, courtesy of SCPCD: NYAN!
	uint32_t q = RN;
	uint32_t r = 0;

	// If 16.16 division, stuff top 16 bits of RN into remainder and put the
	// bottom 16 of RN in top 16 of quotient
	if (gpu_div_control & 0x01)
		q <<= 16, r = RN >> 16;

	for(int i=0; i<32; i++)
	{
//		uint32_t sign = (r >> 31) & 0x01;
		uint32_t sign = r & 0x80000000;
		r = (r << 1) | ((q >> 31) & 0x01);
		r += (sign ? RM : -RM);
		q = (q << 1) | (((~r) >> 31) & 0x01);
	}

	RN = q;
	gpu_remain = r;
#endif

#ifdef GPU_DIS_DIV
	if (doGPUDis)
		//WriteLog("[NCZ:%u%u%u, R%02u=%08X, R%02u=%08X] Remainder: %08X\n", gpu_flag_n, gpu_flag_c, gpu_flag_z, IMM_1, RM, IMM_2, RN, gpu_remain);
#endif
}


static void gpu_opcode_imultn(void)
{
	uint32_t res = (int32_t)((int16_t)RN * (int16_t)RM);
	gpu_acc = (int32_t)res;
	SET_FLAG_Z(res);
	SET_FLAG_N(res);
}


static void gpu_opcode_neg(void)
{
#ifdef GPU_DIS_NEG
	if (doGPUDis)
		//WriteLog("%06X: NEG    R%02u [NCZ:%u%u%u, R%02u=%08X] -> ", gpu_pc-2, IMM_2, gpu_flag_n, gpu_flag_c, gpu_flag_z, IMM_2, RN);
#endif
	uint32_t res = -RN;
	SET_ZNC_SUB(0, RN, res);
	RN = res;
#ifdef GPU_DIS_NEG
	if (doGPUDis)
		//WriteLog("[NCZ:%u%u%u, R%02u=%08X]\n", gpu_flag_n, gpu_flag_c, gpu_flag_z, IMM_2, RN);
#endif
}


static void gpu_opcode_shlq(void)
{
#ifdef GPU_DIS_SHLQ
	if (doGPUDis)
		//WriteLog("%06X: SHLQ   #%u, R%02u [NCZ:%u%u%u, R%02u=%08X] -> ", gpu_pc-2, 32 - IMM_1, IMM_2, gpu_flag_n, gpu_flag_c, gpu_flag_z, IMM_2, RN);
#endif
// Was a bug here...
// (Look at Aaron's code: If r1 = 32, then 32 - 32 = 0 which is wrong!)
	int32_t r1 = 32 - IMM_1;
	uint32_t res = RN << r1;
	SET_ZN(res); gpu_flag_c = (RN >> 31) & 1;
	RN = res;
#ifdef GPU_DIS_SHLQ
	if (doGPUDis)
		//WriteLog("[NCZ:%u%u%u, R%02u=%08X]\n", gpu_flag_n, gpu_flag_c, gpu_flag_z, IMM_2, RN);
#endif
}


static void gpu_opcode_shrq(void)
{
#ifdef GPU_DIS_SHRQ
	if (doGPUDis)
		//WriteLog("%06X: SHRQ   #%u, R%02u [NCZ:%u%u%u, R%02u=%08X] -> ", gpu_pc-2, gpu_convert_zero[IMM_1], IMM_2, gpu_flag_n, gpu_flag_c, gpu_flag_z, IMM_2, RN);
#endif
	int32_t r1 = gpu_convert_zero[IMM_1];
	uint32_t res = RN >> r1;
	SET_ZN(res); gpu_flag_c = RN & 1;
	RN = res;
#ifdef GPU_DIS_SHRQ
	if (doGPUDis)
		//WriteLog("[NCZ:%u%u%u, R%02u=%08X]\n", gpu_flag_n, gpu_flag_c, gpu_flag_z, IMM_2, RN);
#endif
}


static void gpu_opcode_ror(void)
{
#ifdef GPU_DIS_ROR
	if (doGPUDis)
		//WriteLog("%06X: ROR    R%02u, R%02u [NCZ:%u%u%u, R%02u=%08X, R%02u=%08X] -> ", gpu_pc-2, IMM_1, IMM_2, gpu_flag_n, gpu_flag_c, gpu_flag_z, IMM_1, RM, IMM_2, RN);
#endif
	uint32_t r1 = RM & 0x1F;
	uint32_t res = (RN >> r1) | (RN << (32 - r1));
	SET_ZN(res); gpu_flag_c = (RN >> 31) & 1;
	RN = res;
#ifdef GPU_DIS_ROR
	if (doGPUDis)
		//WriteLog("[NCZ:%u%u%u, R%02u=%08X, R%02u=%08X]\n", gpu_flag_n, gpu_flag_c, gpu_flag_z, IMM_1, RM, IMM_2, RN);
#endif
}


static void gpu_opcode_rorq(void)
{
#ifdef GPU_DIS_RORQ
	if (doGPUDis)
		//WriteLog("%06X: RORQ   #%u, R%02u [NCZ:%u%u%u, R%02u=%08X] -> ", gpu_pc-2, gpu_convert_zero[IMM_1], IMM_2, gpu_flag_n, gpu_flag_c, gpu_flag_z, IMM_2, RN);
#endif
	uint32_t r1 = gpu_convert_zero[IMM_1 & 0x1F];
	uint32_t r2 = RN;
	uint32_t res = (r2 >> r1) | (r2 << (32 - r1));
	RN = res;
	SET_ZN(res); gpu_flag_c = (r2 >> 31) & 0x01;
#ifdef GPU_DIS_RORQ
	if (doGPUDis)
		//WriteLog("[NCZ:%u%u%u, R%02u=%08X]\n", gpu_flag_n, gpu_flag_c, gpu_flag_z, IMM_2, RN);
#endif
}


static void gpu_opcode_sha(void)
{
/*	int dreg = jaguar.op & 31;
	int32_t r1 = (int32_t)jaguar.r[(jaguar.op >> 5) & 31];
	uint32_t r2 = jaguar.r[dreg];
	uint32_t res;

	CLR_ZNC;
	if (r1 < 0)
	{
		res = (r1 <= -32) ? 0 : (r2 << -r1);
		jaguar.FLAGS |= (r2 >> 30) & 2;
	}
	else
	{
		res = (r1 >= 32) ? ((int32_t)r2 >> 31) : ((int32_t)r2 >> r1);
		jaguar.FLAGS |= (r2 << 1) & 2;
	}
	jaguar.r[dreg] = res;
	SET_ZN(res);*/

#ifdef GPU_DIS_SHA
	if (doGPUDis)
		//WriteLog("%06X: SHA    R%02u, R%02u [NCZ:%u%u%u, R%02u=%08X, R%02u=%08X] -> ", gpu_pc-2, IMM_1, IMM_2, gpu_flag_n, gpu_flag_c, gpu_flag_z, IMM_1, RM, IMM_2, RN);
#endif
	uint32_t res;

	if ((int32_t)RM < 0)
	{
		res = ((int32_t)RM <= -32) ? 0 : (RN << -(int32_t)RM);
		gpu_flag_c = RN >> 31;
	}
	else
	{
		res = ((int32_t)RM >= 32) ? ((int32_t)RN >> 31) : ((int32_t)RN >> (int32_t)RM);
		gpu_flag_c = RN & 0x01;
	}
	RN = res;
	SET_ZN(res);
#ifdef GPU_DIS_SHA
	if (doGPUDis)
		//WriteLog("[NCZ:%u%u%u, R%02u=%08X, R%02u=%08X]\n", gpu_flag_n, gpu_flag_c, gpu_flag_z, IMM_1, RM, IMM_2, RN);
#endif

/*	int32_t sRM=(int32_t)RM;
	uint32_t _RN=RN;

	if (sRM<0)
	{
		uint32_t shift=-sRM;
		if (shift>=32) shift=32;
		gpu_flag_c=(_RN&0x80000000)>>31;
		while (shift)
		{
			_RN<<=1;
			shift--;
		}
	}
	else
	{
		uint32_t shift=sRM;
		if (shift>=32) shift=32;
		gpu_flag_c=_RN&0x1;
		while (shift)
		{
			_RN=((int32_t)_RN)>>1;
			shift--;
		}
	}
	RN=_RN;
	SET_FLAG_Z(_RN);
	SET_FLAG_N(_RN);*/
}


static void gpu_opcode_sharq(void)
{
#ifdef GPU_DIS_SHARQ
	if (doGPUDis)
		//WriteLog("%06X: SHARQ  #%u, R%02u [NCZ:%u%u%u, R%02u=%08X] -> ", gpu_pc-2, gpu_convert_zero[IMM_1], IMM_2, gpu_flag_n, gpu_flag_c, gpu_flag_z, IMM_2, RN);
#endif
	uint32_t res = (int32_t)RN >> gpu_convert_zero[IMM_1];
	SET_ZN(res); gpu_flag_c = RN & 0x01;
	RN = res;
#ifdef GPU_DIS_SHARQ
	if (doGPUDis)
		//WriteLog("[NCZ:%u%u%u, R%02u=%08X]\n", gpu_flag_n, gpu_flag_c, gpu_flag_z, IMM_2, RN);
#endif
}


static void gpu_opcode_sh(void)
{
	if (RM & 0x80000000)		// Shift left
	{
		gpu_flag_c = RN >> 31;
		RN = ((int32_t)RM <= -32 ? 0 : RN << -(int32_t)RM);
	}
	else						// Shift right
	{
		gpu_flag_c = RN & 0x01;
		RN = (RM >= 32 ? 0 : RN >> RM);
	}
	SET_ZN(RN);
}


