//
// DAC (really, Synchronous Serial Interface) Handler
//
// Originally by David Raingeard
// GCC/SDL port by Niels Wagenaar (Linux/WIN32) and Caz (BeOS)
// Rewritten by James L. Hammons
//

#include "SDL.h"
#include "m68k.h"
#include "jaguar.h"
#include "settings.h"
#include "dac.h"

//#define DEBUG_DAC

#define BUFFER_SIZE		0x10000						// Make the DAC buffers 64K x 16 bits

// Jaguar memory locations

#define LTXD			0xF1A148
#define RTXD			0xF1A14C
#define LRXD			0xF1A148
#define RRXD			0xF1A14C
#define SCLK			0xF1A150
#define SMODE			0xF1A154

// Global variables

uint16_t lrxd, rrxd;									// I2S ports (into Jaguar)

// Local variables

uint32_t LeftFIFOHeadPtr, LeftFIFOTailPtr, RightFIFOHeadPtr, RightFIFOTailPtr;
SDL_AudioSpec desired;

// We can get away with using native endian here because we can tell SDL to use the native
// endian when looking at the sample buffer, i.e., no need to worry about it.

uint16_t * DACBuffer;
uint8_t SCLKFrequencyDivider = 19;						// Default is roughly 22 KHz (20774 Hz in NTSC mode)
uint16_t serialMode = 0;

// Private function prototypes

void SDLSoundCallback(void * userdata, Uint8 * buffer, int length);
int GetCalculatedFrequency(void);

//
// Initialize the SDL sound system
//
void DACInit(void)
{
	memory_malloc_secure((void **)&DACBuffer, BUFFER_SIZE * sizeof(uint16_t), "DAC buffer");

	desired.freq = GetCalculatedFrequency();		// SDL will do conversion on the fly, if it can't get the exact rate. Nice!
	desired.format = AUDIO_S16SYS;					// This uses the native endian (for portability)...
	desired.channels = 2;
	desired.samples = 2048;							// Let's try a 2K buffer (can always go lower)
	desired.callback = SDLSoundCallback;

	if (SDL_OpenAudio(&desired, NULL) < 0)			// NULL means SDL guarantees what we want
	{
		exit(1);
	}

	DACReset();
	SDL_PauseAudio(false);							// Start playback!
}

//
// Reset the sound buffer FIFOs
//
void DACReset(void)
{
	LeftFIFOHeadPtr = LeftFIFOTailPtr = 0, RightFIFOHeadPtr = RightFIFOTailPtr = 1;
}

//
// Close down the SDL sound subsystem
//
void DACDone(void)
{
	SDL_PauseAudio(true);
	SDL_CloseAudio();
	memory_free(DACBuffer);
}

//
// SDL callback routine to fill audio buffer
//
// Note: The samples are packed in the buffer in 16 bit left/16 bit right pairs.
//
void SDLSoundCallback(void * userdata, Uint8 * buffer, int length)
{
	memset(buffer, desired.silence, length);
	if (LeftFIFOHeadPtr != LeftFIFOTailPtr)
	{
		int numLeftSamplesReady
			= (LeftFIFOTailPtr + (LeftFIFOTailPtr < LeftFIFOHeadPtr ? BUFFER_SIZE : 0))
				- LeftFIFOHeadPtr;
		int numRightSamplesReady
			= (RightFIFOTailPtr + (RightFIFOTailPtr < RightFIFOHeadPtr ? BUFFER_SIZE : 0))
				- RightFIFOHeadPtr;
		int numSamplesReady
			= (numLeftSamplesReady < numRightSamplesReady
				? numLeftSamplesReady : numRightSamplesReady);//Hmm. * 2;

		if (numSamplesReady > length / 2)	// length / 2 because we're comparing 16-bit lengths
			numSamplesReady = length / 2;

		for(int i=0; i<numSamplesReady; i++)
			((uint16_t *)buffer)[i] = DACBuffer[(LeftFIFOHeadPtr + i) % BUFFER_SIZE];
		LeftFIFOHeadPtr = (LeftFIFOHeadPtr + numSamplesReady) % BUFFER_SIZE;
		RightFIFOHeadPtr = (RightFIFOHeadPtr + numSamplesReady) % BUFFER_SIZE;
	}
}

//
// Calculate the frequency of SCLK * 32 using the divider
//
int GetCalculatedFrequency(void)
{
	int systemClockFrequency = (vjs.hardwareTypeNTSC ? RISC_CLOCK_RATE_NTSC : RISC_CLOCK_RATE_PAL);

	// We divide by 32 here in order to find the frequency of 32 SCLKs in a row (transferring
	// 16 bits of left data + 16 bits of right data = 32 bits, 1 SCLK = 1 bit transferred).
	return systemClockFrequency / (32 * (2 * (SCLKFrequencyDivider + 1)));
}

//
// LTXD/RTXD/SCLK/SMODE ($F1A148/4C/50/54)
//
void DACWriteByte(uint32_t offset, uint8_t data, uint32_t who/*= UNKNOWN*/)
{
	who = UNKNOWN;
	//WriteLog("DAC: %s writing BYTE %02X at %08X\n", whoName[who], data, offset);
	if (offset == SCLK + 3)
		DACWriteWord(offset - 3, (uint16_t)data, UNKNOWN);
}

void DACWriteWord(uint32_t offset, uint16_t data, uint32_t who/*= UNKNOWN*/)
{
	who = UNKNOWN;
	if (offset == LTXD + 2)
	{
		while ((LeftFIFOTailPtr + 2) & (BUFFER_SIZE - 1) == LeftFIFOHeadPtr);

		SDL_LockAudio();							// Is it necessary to do this? Mebbe.
		LeftFIFOTailPtr = (LeftFIFOTailPtr + 2) % BUFFER_SIZE;
		DACBuffer[LeftFIFOTailPtr] = data;
		SDL_UnlockAudio();
	}
	else if (offset == RTXD + 2)
	{
		while ((RightFIFOTailPtr + 2) & (BUFFER_SIZE - 1) == RightFIFOHeadPtr);
		

		SDL_LockAudio();
		RightFIFOTailPtr = (RightFIFOTailPtr + 2) % BUFFER_SIZE;
		DACBuffer[RightFIFOTailPtr] = data;
		SDL_UnlockAudio();
	}
	else if (offset == SCLK + 2)					// Sample rate
	{
		if ((uint8_t)data != SCLKFrequencyDivider)
		{
			SCLKFrequencyDivider = (uint8_t)data;
			if (data > 7)	// Anything less than 8 is too high!
			{
				SDL_CloseAudio();
				desired.freq = GetCalculatedFrequency();// SDL will do conversion on the fly, if it can't get the exact rate. Nice!
				if (SDL_OpenAudio(&desired, NULL) < 0)	// NULL means SDL guarantees what we want
				{
					exit(1);
				}

				DACReset();
				SDL_PauseAudio(false);				// Start playback!
			}
		}
	}
	else if (offset == SMODE + 2)
	{
		serialMode = data;
	}
}

//
// LRXD/RRXD/SSTAT ($F1A148/4C/50)
//
uint8_t DACReadByte(uint32_t offset, uint32_t who/*= UNKNOWN*/)
{
	who = UNKNOWN;
	return 0xFF;
}

//static uint16_t fakeWord = 0;
uint16_t DACReadWord(uint32_t offset, uint32_t who/*= UNKNOWN*/)
{
	who = UNKNOWN;
	if (offset == LRXD || offset == RRXD)
		return 0x0000;
	else if (offset == LRXD + 2)
		return lrxd;
	else if (offset == RRXD + 2)
		return rrxd;

	return 0xFFFF;	// May need SSTAT as well... (but may be a Jaguar II only feature)		
}
