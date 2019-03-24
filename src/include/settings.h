//
// SETTINGS.H: Header file
//

#ifndef __SETTINGS_H__
#define __SETTINGS_H__

// MAX_PATH isn't defined in stdlib.h on *nix, so we do it here...
#ifdef __GCCUNIX__
#include <limits.h>
#define MAX_PATH		_POSIX_PATH_MAX
#endif
#include "types.h"

// Settings struct

struct VJSettings
{
	uint8_t useJoystick;
	int32_t joyport;									// Joystick port
	uint8_t hardwareTypeNTSC;							// Set to false for PAL
	uint8_t useJaguarBIOS;
	uint8_t DSPEnabled;
	uint8_t usePipelinedDSP;
	uint8_t fullscreen;
	uint8_t hardwareTypeAlpine;

	// Keybindings in order of U, D, L, R, C, B, A, Op, Pa, 0-9, #, *
	uint16_t p1KeyBindings[21];
	uint16_t p2KeyBindings[21];

	// Paths
	char ROMPath[MAX_PATH];
	char jagBootPath[MAX_PATH];
	char EEPROMPath[MAX_PATH];

	// Internal global stuff
//	uint32_t ROMType;
};

// ROM Types
//enum { RT_CARTRIDGE, RT_

// Exported functions

void LoadVJSettings(void);
void SaveVJSettings(void);

// Exported variables

extern struct VJSettings vjs;

#endif	// __SETTINGS_H__
