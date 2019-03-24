//
// GUI.CPP
//
// Graphical User Interface support
// by James L. Hammons
//

#include <stdarg.h>
#include <sys/types.h>								// For MacOS <dirent.h> dependency
#include <dirent.h>
#include <SDL.h>
#include <string.h>
#include <ctype.h>									// For toupper()
#include "settings.h"
#include "tom.h"
#include "video.h"
#include "crc32.h"
#include "zlib.h"
#include "unzip.h"
#include "gui.h"

void LoadROM(void);
void ResetJaguar(void);
void ResetJaguarCD(void);
void RunEmu(void);
void Quit(void);
void About(void);
void MiscOptions(void);

int gzfilelength(gzFile gd);

// External variables

extern uint8_t * jaguar_mainRam;
extern uint8_t * jaguar_mainRom;
extern uint8_t * jaguar_bootRom;
extern uint8_t * jaguar_CDBootROM;
extern uint8_t BIOSLoaded;
extern uint8_t CDBIOSLoaded;

// Local global variables

uint8_t exitGUI = false;								// GUI (emulator) done variable
int mouseX, mouseY;

//
// GUI stuff--it's not crunchy, it's GUI! ;-)
//

void InitGUI(void)
{
	SDL_ShowCursor(SDL_DISABLE);
}

void GUIDone(void)
{
}

//
// GUI main loop
//
//uint8_t GUIMain(void)
uint8_t GUIMain(char * filename)
{
	if (filename)
	{
		if (JaguarLoadFile(filename))
		{
			jaguar_reset();
			RunEmu();
		}
	}

	return true;
}

void RunEmu(void)
{
	extern int16_t * backbuffer;
	extern uint8_t finished;
	uint32_t nFrame = 0, nFrameskip = 6;
	finished = false;

	while (true)
	{
		JaguarExecute(backbuffer, true);

		joystick_exec();

		if (finished)
			break;

		// Simple frameskip
		if (nFrame == nFrameskip)
		{
			RenderBackbuffer();
			nFrame = 0;
		}
		else
			nFrame++;
	}
}


//
// Generic ROM loading
//
uint32_t JaguarLoadROM(uint8_t * rom, char * path)
{
	// We really should have some kind of sanity checking for the ROM size here to prevent
	// a buffer overflow... !!! FIX !!!
	uint32_t romSize = 0;

	char * ext = strrchr(path, '.');
	if (ext != NULL)
	{
		//WriteLog("VJ: Loading \"%s\"...", path);

		if (strcasecmp(ext, ".zip") == 0)
		{
			// Handle ZIP file loading here...
			//WriteLog("(ZIPped)...");

			if (load_zipped_file(0, 0, path, NULL, &rom, &romSize) == -1)
			{
				//WriteLog("Failed!\n");
				return 0;
			}
		}
		else
		{
			/*			
			FILE * fp = fopen(path, "rb");

			if (fp == NULL)
			{
				//WriteLog("Failed!\n");
				return 0;
			}

			fseek(fp, 0, SEEK_END);
			romSize = ftell(fp);
			fseek(fp, 0, SEEK_SET);
			fread(rom, 1, romSize, fp);
			fclose(fp);
			*/

			// Handle gzipped files transparently [Adam Green]...

			gzFile fp = gzopen(path, "rb");

			if (fp == NULL)
			{
				//WriteLog("Failed!\n");
				return 0;
			}

			romSize = gzfilelength(fp);
			gzseek(fp, 0, SEEK_SET);
			gzread(fp, rom, romSize);
			gzclose(fp);
		}

		//WriteLog("OK (%i bytes)\n", romSize);
	}

	return romSize;
}

//
// Jaguar file loading
//
uint8_t JaguarLoadFile(char * path)
{
	char * ext = strrchr(path, '.');
	jaguarRomSize = JaguarLoadROM(jaguar_mainRom, path);

	if (jaguarRomSize == 0)
	{
		//WriteLog("GUI: Could not load ROM from file \"%s\"...\nAborting load!\n", path);
		return false;
	}

	jaguar_mainRom_crc32 = crc32_calcCheckSum(jaguar_mainRom, jaguarRomSize);
	eeprom_init();
	//WriteLog("CRC: %08X\n", (unsigned int)jaguar_mainRom_crc32);

	jaguarRunAddress = 0x802000;

	if (strcasecmp(ext, ".rom") == 0)
	{
		//WriteLog("GUI: Setting up homebrew (ROM)... Run address: 00802000, length: %08X\n", jaguarRomSize);

		for(int i=jaguarRomSize-1; i>=0; i--)
			jaguar_mainRom[0x2000 + i] = jaguar_mainRom[i];

		memset(jaguar_mainRom, 0xFF, 0x2000);
		SET32(jaguar_mainRam, 0x10, 0x00001000);
		SET16(jaguar_mainRam, 0x1000, 0x60FE);		// Here: bra Here
	}
	else if (strcasecmp(ext, ".abs") == 0)
	{
		if (jaguar_mainRom[0] == 0x60 && jaguar_mainRom[1] == 0x1B)
		{
			uint32_t loadAddress = GET32(jaguar_mainRom, 0x16),
			codeSize = GET32(jaguar_mainRom, 0x02) + GET32(jaguar_mainRom, 0x06);

			if (loadAddress < 0x800000)
				memcpy(jaguar_mainRam + loadAddress, jaguar_mainRom + 0x24, codeSize);
			else
			{
				for(int i=codeSize-1; i>=0; i--)
					jaguar_mainRom[(loadAddress - 0x800000) + i] = jaguar_mainRom[i + 0x24];
			}

			jaguarRunAddress = loadAddress;
		}
		else if (jaguar_mainRom[0] == 0x01 && jaguar_mainRom[1] == 0x50)
		{
			uint32_t loadAddress = GET32(jaguar_mainRom, 0x28), runAddress = GET32(jaguar_mainRom, 0x24),
				codeSize = GET32(jaguar_mainRom, 0x18) + GET32(jaguar_mainRom, 0x1C);
			//WriteLog("GUI: Setting up homebrew (ABS-2)... Run address: %08X, length: %08X\n", runAddress, codeSize);

			if (loadAddress < 0x800000)
				memcpy(jaguar_mainRam + loadAddress, jaguar_mainRom + 0xA8, codeSize);
			else
			{
				for(int i=codeSize-1; i>=0; i--)
					jaguar_mainRom[(loadAddress - 0x800000) + i] = jaguar_mainRom[i + 0xA8];
			}

			jaguarRunAddress = runAddress;
		}
		else
		{
			//WriteLog("GUI: Couldn't find correct ABS format: %02X %02X\n", jaguar_mainRom[0], jaguar_mainRom[1]);
			return false;
		}
	}
	else if (strcasecmp(ext, ".jag") == 0)
	{
		if (jaguar_mainRom[0] == 0x60 && jaguar_mainRom[1] == 0x1A)
		{
			uint32_t loadAddress = GET32(jaguar_mainRom, 0x22), runAddress = GET32(jaguar_mainRom, 0x2A);
			//WriteLog("GUI: Setting up homebrew (JAG)... Run address: %08X, length: %08X\n", runAddress, jaguarRomSize - 0x2E);
			memcpy(jaguar_mainRam + loadAddress, jaguar_mainRom + 0x2E, jaguarRomSize - 0x2E);
			jaguarRunAddress = runAddress;
		}
		else
			return false;
	}
	// .J64 (Jaguar cartridge ROM image) is implied by the FileList object...

	return true;
}

//
// Get the length of a (possibly) gzipped file
//
int gzfilelength(gzFile gd)
{
   int size = 0, length = 0;
   unsigned char buffer[0x10000];

   gzrewind(gd);

   do
   {
      // Read in chunks until EOF
      size = gzread(gd, buffer, 0x10000);

      if (size <= 0)
      	break;

      length += size;
   }
   while (!gzeof(gd));

   gzrewind(gd);
   return length;
}
