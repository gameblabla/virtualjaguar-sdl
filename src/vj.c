//
// Virtual Jaguar Emulator
//
// Original codebase by David Raingeard (Cal2)
// GCC/SDL port by Niels Wagenaar (Linux/WIN32) and Caz (BeOS)
// Cleanups/fixes/enhancements by James L. Hammons and Adam Green
//

#ifdef __GCCUNIX__
#include <unistd.h>									// Is this necessary anymore?
#endif

#include <time.h>
#include <SDL/SDL.h>
#include "jaguar.h"
#include "video.h"
#include "gui.h"
#include "settings.h"								// Pull in "vjs" struct

// Uncomment this for speed control (?)
//#define SPEED_CONTROL

// Private function prototypes

// External variables

extern uint8_t * jaguar_mainRam;
extern uint8_t * jaguar_mainRom;
extern uint8_t * jaguar_bootRom;

// Global variables (export capable)
//should these even be here anymore?

uint8_t finished = false;
uint8_t BIOSLoaded = false;

//
// The main emulator loop (what else?)
//

//temp, so we can grab this from elsewhere.
uint32_t totalFrames;

int main(int argc, char * argv[])
{
	int32_t nFrameskip = 0;							// Default: Show every frame

	printf("Virtual Jaguar GCC/SDL Portable Jaguar Emulator v1.0.7\n");
	printf("Based upon Virtual Jaguar core v1.0.0 by David Raingeard.\n");
	printf("Written by Niels Wagenaar (Linux/WIN32), Carwin Jones (BeOS),\n");
	printf("James L. Hammons (WIN32) and Adam Green (MacOS)\n");
	printf("Contact: http://sdlemu.ngemu.com/ | sdlemu@ngemu.com\n");

	LoadVJSettings();								// Get config file settings...

	// Check the switches... ;-)
	// NOTE: Command line switches can override any config file settings, thus the
	//       proliferation of the noXXX switches. ;-)

	for(int i=1; i<argc || argv[i]!=NULL; i++)
	{
		if (!strcmp(argv[i], "-joystick")) 
			vjs.useJoystick = true;

		if (!strcmp(argv[i], "-joyport"))
		{
			vjs.joyport = atoi(argv[++i]) + 1;
			if (vjs.joyport > 3)
				vjs.joyport = 3;
		}

		if (!strcmp(argv[i], "-frameskip"))
		{
			nFrameskip = atoi(argv[++i]) + 1;
			if (nFrameskip > 10)
				nFrameskip = 10;
#ifdef SPEED_CONTROL
			nFrameskip = 0;
#endif
		}

		if (!strcmp(argv[i], "-bios"))
			vjs.useJaguarBIOS = true;

		if (!strcmp(argv[i], "-nobios"))
			vjs.useJaguarBIOS = false;

		if (!strcmp(argv[i], "-dsp"))
			vjs.DSPEnabled = true;

		if (!strcmp(argv[i], "-nodsp"))
			vjs.DSPEnabled = false;

		if (!strcmp(argv[i], "-pipeline"))
			vjs.usePipelinedDSP = true;

		if (!strcmp(argv[i], "-nopipeline"))
			vjs.usePipelinedDSP = false;

		if (!strcmp(argv[i], "-fullscreen")) 
			vjs.fullscreen = true;

		if (!strcmp(argv[i], "-window")) 
			vjs.fullscreen = false;

		if (!strcmp(argv[i], "-pal"))
			vjs.hardwareTypeNTSC = false;

		if (!strcmp(argv[i], "-ntsc"))
			vjs.hardwareTypeNTSC = true;

		if (!strcmp(argv[i], "-help") || !strcmp(argv[i], "-?"))
		{
		    printf("Usage: \n\n");
			printf("vj [romfile] [switches]\n");
			printf("  -? or -help     : Display usage and switches                \n");
			printf("  -frameskip 1-10 : Enable frameskip 1 - 10 (default: none)   \n");
			printf("  -joystick       : Enable joystick/gamepad                   \n");
			printf("  -joyport 0-3    : Select desired joystick port              \n");
			printf("  -bios           : Boot cart using Jaguar BIOS ROM           \n");
			printf("  -nobios         : Boot cart without using Jaguar BIOS ROM   \n");
			printf("  -dsp            : Force VJ to use the DSP                   \n");
			printf("  -nodsp          : Force VJ to run without the DSP           \n");
			printf("  -pipeline       : Use the DSP pipelined core                \n");
			printf("  -nopipeline     : Use the DSP non-pipelined core            \n");
			printf("  -fullscreen     : Enable fullscreen mode (default: windowed)\n");
			printf("  -window         : Enable windowed mode                      \n");
			printf("  -pal            : Force VJ to PAL mode (default: NTSC)      \n");
			printf("  -ntsc           : Force VJ to NTSC mode                     \n");
			printf("\nInvoking Virtual Jagaur with no ROM file will cause it to boot up\n");
			printf("with the VJ GUI.\n");
 			return 1;
		}
    }

	// Set up SDL library
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK | SDL_INIT_AUDIO | SDL_INIT_TIMER) < 0)
	{
		return -1;
	}
	
	InitMemory();
	InitVersion();
	jaguar_init();

	// Get the BIOS ROM
	//	if (vjs.useJaguarBIOS)
	// What would be nice here would be a way to check if the BIOS was loaded so that we
	// could disable the pushbutton on the Misc Options menu... !!! FIX !!! [DONE here, but needs to be fixed in GUI as well!]
	BIOSLoaded = (JaguarLoadROM(jaguar_bootRom, vjs.jagBootPath) == 0x20000 ? true : false);

	SET32(jaguar_mainRam, 0, 0x00200000);			// Set top of stack...

	InitVideo();
	InitGUI();

	GUIMain(argv[1]);

	jaguar_done();
	VersionDone();
	MemoryDone();
	VideoDone();

	// Free SDL components last...!
	SDL_QuitSubSystem(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK | SDL_INIT_AUDIO | SDL_INIT_TIMER);
	SDL_Quit();

    return 0;
}
