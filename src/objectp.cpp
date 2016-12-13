//
// Object Processor
//
// Original source by Cal2
// GCC/SDL port by Niels Wagenaar (Linux/WIN32) and Caz (BeOS)
// Extensive cleanups/fixes/rewrites by James L. Hammons
//

//#include <stdio.h>
//#include <stdlib.h>
#include <string.h>
#include "jaguar.h"

//#define OP_DEBUG
//#define OP_DEBUG_BMP

#define BLEND_Y(dst, src)	op_blend_y[(((uint16)dst<<8)) | ((uint16)(src))]
#define BLEND_CR(dst, src)	op_blend_cr[(((uint16)dst)<<8) | ((uint16)(src))]

#define OBJECT_TYPE_BITMAP	0			// 000
#define OBJECT_TYPE_SCALE	1			// 001
#define OBJECT_TYPE_GPU		2			// 010
#define OBJECT_TYPE_BRANCH	3			// 011
#define OBJECT_TYPE_STOP	4			// 100

#define CONDITION_EQUAL				0
#define CONDITION_LESS_THAN			1
#define CONDITION_GREATER_THAN		2
#define CONDITION_OP_FLAG_SET		3
#define CONDITION_SECOND_HALF_LINE	4

#define OPFLAG_RELEASE		8			// Bus release bit
#define OPFLAG_TRANS		4			// Transparency bit
#define OPFLAG_RMW			2			// Read-Modify-Write bit
#define OPFLAG_REFLECT		1			// Horizontal mirror bit

// Private function prototypes

void OPProcessFixedBitmap(uint64 p0, uint64 p1, bool render);
void OPProcessScaledBitmap(uint64 p0, uint64 p1, uint64 p2, bool render);
void DumpScaledObject(uint64 p0, uint64 p1, uint64 p2);
void DumpFixedObject(uint64 p0, uint64 p1);
uint64 op_load_phrase(uint32 offset);

// Local global variables

static uint8 * op_blend_y;
static uint8 * op_blend_cr;
// There may be a problem with this "RAM" overlapping (and thus being independent of)
// some of the regular TOM RAM...
static uint8 objectp_ram[0x40];			// This is based at $F00000
uint8 objectp_running;
//bool objectp_stop_reading_list;

static uint8 op_bitmap_bit_depth[8] = { 1, 2, 4, 8, 16, 24, 32, 0 };
//static uint32 op_bitmap_bit_size[8] =
//	{ (uint32)(0.125*65536), (uint32)(0.25*65536), (uint32)(0.5*65536), (uint32)(1*65536),
//	  (uint32)(2*65536),     (uint32)(1*65536),    (uint32)(1*65536),   (uint32)(1*65536) };
static uint32 op_pointer;

int32 phraseWidthToPixels[8] = { 64, 32, 16, 8, 4, 2, 0, 0 };


//
// Object Processor initialization
//
void op_init(void)
{
	// Blend tables (64K each)
	memory_malloc_secure((void **)&op_blend_y, 0x10000, "Jaguar Object processor Y blend lookup table");
	memory_malloc_secure((void **)&op_blend_cr, 0x10000, "Jaguar Object processor CR blend lookup table");

	// Here we calculate the saturating blend of a signed 4-bit value and an
	// existing Cyan/Red value as well as a signed 8-bit value and an existing intensity...
	// Note: CRY is 4 bits Cyan, 4 bits Red, 16 bits intensitY
	for(int i=0; i<256*256; i++)
	{
		int y = (i >> 8) & 0xFF;
		int dy = (INT8)i;					// Sign extend the Y index
		int c1 = (i >> 8) & 0x0F;
		int dc1 = (INT8)(i << 4) >> 4;		// Sign extend the R index
		int c2 = (i >> 12) & 0x0F;
		int dc2 = (INT8)(i & 0xF0) >> 4;	// Sign extend the C index

		y += dy;
		if (y < 0)
			y = 0;
		else if (y > 0xFF)
			y = 0xFF;
		op_blend_y[i] = y;

		c1 += dc1;
		if (c1 < 0)
			c1 = 0;
		else if (c1 > 0x0F)
			c1 = 0x0F;
		c2 += dc2;

		if (c2 < 0)
			c2 = 0;
		else if (c2 > 0x0F)
			c2 = 0x0F;
		op_blend_cr[i] = (c2 << 4) | c1;
	}

	op_reset();
}

//
// Object Processor reset
//
void op_reset(void)
{
	memset(objectp_ram, 0x00, 0x40);
	objectp_running = 0;
}

void op_done(void)
{
	char * opType[8] =
	{ "(BITMAP)", "(SCALED BITMAP)", "(GPU INT)", "(BRANCH)", "(STOP)", "???", "???", "???" };
	char * ccType[8] =
		{ "\"==\"", "\"<\"", "\">\"", "(opflag set)", "(second half line)", "?", "?", "?" };

	uint32 olp = op_get_list_pointer();
	//WriteLog("OP: OLP = %08X\n", olp);
	//WriteLog("OP: Phrase dump\n    ----------\n");
	for(uint32 i=0; i<0x100; i+=8)
	{
		uint32 hi = JaguarReadLong(olp + i, OP), lo = JaguarReadLong(olp + i + 4, OP);
		//WriteLog("\t%08X: %08X %08X %s", olp + i, hi, lo, opType[lo & 0x07]);
		if ((lo & 0x07) == 3)
		{
			uint16 ypos = (lo >> 3) & 0x7FF;
			uint8  cc   = (lo >> 14) & 0x03;
			uint32 link = ((hi << 11) | (lo >> 21)) & 0x3FFFF8;
			//WriteLog(" YPOS=%u, CC=%s, link=%08X", ypos, ccType[cc], link);
		}
		//WriteLog("\n");
		if ((lo & 0x07) == 0)
			DumpFixedObject(op_load_phrase(olp+i), op_load_phrase(olp+i+8));
		if ((lo & 0x07) == 1)
			DumpScaledObject(op_load_phrase(olp+i), op_load_phrase(olp+i+8), op_load_phrase(olp+i+16));
	}
	//WriteLog("\n");

	memory_free(op_blend_y);
	memory_free(op_blend_cr);
}

//
// Object Processor memory access
// Memory range: F00010 - F00027
//
//	F00010-F00017   R     xxxxxxxx xxxxxxxx   OB - current object code from the graphics processor
//	F00020-F00023     W   xxxxxxxx xxxxxxxx   OLP - start of the object list
//	F00026            W   -------- -------x   OBF - object processor flag
//

uint8 OPReadByte(uint32 offset, uint32 who/*=UNKNOWN*/)
{
	offset &= 0x3F;
	return objectp_ram[offset];
}

uint16 OPReadWord(uint32 offset, uint32 who/*=UNKNOWN*/)
{
	offset &= 0x3F;
	return GET16(objectp_ram, offset);
}

void OPWriteByte(uint32 offset, uint8 data, uint32 who/*=UNKNOWN*/)
{
	offset &= 0x3F;
	objectp_ram[offset] = data;
}

void OPWriteWord(uint32 offset, uint16 data, uint32 who/*=UNKNOWN*/)
{
	offset &= 0x3F;
	SET16(objectp_ram, offset, data);

/*if (offset == 0x20)
//WriteLog("OP: Setting lo list pointer: %04X\n", data);
if (offset == 0x22)
//WriteLog("OP: Setting hi list pointer: %04X\n", data);//*/
}

uint32 op_get_list_pointer(void)
{
	// Note: This register is LO / HI WORD, hence the funky look of this...
//	return (objectp_ram[0x22] << 24) | (objectp_ram[0x23] << 16) | (objectp_ram[0x20] << 8) | objectp_ram[0x21];
	return GET16(objectp_ram, 0x20) | (GET16(objectp_ram, 0x22) << 16);
}

// This is WRONG, since the OBF is only 16 bits wide!!! [FIXED]

uint32 op_get_status_register(void)
{
//	return (objectp_ram[0x26] << 24) | (objectp_ram[0x27] << 16) | (objectp_ram[0x28] << 8) | objectp_ram[0x29];
//	return GET32(objectp_ram, 0x26);
	return GET16(objectp_ram, 0x26);
}

// This is WRONG, since the OBF is only 16 bits wide!!! [FIXED]

void op_set_status_register(uint32 data)
{
/*	objectp_ram[0x26] = (data & 0xFF000000) >> 24;
	objectp_ram[0x27] = (data & 0x00FF0000) >> 16;
	objectp_ram[0x28] = (data & 0x0000FF00) >> 8;
	objectp_ram[0x29] |= (data & 0xFE);*/
	objectp_ram[0x26] = (data & 0x0000FF00) >> 8;
	objectp_ram[0x27] |= (data & 0xFE);
}

void op_set_current_object(uint64 object)
{
//Not sure this is right... Wouldn't it just be stored 64 bit BE?
	// Stored as least significant 32 bits first, ms32 last in big endian
/*	objectp_ram[0x13] = object & 0xFF; object >>= 8;
	objectp_ram[0x12] = object & 0xFF; object >>= 8;
	objectp_ram[0x11] = object & 0xFF; object >>= 8;
	objectp_ram[0x10] = object & 0xFF; object >>= 8;

	objectp_ram[0x17] = object & 0xFF; object >>= 8;
	objectp_ram[0x16] = object & 0xFF; object >>= 8;
	objectp_ram[0x15] = object & 0xFF; object >>= 8;
	objectp_ram[0x14] = object & 0xFF;*/
// Let's try regular good old big endian...
	objectp_ram[0x17] = object & 0xFF; object >>= 8;
	objectp_ram[0x16] = object & 0xFF; object >>= 8;
	objectp_ram[0x15] = object & 0xFF; object >>= 8;
	objectp_ram[0x14] = object & 0xFF; object >>= 8;

	objectp_ram[0x13] = object & 0xFF; object >>= 8;
	objectp_ram[0x12] = object & 0xFF; object >>= 8;
	objectp_ram[0x11] = object & 0xFF; object >>= 8;
	objectp_ram[0x10] = object & 0xFF;
}

uint64 op_load_phrase(uint32 offset)
{
	offset &= ~0x07;						// 8 byte alignment
	return ((uint64)JaguarReadLong(offset, OP) << 32) | (uint64)JaguarReadLong(offset+4, OP);
}

void OPStorePhrase(uint32 offset, uint64 p)
{
	offset &= ~0x07;						// 8 byte alignment
	JaguarWriteLong(offset, p >> 32, OP);
	JaguarWriteLong(offset + 4, p & 0xFFFFFFFF, OP);
}

//
// Debugging routines
//
void DumpScaledObject(uint64 p0, uint64 p1, uint64 p2)
{
}

void DumpFixedObject(uint64 p0, uint64 p1)
{
}

//
// Object Processor main routine
//
//Need to fix this so that when an GPU object IRQ happens, we can pick up OP processing
//where we left off. !!! FIX !!!
void OPProcessList(int scanline, bool render)
{
//	char * condition_to_str[8] =
//		{ "==", "<", ">", "(opflag set)", "(second half line)", "?", "?", "?" };

	op_pointer = op_get_list_pointer();

//	objectp_stop_reading_list = false;

////WriteLog("OP: Processing line #%u (OLP=%08X)...\n", scanline, op_pointer);
//op_done();

// *** BEGIN OP PROCESSOR TESTING ONLY ***
extern bool interactiveMode;
extern bool iToggle;
extern int objectPtr;
bool inhibit;
int bitmapCounter = 0;
// *** END OP PROCESSOR TESTING ONLY ***

	uint32 opCyclesToRun = 10000;					// This is a pulled-out-of-the-air value (will need to be fixed, obviously!)

//	if (op_pointer) //WriteLog(" new op list at 0x%.8x scanline %i\n",op_pointer,scanline);
	while (op_pointer)
	{
// *** BEGIN OP PROCESSOR TESTING ONLY ***
if (interactiveMode && bitmapCounter == objectPtr)
	inhibit = iToggle;
else
	inhibit = false;
// *** END OP PROCESSOR TESTING ONLY ***
//		if (objectp_stop_reading_list)
//			return;
			
		uint64 p0 = op_load_phrase(op_pointer);
////WriteLog("\t%08X type %i\n", op_pointer, (uint8)p0 & 0x07);
		op_pointer += 8;

		switch ((uint8)p0 & 0x07)
		{
		case OBJECT_TYPE_BITMAP:
		{
//WAS:			uint16 ypos = (p0 >> 3) & 0x3FF;
			uint16 ypos = (p0 >> 3) & 0x7FF;
// This is only theory implied by Rayman...!
// It seems that if the YPOS is zero, then bump the YPOS value so that it coincides with
// the VDB value. With interlacing, this would be slightly more tricky.
// There's probably another bit somewhere that enables this mode--but so far, doesn't seem
// to affect any other game in a negative way (that I've seen).
// Either that, or it's an undocumented bug...

//No, the reason this was needed is that the OP code before was wrong. Any value
//less than VDB will get written to the top line of the display!
//			if (ypos == 0)
//				ypos = TOMReadWord(0xF00046, OP) / 2;			// Get the VDB value
			uint32 height = (p0 & 0xFFC000) >> 14;
			uint32 oldOPP = op_pointer - 8;
// *** BEGIN OP PROCESSOR TESTING ONLY ***

bitmapCounter++;
if (!inhibit)	// For OP testing only!
// *** END OP PROCESSOR TESTING ONLY ***
			if (scanline >= ypos && height > 0)
			{
				uint64 p1 = op_load_phrase(op_pointer);
				op_pointer += 8;
////WriteLog("OP: Writing scanline %d with ypos == %d...\n", scanline, ypos);
////WriteLog("--> Writing %u BPP bitmap...\n", op_bitmap_bit_depth[(p1 >> 12) & 0x07]);
//				OPProcessFixedBitmap(scanline, p0, p1, render);
				OPProcessFixedBitmap(p0, p1, render);

				// OP write-backs

//???Does this really happen??? Doesn't seem to work if you do this...!
//Probably not. Must be a bug in the documentation...!
//				uint32 link = (p0 & 0x7FFFF000000) >> 21;
//				SET16(objectp_ram, 0x20, link & 0xFFFF);	// OLP
//				SET16(objectp_ram, 0x22, link >> 16);
/*				uint32 height = (p0 & 0xFFC000) >> 14;
				if (height - 1 > 0)
					height--;*/
				// NOTE: Would subtract 2 if in interlaced mode...!
//				uint64 height = ((p0 & 0xFFC000) - 0x4000) & 0xFFC000;
//				if (height)
				height--;

				uint64 data = (p0 & 0xFFFFF80000000000LL) >> 40;
				uint64 dwidth = (p1 & 0xFFC0000) >> 15;
				data += dwidth;

				p0 &= ~0xFFFFF80000FFC000LL;		// Mask out old data...
				p0 |= (uint64)height << 14;
				p0 |= data << 40;
				OPStorePhrase(oldOPP, p0);
			}
////WriteLog("\t\tOld OP: %08X -> ", op_pointer);
//Temp, for testing...
//No doubt, this type of check will break all kinds of stuff... !!! FIX !!!
//And it does! !!! FIX !!!
//Let's remove this "fix" since it screws up more than it fixes.
/*	if (op_pointer > ((p0 & 0x000007FFFF000000LL) >> 21))
		return;*/

			op_pointer = (p0 & 0x000007FFFF000000LL) >> 21;
////WriteLog("New OP: %08X\n", op_pointer);
			break;
		}
		case OBJECT_TYPE_SCALE:
		{
//WAS:			uint16 ypos = (p0 >> 3) & 0x3FF;
			uint16 ypos = (p0 >> 3) & 0x7FF;
			uint32 height = (p0 & 0xFFC000) >> 14;
			uint32 oldOPP = op_pointer - 8;
// *** BEGIN OP PROCESSOR TESTING ONLY ***

bitmapCounter++;
if (!inhibit)	// For OP testing only!
// *** END OP PROCESSOR TESTING ONLY ***
			if (scanline >= ypos && height > 0)
			{
				uint64 p1 = op_load_phrase(op_pointer);
				op_pointer += 8;
				uint64 p2 = op_load_phrase(op_pointer);
				op_pointer += 8;
////WriteLog("OP: %08X (%d) %08X%08X %08X%08X %08X%08X\n", oldOPP, scanline, (uint32)(p0>>32), (uint32)(p0&0xFFFFFFFF), (uint32)(p1>>32), (uint32)(p1&0xFFFFFFFF), (uint32)(p2>>32), (uint32)(p2&0xFFFFFFFF));
				OPProcessScaledBitmap(p0, p1, p2, render);

				// OP write-backs

				uint8 remainder = p2 >> 16, vscale = p2 >> 8;
//Actually, we should skip this object if it has a vscale of zero.
//Or do we? Not sure... Atari Karts has a few lines that look like:
// (SCALED BITMAP)
//000E8268 --> phrase 00010000 7000B00D 
//    [7 (0) x 1 @ (13, 0) (8 bpp), l: 000E82A0, p: 000E0FC0 fp: 00, fl:RELEASE, idx:00, pt:01]
//    [hsc: 9A, vsc: 00, rem: 00]
// Could it be the vscale is overridden if the DWIDTH is zero? Hmm...

				if (vscale == 0)
					vscale = 0x20;					// OP bug??? Nope, it isn't...! Or is it?

//extern int start_logging;
//if (start_logging)
//	//WriteLog("--> Returned from scaled bitmap processing (rem=%02X, vscale=%02X)...\n", remainder, vscale);//*/
//Locks up here:
//--> Returned from scaled bitmap processing (rem=20, vscale=80)...
//There are other problems here, it looks like...
//Another lock up:
//About to execute OP (508)...
/*
OP: Scaled bitmap 4x? 4bpp at 38,? hscale=7C fpix=0 data=00075E28 pitch 1 hflipped=no dwidth=? (linked to 00071118) Transluency=no
--> Returned from scaled bitmap processing (rem=50, vscale=7C)...
OP: Scaled bitmap 4x? 4bpp at 38,? hscale=7C fpix=0 data=00075E28 pitch 1 hflipped=no dwidth=? (linked to 00071118) Transluency=no
--> Returned from scaled bitmap processing (rem=30, vscale=7C)...
OP: Scaled bitmap 4x? 4bpp at 38,? hscale=7C fpix=0 data=00075E28 pitch 1 hflipped=no dwidth=? (linked to 00071118) Transluency=no
--> Returned from scaled bitmap processing (rem=10, vscale=7C)...
OP: Scaled bitmap 4x? 4bpp at 36,? hscale=7E fpix=0 data=000756A8 pitch 1 hflipped=no dwidth=? (linked to 00073058) Transluency=no
--> Returned from scaled bitmap processing (rem=00, vscale=7E)...
OP: Scaled bitmap 4x? 4bpp at 34,? hscale=80 fpix=0 data=000756C8 pitch 1 hflipped=no dwidth=? (linked to 00073078) Transluency=no
--> Returned from scaled bitmap processing (rem=00, vscale=80)...
OP: Scaled bitmap 4x? 4bpp at 36,? hscale=7E fpix=0 data=000756C8 pitch 1 hflipped=no dwidth=? (linked to 00073058) Transluency=no
--> Returned from scaled bitmap processing (rem=5E, vscale=7E)...
OP: Scaled bitmap 4x? 4bpp at 34,? hscale=80 fpix=0 data=000756E8 pitch 1 hflipped=no dwidth=? (linked to 00073078) Transluency=no
--> Returned from scaled bitmap processing (rem=60, vscale=80)...
OP: Scaled bitmap 4x? 4bpp at 36,? hscale=7E fpix=0 data=000756C8 pitch 1 hflipped=no dwidth=? (linked to 00073058) Transluency=no
--> Returned from scaled bitmap processing (rem=3E, vscale=7E)...
OP: Scaled bitmap 4x? 4bpp at 34,? hscale=80 fpix=0 data=000756E8 pitch 1 hflipped=no dwidth=? (linked to 00073078) Transluency=no
--> Returned from scaled bitmap processing (rem=40, vscale=80)...
OP: Scaled bitmap 4x? 4bpp at 36,? hscale=7E fpix=0 data=000756C8 pitch 1 hflipped=no dwidth=? (linked to 00073058) Transluency=no
--> Returned from scaled bitmap processing (rem=1E, vscale=7E)...
OP: Scaled bitmap 4x? 4bpp at 34,? hscale=80 fpix=0 data=000756E8 pitch 1 hflipped=no dwidth=? (linked to 00073078) Transluency=no
--> Returned from scaled bitmap processing (rem=20, vscale=80)...
*/
//Here's another problem:
//    [hsc: 20, vsc: 20, rem: 00]
// Since we're not checking for $E0 (but that's what we get from the above), we end
// up repeating this scanline unnecessarily... !!! FIX !!! [DONE, but... still not quite
// right. Either that, or the Accolade team that wrote Bubsy screwed up royal.]
//Also note: $E0 = 7.0 which IS a legal vscale value...

//				if (remainder & 0x80)				// I.e., it's negative
//				if ((remainder & 0x80) || remainder == 0)	// I.e., it's <= 0
//				if ((remainder - 1) >= 0xE0)		// I.e., it's <= 0
//				if ((remainder >= 0xE1) || remainder == 0)// I.e., it's <= 0
//				if ((remainder >= 0xE1 && remainder <= 0xFF) || remainder == 0)// I.e., it's <= 0
				if (remainder < 0x20)				// I.e., it's <= 0
				{
					uint64 data = (p0 & 0xFFFFF80000000000LL) >> 40;
					uint64 dwidth = (p1 & 0xFFC0000) >> 15;

//					while (remainder & 0x80)
//					while ((remainder & 0x80) || remainder == 0)
//					while ((remainder - 1) >= 0xE0)
//					while ((remainder >= 0xE1) || remainder == 0)
//					while ((remainder >= 0xE1 && remainder <= 0xFF) || remainder == 0)
					while (remainder < 0x20)
					{
						remainder += vscale;

						if (height)
							height--;

						data += dwidth;
					}

					p0 &= ~0xFFFFF80000FFC000LL;	// Mask out old data...
					p0 |= (uint64)height << 14;
					p0 |= data << 40;
					OPStorePhrase(oldOPP, p0);
				}

				remainder -= 0x20;					// 1.0f in [3.5] fixed point format

//if (start_logging)
//	//WriteLog("--> Finished writebacks...\n");//*/

////WriteLog(" [%08X%08X -> ", (uint32)(p2>>32), (uint32)(p2&0xFFFFFFFF));
				p2 &= ~0x0000000000FF0000LL;
				p2 |= (uint64)remainder << 16;
////WriteLog("%08X%08X]\n", (uint32)(p2>>32), (uint32)(p2&0xFFFFFFFF));
				OPStorePhrase(oldOPP+16, p2);
//remainder = (uint8)(p2 >> 16), vscale = (uint8)(p2 >> 8);
////WriteLog(" [after]: rem=%02X, vscale=%02X\n", remainder, vscale);
			}
			op_pointer = (p0 & 0x000007FFFF000000LL) >> 21;
			break;
		}
		case OBJECT_TYPE_GPU:
		{
////WriteLog("OP: Asserting GPU IRQ #3...\n");
			op_set_current_object(p0);
			GPUSetIRQLine(3, ASSERT_LINE);
//Also, OP processing is suspended from this point until OBF (F00026) is written to...
// !!! FIX !!!
//Do something like:
//OPSuspendedByGPU = true;
//Dunno if the OP keeps processing from where it was interrupted, or if it just continues
//on the next scanline...
// --> It continues from where it was interrupted! !!! FIX !!!
			break;
		}
		case OBJECT_TYPE_BRANCH:
		{
			uint16 ypos = (p0 >> 3) & 0x7FF;
			uint8  cc   = (p0 >> 14) & 0x03;
			uint32 link = (p0 >> 21) & 0x3FFFF8;
			
//			if ((ypos!=507)&&(ypos!=25))
//				//WriteLog("\t%i%s%i link=0x%.8x\n",scanline,condition_to_str[cc],ypos>>1,link);
			switch (cc)
			{
			case CONDITION_EQUAL:
				if (TOMReadWord(0xF00006, OP) == ypos || ypos == 0x7FF)
					op_pointer = link;
				break;
			case CONDITION_LESS_THAN:
				if (TOMReadWord(0xF00006, OP) < ypos)
					op_pointer = link;
				break;
			case CONDITION_GREATER_THAN:
				if (TOMReadWord(0xF00006, OP) > ypos)
					op_pointer = link;
				break;
			case CONDITION_OP_FLAG_SET:
				if (op_get_status_register() & 0x01)
					op_pointer = link;
				break;
			case CONDITION_SECOND_HALF_LINE:
				// This basically means branch if bit 10 of HC is set
				////WriteLog("OP: Unexpected CONDITION_SECOND_HALF_LINE in BRANCH object\nOP: shuting down\n");
				//fclose(log_get());
				//exit(0);
				break;
			default:
				////WriteLog("OP: Unimplemented branch condition %i\n", cc);
			break;
			}
			break;
		}
		case OBJECT_TYPE_STOP:
		{
//op_start_log = 0;
			// unsure
////WriteLog("OP: --> STOP\n");
//			op_set_status_register(((p0>>3) & 0xFFFFFFFF));
//This seems more likely...
			op_set_current_object(p0);
			
			if (p0 & 0x08)
			{
				tom_set_pending_object_int();
				if (tom_irq_enabled(IRQ_OPFLAG))// && jaguar_interrupt_handler_is_valid(64))
					m68k_set_irq(7);				// Cause an NMI to occur...
			}

			return;
//			break;
		}
		default:
			return;
		}

		// Here is a little sanity check to keep the OP from locking up the machine
		// when fed bad data. Better would be to count how many actual cycles it used
		// and bail out/reenter to properly simulate an overloaded OP... !!! FIX !!!
		opCyclesToRun--;
		if (!opCyclesToRun)
			return;
	}
}

//
// Store fixed size bitmap in line buffer
//
void OPProcessFixedBitmap(uint64 p0, uint64 p1, bool render)
{
// Need to make sure that when writing that it stays within the line buffer...
// LBUF ($F01800 - $F01D9E) 360 x 32-bit RAM
	uint8 depth = (p1 >> 12) & 0x07;				// Color depth of image
	int32 xpos = ((int16)((p1 << 4) & 0xFFFF)) >> 4;// Image xpos in LBUF
	uint32 iwidth = (p1 >> 28) & 0x3FF;				// Image width in *phrases*
	uint32 data = (p0 >> 40) & 0xFFFFF8;			// Pixel data address
//#ifdef OP_DEBUG_BMP
	uint32	firstPix = (p1 >> 49) & 0x3F;
	// "The LSB is significant only for scaled objects..." -JTRM
	// "In 1 BPP mode, all five bits are significant. In 2 BPP mode, the top four are significant..."
	firstPix &= 0x3E;
//#endif
// We can ignore the RELEASE (high order) bit for now--probably forever...!
//	uint8 flags = (p1 >> 45) & 0x0F;	// REFLECT, RMW, TRANS, RELEASE
//Optimize: break these out to their own BOOL values
	uint8 flags = (p1 >> 45) & 0x07;				// REFLECT (0), RMW (1), TRANS (2)
	bool flagREFLECT = (flags & OPFLAG_REFLECT ? true : false),
		flagRMW = (flags & OPFLAG_RMW ? true : false),
		flagTRANS = (flags & OPFLAG_TRANS ? true : false);
// "For images with 1 to 4 bits/pixel the top 7 to 4 bits of the index
//  provide the most significant bits of the palette address."
	uint8 index = (p1 >> 37) & 0xFE;				// CLUT index offset (upper pix, 1-4 bpp)
	uint32 pitch = (p1 >> 15) & 0x07;				// Phrase pitch
	pitch <<= 3;									// Optimization: Multiply pitch by 8

//	int16 scanlineWidth = tom_getVideoModeWidth();
	uint8 * tom_ram_8 = tom_get_ram_pointer();
	uint8 * paletteRAM = &tom_ram_8[0x400];
	// This is OK as long as it's used correctly: For 16-bit RAM to RAM direct copies--NOT
	// for use when using endian-corrected data (i.e., any of the *_word_read functions!)
	uint16 * paletteRAM16 = (uint16 *)paletteRAM;

//	//WriteLog("bitmap %ix? %ibpp at %i,? firstpix=? data=0x%.8x pitch %i hflipped=%s dwidth=? (linked to ?) RMW=%s Tranparent=%s\n",
//		iwidth, op_bitmap_bit_depth[bitdepth], xpos, ptr, pitch, (flags&OPFLAG_REFLECT ? "yes" : "no"), (flags&OPFLAG_RMW ? "yes" : "no"), (flags&OPFLAG_TRANS ? "yes" : "no"));

// Is it OK to have a 0 for the data width??? (i.e., undocumented?)
// Seems to be... Seems that dwidth *can* be zero (i.e., reuse same line) as well.
// Pitch == 0 is OK too...
//	if (!render || op_pointer == 0 || ptr == 0 || pitch == 0)
//I'm not convinced that we need to concern ourselves with data & op_pointer here either!
	if (!render || iwidth == 0)
		return;

//#define OP_DEBUG_BMP
//#ifdef OP_DEBUG_BMP
//	//WriteLog("bitmap %ix%i %ibpp at %i,%i firstpix=%i data=0x%.8x pitch %i hflipped=%s dwidth=%i (linked to 0x%.8x) Transluency=%s\n",
//		iwidth, height, op_bitmap_bit_depth[bitdepth], xpos, ypos, firstPix, ptr, pitch, (flags&OPFLAG_REFLECT ? "yes" : "no"), dwidth, op_pointer, (flags&OPFLAG_RMW ? "yes" : "no"));
//#endif

//	int32 leftMargin = xpos, rightMargin = (xpos + (phraseWidthToPixels[depth] * iwidth)) - 1;
	int32 startPos = xpos, endPos = xpos +
		(!flagREFLECT ? (phraseWidthToPixels[depth] * iwidth) - 1
		: -((phraseWidthToPixels[depth] * iwidth) + 1));
	uint32 clippedWidth = 0, phraseClippedWidth = 0, dataClippedWidth = 0;//, phrasePixel = 0;
	bool in24BPPMode = (((GET16(tom_ram_8, 0x0028) >> 1) & 0x03) == 1 ? true : false);	// VMODE
	// Not sure if this is Jaguar Two only location or what...
	// From the docs, it is... If we want to limit here we should think of something else.
//	int32 limit = GET16(tom_ram_8, 0x0008);			// LIMIT
	int32 limit = 720;
	int32 lbufWidth = (!in24BPPMode ? limit - 1 : (limit / 2) - 1);	// Zero based limit...

	// If the image is completely to the left or right of the line buffer, then bail.
//If in REFLECT mode, then these values are swapped! !!! FIX !!! [DONE]
//There are four possibilities:
//  1. image sits on left edge and no REFLECT; starts out of bounds but ends in bounds.
//  2. image sits on left edge and REFLECT; starts in bounds but ends out of bounds.
//  3. image sits on right edge and REFLECT; starts out of bounds but ends in bounds.
//  4. image sits on right edge and no REFLECT; starts in bounds but ends out of bounds.
//Numbers 2 & 4 can be caught by checking the LBUF clip while in the inner loop,
// numbers 1 & 3 are of concern.
// This *indirectly* handles only cases 2 & 4! And is WRONG is REFLECT is set...!
//	if (rightMargin < 0 || leftMargin > lbufWidth)

// It might be easier to swap these (if REFLECTed) and just use XPOS down below...
// That way, you could simply set XPOS to leftMargin if !REFLECT and to rightMargin otherwise.
// Still have to be careful with the DATA and IWIDTH values though...

//	if ((!flagREFLECT && (rightMargin < 0 || leftMargin > lbufWidth))
//		|| (flagREFLECT && (leftMargin < 0 || rightMargin > lbufWidth)))
//		return;
	if ((!flagREFLECT && (endPos < 0 || startPos > lbufWidth))
		|| (flagREFLECT && (startPos < 0 || endPos > lbufWidth)))
		return;

	// Otherwise, find the clip limits and clip the phrase as well...
	// NOTE: I'm fudging here by letting the actual blit overstep the bounds of the
	//       line buffer, but it shouldn't matter since there are two unused line
	//       buffers below and nothing above and I'll at most write 8 bytes outside
	//       the line buffer... I could use a fractional clip begin/end value, but
	//       this makes the blit a *lot* more hairy. I might fix this in the future
	//       if it becomes necessary. (JLH)
	//       Probably wouldn't be *that* hairy. Just use a delta that tells the inner loop
	//       which pixel in the phrase is being written, and quit when either end of phrases
	//       is reached or line buffer extents are surpassed.

//This stuff is probably wrong as well... !!! FIX !!!
//The strange thing is that it seems to work, but that's no guarantee that it's bulletproof!
//Yup. Seems that JagMania doesn't work correctly with this...
//Dunno if this is the problem, but Atari Karts is showing *some* of the road now...
//	if (!flagREFLECT)

/*
	if (leftMargin < 0)
		clippedWidth = 0 - leftMargin,
		phraseClippedWidth = clippedWidth / phraseWidthToPixels[depth],
		leftMargin = 0 - (clippedWidth % phraseWidthToPixels[depth]);
//		leftMargin = 0;

	if (rightMargin > lbufWidth)
		clippedWidth = rightMargin - lbufWidth,
		phraseClippedWidth = clippedWidth / phraseWidthToPixels[depth];//,
//		rightMargin = lbufWidth + (clippedWidth % phraseWidthToPixels[depth]);
//		rightMargin = lbufWidth;
*/
if (depth > 5)
	//WriteLog("OP: We're about to encounter a divide by zero error!\n");
	// NOTE: We're just using endPos to figure out how much, if any, to clip by.
	// ALSO: There may be another case where we start out of bounds and end out of bounds...!
	// !!! FIX !!!
	if (startPos < 0)			// Case #1: Begin out, end in, L to R
		clippedWidth = 0 - startPos,
		dataClippedWidth = phraseClippedWidth = clippedWidth / phraseWidthToPixels[depth],
		startPos = 0 - (clippedWidth % phraseWidthToPixels[depth]);

	if (endPos < 0)				// Case #2: Begin in, end out, R to L
		clippedWidth = 0 - endPos,
		phraseClippedWidth = clippedWidth / phraseWidthToPixels[depth];

	if (endPos > lbufWidth)		// Case #3: Begin in, end out, L to R
		clippedWidth = endPos - lbufWidth,
		phraseClippedWidth = clippedWidth / phraseWidthToPixels[depth];

	if (startPos > lbufWidth)	// Case #4: Begin out, end in, R to L
		clippedWidth = startPos - lbufWidth,
		dataClippedWidth = phraseClippedWidth = clippedWidth / phraseWidthToPixels[depth],
		startPos = lbufWidth + (clippedWidth % phraseWidthToPixels[depth]);

	// If the image is sitting on the line buffer left or right edge, we need to compensate
	// by decreasing the image phrase width accordingly.
	iwidth -= phraseClippedWidth;

	// Also, if we're clipping the phrase we need to make sure we're in the correct part of
	// the pixel data.
//	data += phraseClippedWidth * (pitch << 3);
	data += dataClippedWidth * pitch;

	// NOTE: When the bitmap is in REFLECT mode, the XPOS marks the *right* side of the
	//       bitmap! This makes clipping & etc. MUCH, much easier...!
//	uint32 lbufAddress = 0x1800 + (!in24BPPMode ? leftMargin * 2 : leftMargin * 4);
//Why does this work right when multiplying startPos by 2 (instead of 4) for 24 BPP mode?
//Is this a bug in the OP?
	uint32 lbufAddress = 0x1800 + (!in24BPPMode ? startPos * 2 : startPos * 2);
	uint8 * currentLineBuffer = &tom_ram_8[lbufAddress];

	// Render.

// Hmm. We check above for 24 BPP mode, but don't do anything about it below...
// If we *were* in 24 BPP mode, how would you convert CRY to RGB24? Seems to me
// that if you're in CRY mode then you wouldn't be able to use 24 BPP bitmaps
// anyway.
// This seems to be the case (at least according to the Midsummer docs)...!

	if (depth == 0)									// 1 BPP
	{
		// The LSB of flags is OPFLAG_REFLECT, so sign extend it and or 2 into it.
		int32 lbufDelta = ((int8)((flags << 7) & 0xFF) >> 5) | 0x02;

		// Fetch 1st phrase...
		uint64 pixels = ((uint64)JaguarReadLong(data, OP) << 32) | JaguarReadLong(data + 4, OP);
//Note that firstPix should only be honored *if* we start with the 1st phrase of the bitmap
//i.e., we didn't clip on the margin... !!! FIX !!!
		pixels <<= firstPix;						// Skip first N pixels (N=firstPix)...
		int i = firstPix;							// Start counter at right spot...

		while (iwidth--)
		{
			while (i++ < 64)
			{
				uint8 bit = pixels >> 63;
				if (flagTRANS && bit == 0)
					;	// Do nothing...
				else
				{
					if (!flagRMW)
//Optimize: Set palleteRAM16 to beginning of palette RAM + index*2 and use only [bit] as index...
//Won't optimize RMW case though...
						// This is the *only* correct use of endian-dependent code
						// (i.e., mem-to-mem direct copying)!
						*(uint16 *)currentLineBuffer = paletteRAM16[index | bit];
					else
						*currentLineBuffer = 
							BLEND_CR(*currentLineBuffer, paletteRAM[(index | bit) << 1]),
						*(currentLineBuffer + 1) = 
							BLEND_Y(*(currentLineBuffer + 1), paletteRAM[((index | bit) << 1) + 1]);
				}

				currentLineBuffer += lbufDelta;
				pixels <<= 1;
			}
			i = 0;
			// Fetch next phrase...
			data += pitch;
			pixels = ((uint64)JaguarReadLong(data, OP) << 32) | JaguarReadLong(data + 4, OP);
		}
	}
	else if (depth == 1)							// 2 BPP
	{
		index &= 0xFC;								// Top six bits form CLUT index
		// The LSB is OPFLAG_REFLECT, so sign extend it and or 2 into it.
		int32 lbufDelta = ((int8)((flags << 7) & 0xFF) >> 5) | 0x02;

		while (iwidth--)
		{
			// Fetch phrase...
			uint64 pixels = ((uint64)JaguarReadLong(data, OP) << 32) | JaguarReadLong(data + 4, OP);
			data += pitch;

			for(int i=0; i<32; i++)
			{
				uint8 bits = pixels >> 62;
// Seems to me that both of these are in the same endian, so we could cast it as
// uint16 * and do straight across copies (what about 24 bpp? Treat it differently...)
// This only works for the palettized modes (1 - 8 BPP), since we actually have to
// copy data from memory in 16 BPP mode (or does it? Isn't this the same as the CLUT case?)
// No, it isn't because we read the memory in an endian safe way--this *won't* work...
				if (flagTRANS && bits == 0)
					;	// Do nothing...
				else
				{
					if (!flagRMW)
						*(uint16 *)currentLineBuffer = paletteRAM16[index | bits];
					else
						*currentLineBuffer = 
							BLEND_CR(*currentLineBuffer, paletteRAM[(index | bits) << 1]),
						*(currentLineBuffer + 1) = 
							BLEND_Y(*(currentLineBuffer + 1), paletteRAM[((index | bits) << 1) + 1]);
				}

				currentLineBuffer += lbufDelta;
				pixels <<= 2;
			}
		}
	}
	else if (depth == 2)							// 4 BPP
	{
		index &= 0xF0;								// Top four bits form CLUT index
		// The LSB is OPFLAG_REFLECT, so sign extend it and or 2 into it.
		int32 lbufDelta = ((int8)((flags << 7) & 0xFF) >> 5) | 0x02;

		while (iwidth--)
		{
			// Fetch phrase...
			uint64 pixels = ((uint64)JaguarReadLong(data, OP) << 32) | JaguarReadLong(data + 4, OP);
			data += pitch;

			for(int i=0; i<16; i++)
			{
				uint8 bits = pixels >> 60;
// Seems to me that both of these are in the same endian, so we could cast it as
// uint16 * and do straight across copies (what about 24 bpp? Treat it differently...)
// This only works for the palettized modes (1 - 8 BPP), since we actually have to
// copy data from memory in 16 BPP mode (or does it? Isn't this the same as the CLUT case?)
// No, it isn't because we read the memory in an endian safe way--this *won't* work...
				if (flagTRANS && bits == 0)
					;	// Do nothing...
				else
				{
					if (!flagRMW)
						*(uint16 *)currentLineBuffer = paletteRAM16[index | bits];
					else
						*currentLineBuffer = 
							BLEND_CR(*currentLineBuffer, paletteRAM[(index | bits) << 1]),
						*(currentLineBuffer + 1) = 
							BLEND_Y(*(currentLineBuffer + 1), paletteRAM[((index | bits) << 1) + 1]);
				}

				currentLineBuffer += lbufDelta;
				pixels <<= 4;
			}
		}
	}
	else if (depth == 3)							// 8 BPP
	{
		// The LSB is OPFLAG_REFLECT, so sign extend it and or 2 into it.
		int32 lbufDelta = ((int8)((flags << 7) & 0xFF) >> 5) | 0x02;

		// Fetch 1st phrase...
		uint64 pixels = ((uint64)JaguarReadLong(data, OP) << 32) | JaguarReadLong(data + 4, OP);
//Note that firstPix should only be honored *if* we start with the 1st phrase of the bitmap
//i.e., we didn't clip on the margin... !!! FIX !!!
		firstPix &= 0x30;							// Only top two bits are valid for 8 BPP
		pixels <<= firstPix;						// Skip first N pixels (N=firstPix)...
		int i = firstPix >> 3;						// Start counter at right spot...

		while (iwidth--)
		{
			while (i++ < 8)
			{
				uint8 bits = pixels >> 56;
// Seems to me that both of these are in the same endian, so we could cast it as
// uint16 * and do straight across copies (what about 24 bpp? Treat it differently...)
// This only works for the palettized modes (1 - 8 BPP), since we actually have to
// copy data from memory in 16 BPP mode (or does it? Isn't this the same as the CLUT case?)
// No, it isn't because we read the memory in an endian safe way--this *won't* work...
				if (flagTRANS && bits == 0)
					;	// Do nothing...
				else
				{
					if (!flagRMW)
						*(uint16 *)currentLineBuffer = paletteRAM16[bits];
					else
						*currentLineBuffer = 
							BLEND_CR(*currentLineBuffer, paletteRAM[bits << 1]),
						*(currentLineBuffer + 1) = 
							BLEND_Y(*(currentLineBuffer + 1), paletteRAM[(bits << 1) + 1]);
				}

				currentLineBuffer += lbufDelta;
				pixels <<= 8;
			}
			i = 0;
			// Fetch next phrase...
			data += pitch;
			pixels = ((uint64)JaguarReadLong(data, OP) << 32) | JaguarReadLong(data + 4, OP);
		}
	}
	else if (depth == 4)							// 16 BPP
	{
		// The LSB is OPFLAG_REFLECT, so sign extend it and or 2 into it.
		int32 lbufDelta = ((int8)((flags << 7) & 0xFF) >> 5) | 0x02;

		while (iwidth--)
		{
			// Fetch phrase...
			uint64 pixels = ((uint64)JaguarReadLong(data, OP) << 32) | JaguarReadLong(data + 4, OP);
			data += pitch;

			for(int i=0; i<4; i++)
			{
				uint8 bitsHi = pixels >> 56, bitsLo = pixels >> 48;
// Seems to me that both of these are in the same endian, so we could cast it as
// uint16 * and do straight across copies (what about 24 bpp? Treat it differently...)
// This only works for the palettized modes (1 - 8 BPP), since we actually have to
// copy data from memory in 16 BPP mode (or does it? Isn't this the same as the CLUT case?)
// No, it isn't because we read the memory in an endian safe way--it *won't* work...
				if (flagTRANS && (bitsLo | bitsHi) == 0)
					;	// Do nothing...
				else
				{
					if (!flagRMW)
						*currentLineBuffer = bitsHi,
						*(currentLineBuffer + 1) = bitsLo;
					else
						*currentLineBuffer = 
							BLEND_CR(*currentLineBuffer, bitsHi),
						*(currentLineBuffer + 1) = 
							BLEND_Y(*(currentLineBuffer + 1), bitsLo);
				}

				currentLineBuffer += lbufDelta;
				pixels <<= 16;
			}
		}
	}
	else if (depth == 5)							// 24 BPP
	{
//Looks like Iron Soldier is the only game that uses 24BPP mode...
//There *might* be others...
////WriteLog("OP: Writing 24 BPP bitmap!\n");
		// Not sure, but I think RMW only works with 16 BPP and below, and only in CRY mode...
		// The LSB of flags is OPFLAG_REFLECT, so sign extend it and OR 4 into it.
		int32 lbufDelta = ((int8)((flags << 7) & 0xFF) >> 4) | 0x04;

		while (iwidth--)
		{
			// Fetch phrase...
			uint64 pixels = ((uint64)JaguarReadLong(data, OP) << 32) | JaguarReadLong(data + 4, OP);
			data += pitch;

			for(int i=0; i<2; i++)
			{
				// We don't use a 32-bit var here because of endian issues...!
				uint8 bits3 = pixels >> 56, bits2 = pixels >> 48,
					bits1 = pixels >> 40, bits0 = pixels >> 32;

				if (flagTRANS && (bits3 | bits2 | bits1 | bits0) == 0)
					;	// Do nothing...
				else
					*currentLineBuffer = bits3,
					*(currentLineBuffer + 1) = bits2,
					*(currentLineBuffer + 2) = bits1,
					*(currentLineBuffer + 3) = bits0;

				currentLineBuffer += lbufDelta;
				pixels <<= 32;
			}
		}
	}
}

//
// Store scaled bitmap in line buffer
//
void OPProcessScaledBitmap(uint64 p0, uint64 p1, uint64 p2, bool render)
{
// Need to make sure that when writing that it stays within the line buffer...
// LBUF ($F01800 - $F01D9E) 360 x 32-bit RAM
	uint8 depth = (p1 >> 12) & 0x07;				// Color depth of image
	int32 xpos = ((int16)((p1 << 4) & 0xFFFF)) >> 4;// Image xpos in LBUF
	uint32 iwidth = (p1 >> 28) & 0x3FF;				// Image width in *phrases*
	uint32 data = (p0 >> 40) & 0xFFFFF8;			// Pixel data address
//#ifdef OP_DEBUG_BMP
// Prolly should use this... Though not sure exactly how.
//Use the upper bits as an offset into the phrase depending on the BPP. That's how!
	uint32 firstPix = (p1 >> 49) & 0x3F;
//This is WEIRD! I'm sure I saw Atari Karts request 8 BPP FIRSTPIX! What happened???
//#endif
// We can ignore the RELEASE (high order) bit for now--probably forever...!
//	uint8 flags = (p1 >> 45) & 0x0F;	// REFLECT, RMW, TRANS, RELEASE
//Optimize: break these out to their own BOOL values [DONE]
	uint8 flags = (p1 >> 45) & 0x07;				// REFLECT (0), RMW (1), TRANS (2)
	bool flagREFLECT = (flags & OPFLAG_REFLECT ? true : false),
		flagRMW = (flags & OPFLAG_RMW ? true : false),
		flagTRANS = (flags & OPFLAG_TRANS ? true : false);
	uint8 index = (p1 >> 37) & 0xFE;				// CLUT index offset (upper pix, 1-4 bpp)
	uint32 pitch = (p1 >> 15) & 0x07;				// Phrase pitch

	uint8 * tom_ram_8 = tom_get_ram_pointer();
	uint8 * paletteRAM = &tom_ram_8[0x400];
	// This is OK as long as it's used correctly: For 16-bit RAM to RAM direct copies--NOT
	// for use when using endian-corrected data (i.e., any of the *ReadWord functions!)
	uint16 * paletteRAM16 = (uint16 *)paletteRAM;

	uint8 hscale = p2 & 0xFF;
// Hmm. It seems that fixing the horizontal scale necessitated re-fixing this. Not sure why,
// but seems to be consistent with the vertical scaling now (and it may turn out to be wrong!)...
	uint8 horizontalRemainder = hscale;				// Not sure if it starts full, but seems reasonable [It's not!]
//	uint8 horizontalRemainder = 0;					// Let's try zero! Seems to work! Yay! [No, it doesn't!]
	int32 scaledWidthInPixels = (iwidth * phraseWidthToPixels[depth] * hscale) >> 5;
	uint32 scaledPhrasePixels = (phraseWidthToPixels[depth] * hscale) >> 5;

//	//WriteLog("bitmap %ix? %ibpp at %i,? firstpix=? data=0x%.8x pitch %i hflipped=%s dwidth=? (linked to ?) RMW=%s Tranparent=%s\n",
//		iwidth, op_bitmap_bit_depth[bitdepth], xpos, ptr, pitch, (flags&OPFLAG_REFLECT ? "yes" : "no"), (flags&OPFLAG_RMW ? "yes" : "no"), (flags&OPFLAG_TRANS ? "yes" : "no"));

// Looks like an hscale of zero means don't draw!
	if (!render || iwidth == 0 || hscale == 0)
		return;

/*extern int start_logging;
if (start_logging)
	//WriteLog("OP: Scaled bitmap %ix? %ibpp at %i,? hscale=%02X fpix=%i data=%08X pitch %i hflipped=%s dwidth=? (linked to %08X) Transluency=%s\n",
		iwidth, op_bitmap_bit_depth[depth], xpos, hscale, firstPix, data, pitch, (flagREFLECT ? "yes" : "no"), op_pointer, (flagRMW ? "yes" : "no"));*/
//#define OP_DEBUG_BMP
//#ifdef OP_DEBUG_BMP
//	//WriteLog("OP: Scaled bitmap %ix%i %ibpp at %i,%i firstpix=%i data=0x%.8x pitch %i hflipped=%s dwidth=%i (linked to 0x%.8x) Transluency=%s\n",
//		iwidth, height, op_bitmap_bit_depth[bitdepth], xpos, ypos, firstPix, ptr, pitch, (flags&OPFLAG_REFLECT ? "yes" : "no"), dwidth, op_pointer, (flags&OPFLAG_RMW ? "yes" : "no"));
//#endif

	int32 startPos = xpos, endPos = xpos +
		(!flagREFLECT ? scaledWidthInPixels - 1 : -(scaledWidthInPixels + 1));
	uint32 clippedWidth = 0, phraseClippedWidth = 0, dataClippedWidth = 0;
	bool in24BPPMode = (((GET16(tom_ram_8, 0x0028) >> 1) & 0x03) == 1 ? true : false);	// VMODE
	// Not sure if this is Jaguar Two only location or what...
	// From the docs, it is... If we want to limit here we should think of something else.
//	int32 limit = GET16(tom_ram_8, 0x0008);			// LIMIT
	int32 limit = 720;
	int32 lbufWidth = (!in24BPPMode ? limit - 1 : (limit / 2) - 1);	// Zero based limit...

	// If the image is completely to the left or right of the line buffer, then bail.
//If in REFLECT mode, then these values are swapped! !!! FIX !!! [DONE]
//There are four possibilities:
//  1. image sits on left edge and no REFLECT; starts out of bounds but ends in bounds.
//  2. image sits on left edge and REFLECT; starts in bounds but ends out of bounds.
//  3. image sits on right edge and REFLECT; starts out of bounds but ends in bounds.
//  4. image sits on right edge and no REFLECT; starts in bounds but ends out of bounds.
//Numbers 2 & 4 can be caught by checking the LBUF clip while in the inner loop,
// numbers 1 & 3 are of concern.
// This *indirectly* handles only cases 2 & 4! And is WRONG if REFLECT is set...!
//	if (rightMargin < 0 || leftMargin > lbufWidth)

// It might be easier to swap these (if REFLECTed) and just use XPOS down below...
// That way, you could simply set XPOS to leftMargin if !REFLECT and to rightMargin otherwise.
// Still have to be careful with the DATA and IWIDTH values though...

	if ((!flagREFLECT && (endPos < 0 || startPos > lbufWidth))
		|| (flagREFLECT && (startPos < 0 || endPos > lbufWidth)))
		return;

	// Otherwise, find the clip limits and clip the phrase as well...
	// NOTE: I'm fudging here by letting the actual blit overstep the bounds of the
	//       line buffer, but it shouldn't matter since there are two unused line
	//       buffers below and nothing above and I'll at most write 40 bytes outside
	//       the line buffer... I could use a fractional clip begin/end value, but
	//       this makes the blit a *lot* more hairy. I might fix this in the future
	//       if it becomes necessary. (JLH)
	//       Probably wouldn't be *that* hairy. Just use a delta that tells the inner loop
	//       which pixel in the phrase is being written, and quit when either end of phrases
	//       is reached or line buffer extents are surpassed.

//This stuff is probably wrong as well... !!! FIX !!!
//The strange thing is that it seems to work, but that's no guarantee that it's bulletproof!
//Yup. Seems that JagMania doesn't work correctly with this...
//Dunno if this is the problem, but Atari Karts is showing *some* of the road now...
//Actually, it is! Or, it was. It doesn't seem to be clipping here, so the problem lies
//elsewhere! Hmm. Putting the scaling code into the 1/2/8 BPP cases seems to draw the ground
// a bit more accurately... Strange!
//It's probably a case of the REFLECT flag being set and the background being written
//from the right side of the screen...
//But no, it isn't... At least if the diagnostics are telling the truth!

	// NOTE: We're just using endPos to figure out how much, if any, to clip by.
	// ALSO: There may be another case where we start out of bounds and end out of bounds...!
	// !!! FIX !!!

//There's a problem here with scaledPhrasePixels in that it can be forced to zero when
//the scaling factor is small. So fix it already! !!! FIX !!!
/*if (scaledPhrasePixels == 0)
{
	//WriteLog("OP: [Scaled] We're about to encounter a divide by zero error!\n");
	DumpScaledObject(p0, p1, p2);
}//*/
//NOTE: I'm almost 100% sure that this is wrong... And it is! :-p

//Try a simple example...
// Let's say we have a 8 BPP scanline with an hscale of $80 (4). Our xpos is -10,
// non-flipped. Pixels in the bitmap are XYZXYZXYZXYZXYZ.
// Scaled up, they would be XXXXYYYYZZZZXXXXYYYYZZZZXXXXYYYYZZZZ...
//
// Normally, we would expect this in the line buffer:
// ZZXXXXYYYYZZZZXXXXYYYYZZZZ...
//
// But instead we're getting:
// XXXXYYYYZZZZXXXXYYYYZZZZ...
//
// or are we??? It would seem so, simply by virtue of the fact that we're NOT starting
// on negative boundary--or are we? Hmm...
// cw = 10, dcw = pcw = 10 / ([8 * 4 = 32] 32) = 0, sp = -10
//
// Let's try a real world example:
//
//OP: Scaled bitmap (70, 8 BPP, spp=28) sp (-400) < 0... [new sp=-8, cw=400, dcw=pcw=14]
//OP: Scaled bitmap (6F, 8 BPP, spp=27) sp (-395) < 0... [new sp=-17, cw=395, dcw=pcw=14]
//
// Really, spp is 27.75 in the second case...
// So... If we do 395 / 27.75, we get 14. Ok so far... If we scale that against the
// start position (14 * 27.75), we get -6.5... NOT -17!

//Now it seems we're working OK, at least for the first case...
uint32 scaledPhrasePixelsUS = phraseWidthToPixels[depth] * hscale;

	if (startPos < 0)			// Case #1: Begin out, end in, L to R
{
//		clippedWidth = 0 - startPos,
		clippedWidth = (0 - startPos) << 5,
//		dataClippedWidth = phraseClippedWidth = clippedWidth / scaledPhrasePixels,
		dataClippedWidth = phraseClippedWidth = (clippedWidth / scaledPhrasePixelsUS) >> 5,
//		startPos = 0 - (clippedWidth % scaledPhrasePixels);
		startPos += (dataClippedWidth * scaledPhrasePixelsUS) >> 5;
}

	if (endPos < 0)				// Case #2: Begin in, end out, R to L
		clippedWidth = 0 - endPos,
		phraseClippedWidth = clippedWidth / scaledPhrasePixels;

	if (endPos > lbufWidth)		// Case #3: Begin in, end out, L to R
		clippedWidth = endPos - lbufWidth,
		phraseClippedWidth = clippedWidth / scaledPhrasePixels;

	if (startPos > lbufWidth)	// Case #4: Begin out, end in, R to L
		clippedWidth = startPos - lbufWidth,
		dataClippedWidth = phraseClippedWidth = clippedWidth / scaledPhrasePixels,
		startPos = lbufWidth + (clippedWidth % scaledPhrasePixels);

	// If the image is sitting on the line buffer left or right edge, we need to compensate
	// by decreasing the image phrase width accordingly.
	iwidth -= phraseClippedWidth;

	// Also, if we're clipping the phrase we need to make sure we're in the correct part of
	// the pixel data.
//	data += phraseClippedWidth * (pitch << 3);
	data += dataClippedWidth * (pitch << 3);

	// NOTE: When the bitmap is in REFLECT mode, the XPOS marks the *right* side of the
	//       bitmap! This makes clipping & etc. MUCH, much easier...!
//	uint32 lbufAddress = 0x1800 + (!in24BPPMode ? leftMargin * 2 : leftMargin * 4);
//	uint32 lbufAddress = 0x1800 + (!in24BPPMode ? startPos * 2 : startPos * 4);
	uint32 lbufAddress = 0x1800 + startPos * 2;
	uint8 * currentLineBuffer = &tom_ram_8[lbufAddress];
//uint8 * lineBufferLowerLimit = &tom_ram_8[0x1800],
//	* lineBufferUpperLimit = &tom_ram_8[0x1800 + 719];

	// Render.

// Hmm. We check above for 24 BPP mode, but don't do anything about it below...
// If we *were* in 24 BPP mode, how would you convert CRY to RGB24? Seems to me
// that if you're in CRY mode then you wouldn't be able to use 24 BPP bitmaps
// anyway.
// This seems to be the case (at least according to the Midsummer docs)...!

	if (depth == 0)									// 1 BPP
	{
		// The LSB of flags is OPFLAG_REFLECT, so sign extend it and or 2 into it.
		int32 lbufDelta = ((int8)((flags << 7) & 0xFF) >> 5) | 0x02;

		int pixCount = 0;
		uint64 pixels = ((uint64)JaguarReadLong(data, OP) << 32) | JaguarReadLong(data + 4, OP);

		while ((int32)iwidth > 0)
		{
			uint8 bits = pixels >> 63;

			if (flagTRANS && bits == 0)
				;	// Do nothing...
			else
			{
				if (!flagRMW)
					// This is the *only* correct use of endian-dependent code
					// (i.e., mem-to-mem direct copying)!
					*(uint16 *)currentLineBuffer = paletteRAM16[index | bits];
				else
					*currentLineBuffer = 
						BLEND_CR(*currentLineBuffer, paletteRAM[(index | bits) << 1]),
					*(currentLineBuffer + 1) = 
						BLEND_Y(*(currentLineBuffer + 1), paletteRAM[((index | bits) << 1) + 1]);
			}

			currentLineBuffer += lbufDelta;

/*			horizontalRemainder -= 0x20;		// Subtract 1.0f in [3.5] fixed point format
			while (horizontalRemainder & 0x80)
			{
				horizontalRemainder += hscale;
				pixCount++;
				pixels <<= 1;
			}//*/
			while (horizontalRemainder <= 0x20)		// I.e., it's <= 0 (*before* subtraction)
			{
				horizontalRemainder += hscale;
				pixCount++;
				pixels <<= 1;
			}
			horizontalRemainder -= 0x20;		// Subtract 1.0f in [3.5] fixed point format

			if (pixCount > 63)
			{
				int phrasesToSkip = pixCount / 64, pixelShift = pixCount % 64;

				data += (pitch << 3) * phrasesToSkip;
				pixels = ((uint64)JaguarReadLong(data, OP) << 32) | JaguarReadLong(data + 4, OP);
				pixels <<= 1 * pixelShift;
				iwidth -= phrasesToSkip;
				pixCount = pixelShift;
			}
		}
	}
	else if (depth == 1)							// 2 BPP
	{
		index &= 0xFC;								// Top six bits form CLUT index
		// The LSB is OPFLAG_REFLECT, so sign extend it and or 2 into it.
		int32 lbufDelta = ((int8)((flags << 7) & 0xFF) >> 5) | 0x02;

		int pixCount = 0;
		uint64 pixels = ((uint64)JaguarReadLong(data, OP) << 32) | JaguarReadLong(data + 4, OP);

		while ((int32)iwidth > 0)
		{
			uint8 bits = pixels >> 62;

			if (flagTRANS && bits == 0)
				;	// Do nothing...
			else
			{
				if (!flagRMW)
					// This is the *only* correct use of endian-dependent code
					// (i.e., mem-to-mem direct copying)!
					*(uint16 *)currentLineBuffer = paletteRAM16[index | bits];
				else
					*currentLineBuffer = 
						BLEND_CR(*currentLineBuffer, paletteRAM[(index | bits) << 1]),
					*(currentLineBuffer + 1) = 
						BLEND_Y(*(currentLineBuffer + 1), paletteRAM[((index | bits) << 1) + 1]);
			}

			currentLineBuffer += lbufDelta;

/*			horizontalRemainder -= 0x20;		// Subtract 1.0f in [3.5] fixed point format
			while (horizontalRemainder & 0x80)
			{
				horizontalRemainder += hscale;
				pixCount++;
				pixels <<= 2;
			}//*/
			while (horizontalRemainder <= 0x20)		// I.e., it's <= 0 (*before* subtraction)
			{
				horizontalRemainder += hscale;
				pixCount++;
				pixels <<= 2;
			}
			horizontalRemainder -= 0x20;		// Subtract 1.0f in [3.5] fixed point format

			if (pixCount > 31)
			{
				int phrasesToSkip = pixCount / 32, pixelShift = pixCount % 32;

				data += (pitch << 3) * phrasesToSkip;
				pixels = ((uint64)JaguarReadLong(data, OP) << 32) | JaguarReadLong(data + 4, OP);
				pixels <<= 2 * pixelShift;
				iwidth -= phrasesToSkip;
				pixCount = pixelShift;
			}
		}
	}
	else if (depth == 2)							// 4 BPP
	{
		index &= 0xF0;								// Top four bits form CLUT index
		// The LSB is OPFLAG_REFLECT, so sign extend it and or 2 into it.
		int32 lbufDelta = ((int8)((flags << 7) & 0xFF) >> 5) | 0x02;

		int pixCount = 0;
		uint64 pixels = ((uint64)JaguarReadLong(data, OP) << 32) | JaguarReadLong(data + 4, OP);

		while ((int32)iwidth > 0)
		{
			uint8 bits = pixels >> 60;

			if (flagTRANS && bits == 0)
				;	// Do nothing...
			else
			{
				if (!flagRMW)
					// This is the *only* correct use of endian-dependent code
					// (i.e., mem-to-mem direct copying)!
					*(uint16 *)currentLineBuffer = paletteRAM16[index | bits];
				else
					*currentLineBuffer = 
						BLEND_CR(*currentLineBuffer, paletteRAM[(index | bits) << 1]),
					*(currentLineBuffer + 1) = 
						BLEND_Y(*(currentLineBuffer + 1), paletteRAM[((index | bits) << 1) + 1]);
			}

			currentLineBuffer += lbufDelta;

/*			horizontalRemainder -= 0x20;		// Subtract 1.0f in [3.5] fixed point format
			while (horizontalRemainder & 0x80)
			{
				horizontalRemainder += hscale;
				pixCount++;
				pixels <<= 4;
			}//*/
			while (horizontalRemainder <= 0x20)		// I.e., it's <= 0 (*before* subtraction)
			{
				horizontalRemainder += hscale;
				pixCount++;
				pixels <<= 4;
			}
			horizontalRemainder -= 0x20;		// Subtract 1.0f in [3.5] fixed point format

			if (pixCount > 15)
			{
				int phrasesToSkip = pixCount / 16, pixelShift = pixCount % 16;

				data += (pitch << 3) * phrasesToSkip;
				pixels = ((uint64)JaguarReadLong(data, OP) << 32) | JaguarReadLong(data + 4, OP);
				pixels <<= 4 * pixelShift;
				iwidth -= phrasesToSkip;
				pixCount = pixelShift;
			}
		}
	}
	else if (depth == 3)							// 8 BPP
	{
		// The LSB is OPFLAG_REFLECT, so sign extend it and or 2 into it.
		int32 lbufDelta = ((int8)((flags << 7) & 0xFF) >> 5) | 0x02;

		int pixCount = 0;
		uint64 pixels = ((uint64)JaguarReadLong(data, OP) << 32) | JaguarReadLong(data + 4, OP);

		while ((int32)iwidth > 0)
		{
			uint8 bits = pixels >> 56;

			if (flagTRANS && bits == 0)
				;	// Do nothing...
			else
			{
				if (!flagRMW)
					// This is the *only* correct use of endian-dependent code
					// (i.e., mem-to-mem direct copying)!
					*(uint16 *)currentLineBuffer = paletteRAM16[bits];
/*				{
					if (currentLineBuffer >= lineBufferLowerLimit && currentLineBuffer <= lineBufferUpperLimit)
						*(uint16 *)currentLineBuffer = paletteRAM16[bits];
				}*/
				else
					*currentLineBuffer = 
						BLEND_CR(*currentLineBuffer, paletteRAM[bits << 1]),
					*(currentLineBuffer + 1) = 
						BLEND_Y(*(currentLineBuffer + 1), paletteRAM[(bits << 1) + 1]);
			}

			currentLineBuffer += lbufDelta;

			while (horizontalRemainder <= 0x20)		// I.e., it's <= 0 (*before* subtraction)
			{
				horizontalRemainder += hscale;
				pixCount++;
				pixels <<= 8;
			}
			horizontalRemainder -= 0x20;		// Subtract 1.0f in [3.5] fixed point format

			if (pixCount > 7)
			{
				int phrasesToSkip = pixCount / 8, pixelShift = pixCount % 8;

				data += (pitch << 3) * phrasesToSkip;
				pixels = ((uint64)JaguarReadLong(data, OP) << 32) | JaguarReadLong(data + 4, OP);
				pixels <<= 8 * pixelShift;
				iwidth -= phrasesToSkip;
				pixCount = pixelShift;
			}
		}
	}
	else if (depth == 4)							// 16 BPP
	{
		// The LSB is OPFLAG_REFLECT, so sign extend it and OR 2 into it.
		int32 lbufDelta = ((int8)((flags << 7) & 0xFF) >> 5) | 0x02;

		int pixCount = 0;
		uint64 pixels = ((uint64)JaguarReadLong(data, OP) << 32) | JaguarReadLong(data + 4, OP);

		while ((int32)iwidth > 0)
		{
			uint8 bitsHi = pixels >> 56, bitsLo = pixels >> 48;

			if (flagTRANS && (bitsLo | bitsHi) == 0)
				;	// Do nothing...
			else
			{
				if (!flagRMW)
					*currentLineBuffer = bitsHi,
					*(currentLineBuffer + 1) = bitsLo;
				else
					*currentLineBuffer = 
						BLEND_CR(*currentLineBuffer, bitsHi),
					*(currentLineBuffer + 1) = 
						BLEND_Y(*(currentLineBuffer + 1), bitsLo);
			}

			currentLineBuffer += lbufDelta;

/*			horizontalRemainder -= 0x20;		// Subtract 1.0f in [3.5] fixed point format
			while (horizontalRemainder & 0x80)
			{
				horizontalRemainder += hscale;
				pixCount++;
				pixels <<= 16;
			}//*/
			while (horizontalRemainder <= 0x20)		// I.e., it's <= 0 (*before* subtraction)
			{
				horizontalRemainder += hscale;
				pixCount++;
				pixels <<= 16;
			}
			horizontalRemainder -= 0x20;		// Subtract 1.0f in [3.5] fixed point format
//*/
			if (pixCount > 3)
			{
				int phrasesToSkip = pixCount / 4, pixelShift = pixCount % 4;

				data += (pitch << 3) * phrasesToSkip;
				pixels = ((uint64)JaguarReadLong(data, OP) << 32) | JaguarReadLong(data + 4, OP);
				pixels <<= 16 * pixelShift;

				iwidth -= phrasesToSkip;

				pixCount = pixelShift;
			}
		}
	}
	else if (depth == 5)							// 24 BPP
	{
		// Not sure, but I think RMW only works with 16 BPP and below, and only in CRY mode...
		// The LSB is OPFLAG_REFLECT, so sign extend it and or 4 into it.
		int32 lbufDelta = ((int8)((flags << 7) & 0xFF) >> 4) | 0x04;

		while (iwidth--)
		{
			// Fetch phrase...
			uint64 pixels = ((uint64)JaguarReadLong(data, OP) << 32) | JaguarReadLong(data + 4, OP);
			data += pitch << 3;						// Multiply pitch * 8 (optimize: precompute this value)

			for(int i=0; i<2; i++)
			{
				uint8 bits3 = pixels >> 56, bits2 = pixels >> 48,
					bits1 = pixels >> 40, bits0 = pixels >> 32;

				if (flagTRANS && (bits3 | bits2 | bits1 | bits0) == 0)
					;	// Do nothing...
				else
					*currentLineBuffer = bits3,
					*(currentLineBuffer + 1) = bits2,
					*(currentLineBuffer + 2) = bits1,
					*(currentLineBuffer + 3) = bits0;

				currentLineBuffer += lbufDelta;
				pixels <<= 32;
			}
		}
	}
}
