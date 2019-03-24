//
// GCC/SDL port by Niels Wagenaar (Linux/WIN32) and Caz (BeOS)
// Removal of unsafe macros and addition of typdefs by James L. Hammons
//

#ifndef __TYPES_H__
#define __TYPES_H__

#include <stdint.h>

#define false 0
#define true 1

// Read/write tracing enumeration

enum { UNKNOWN, JAGUAR, DSP, GPU, TOM, JERRY, M68K, BLITTER, OP };

#endif	// __TYPES_H__
