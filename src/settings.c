//
// SETTINGS.CPP: Virtual Jaguar configuration loading/saving support
//
// by James L. Hammons
//

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL/SDL.h>
#include "sdlemu_config.h"
#include "settings.h"

// Global variables

struct VJSettings vjs;

// Private function prototypes

void CheckForTrailingSlash(char * path);

//
// Load Virtual Jaguar's settings
//
void LoadVJSettings(void)
{
	/*if (sdlemu_init_config("./vj.cfg") == 0			// CWD
		&& sdlemu_init_config("~/vj.cfg") == 0		// Home
		&& sdlemu_init_config("~/.vj/vj.cfg") == 0	// Home under .vj directory
		&& sdlemu_init_config("vj.cfg") == 0)		// Somewhere in the path
		WriteLog("Settings: Couldn't find VJ configuration file. Using defaults...\n");
	*/
	vjs.useJoystick = false;
	vjs.joyport = 0;
	vjs.hardwareTypeNTSC = true;
	vjs.useJaguarBIOS = false;
	#ifdef DSP_EMU
	if (vjs.DSPEnabled != true || vjs.DSPEnabled != false) vjs.DSPEnabled = true;
	#else
	if (vjs.DSPEnabled != true || vjs.DSPEnabled != false) vjs.DSPEnabled = false;
	#endif
	vjs.usePipelinedDSP = false;
	vjs.fullscreen = false;

	vjs.p1KeyBindings[0] = SDLK_UP;
	vjs.p1KeyBindings[1] = SDLK_DOWN;
	vjs.p1KeyBindings[2] = SDLK_LEFT;
	vjs.p1KeyBindings[3] = SDLK_RIGHT;
	vjs.p1KeyBindings[4] = SDLK_LCTRL;
	vjs.p1KeyBindings[5] = SDLK_LALT;
	vjs.p1KeyBindings[6] = SDLK_LSHIFT;
	vjs.p1KeyBindings[7] = SDLK_ESCAPE;
	vjs.p1KeyBindings[8] = SDLK_RETURN;
	vjs.p1KeyBindings[9] =  SDLK_SPACE;
	vjs.p1KeyBindings[10] = SDLK_TAB;
	vjs.p1KeyBindings[11] = SDLK_BACKSPACE;
	vjs.p1KeyBindings[12] = SDLK_3;
	vjs.p1KeyBindings[13] = SDLK_4;
	vjs.p1KeyBindings[14] = SDLK_5;
	vjs.p1KeyBindings[15] = SDLK_6;
	vjs.p1KeyBindings[16] = SDLK_7;
	vjs.p1KeyBindings[17] = SDLK_8;
	vjs.p1KeyBindings[18] = SDLK_9;
	vjs.p1KeyBindings[19] = SDLK_KP_DIVIDE;
	vjs.p1KeyBindings[20] = SDLK_KP_MULTIPLY;

	strcpy(vjs.jagBootPath, "./BIOS/jagboot.rom");
	strcpy(vjs.EEPROMPath, "./EEPROMs");
	strcpy(vjs.ROMPath, "./ROMs");
	CheckForTrailingSlash(vjs.EEPROMPath);
	CheckForTrailingSlash(vjs.ROMPath);

	vjs.hardwareTypeAlpine = false;	// No external setting for this yet...
}

//
// Save Virtual Jaguar's settings
//
void SaveVJSettings(void)
{
}

//
// Check path for a trailing slash, and append if not present
//
void CheckForTrailingSlash(char * path)
{
	if (strlen(path) > 0)
		if (path[strlen(path) - 1] != '/')
			strcat(path, "/");	// NOTE: Possible buffer overflow
}
