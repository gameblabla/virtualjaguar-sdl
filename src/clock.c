//
// Clock handler
//
// by cal2
// GCC/SDL port by Niels Wagenaar (Linux/WIN32) and Caz (BeOS)
// Cleanups by James L. Hammons
//

#include "jaguar.h"


void clock_init(void)
{
	clock_reset();
}

void clock_reset(void)
{
}

void clock_done(void)
{
}

void clock_byte_write(uint32_t offset, uint8_t data)
{
}

void clock_word_write(uint32_t offset, uint16_t data)
{
}

uint8_t clock_byte_read(uint32_t offset)
{
	return 0xFF;
}

uint16_t clock_word_read(uint32_t offset)
{
	return 0xFFFF;
}
