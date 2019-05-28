//
// VIDEO.CPP: SDL/local hardware specific video routines
//
// by James L. Hammons
//

#include "tom.h"
#include <SDL/SDL.h>
#include "settings.h"
#include "video.h"

// External global variables

//shouldn't these exist here??? Prolly.
//And now, they do! :-)
SDL_Surface * surface, * mainSurface, *surface_r;
uint32_t mainSurfaceFlags;
int16_t * backbuffer;
SDL_Joystick * joystick;

// One of the reasons why OpenGL is slower then normal SDL rendering, is because
// the data is being pumped into the buffer every frame with a overflow as result.
// So, we going tot render every 1 frame instead of every 0 frame.
int frame_ticker  = 0;


/* alekmaul's scaler taken from mame4all */
static void bitmap_scale(uint32_t startx, uint32_t starty, uint32_t viswidth, uint32_t visheight, uint32_t newwidth, uint32_t newheight,uint32_t pitchsrc,uint32_t pitchdest, uint16_t* restrict src, uint16_t* restrict dst)
{
    uint32_t W,H,ix,iy,x,y;
    x=startx<<16;
    y=starty<<16;
    W=newwidth;
    H=newheight;
    ix=(viswidth<<16)/W;
    iy=(visheight<<16)/H;

    do 
    {
        uint16_t* restrict buffer_mem=&src[(y>>16)*pitchsrc];
        W=newwidth; x=startx<<16;
        do 
        {
            *dst++=buffer_mem[x>>16];
            x+=ix;
        } while (--W);
        dst+=pitchdest;
        y+=iy;
    } while (--H);
}



//
// Create SDL/OpenGL surfaces
//
uint8_t InitVideo(void)
{
#ifdef GCW0
		mainSurfaceFlags = SDL_HWSURFACE | SDL_TRIPLEBUF;
#else
	// Get proper info about the platform we're running on...
	const SDL_VideoInfo * info = SDL_GetVideoInfo();

	if (!info)
	{
		//WriteLog("VJ: SDL is unable to get the video info: %s\n", SDL_GetError());
		return false;
	}
	
	if (info->hw_available)
		mainSurfaceFlags = SDL_HWSURFACE | SDL_HWPALETTE | SDL_DOUBLEBUF;
	if (info->blit_hw)
		mainSurfaceFlags |= SDL_HWACCEL;

	if (vjs.fullscreen)
		mainSurfaceFlags |= SDL_FULLSCREEN;
#endif

#ifdef SCALE_TO_SCREEN
	mainSurface = SDL_SetVideoMode(0,0, 16, SDL_HWSURFACE | SDL_HWPALETTE | SDL_ASYNCBLIT);
#else
	mainSurface = SDL_SetVideoMode(VIRTUAL_SCREEN_WIDTH,
	(vjs.hardwareTypeNTSC ? VIRTUAL_SCREEN_HEIGHT_NTSC : VIRTUAL_SCREEN_HEIGHT_PAL),
	16, mainSurfaceFlags);
#endif

	if (mainSurface == NULL)
	{
		//WriteLog("VJ: SDL is unable to set the video mode: %s\n", SDL_GetError());
		return false;
	}

	SDL_WM_SetCaption("Virtual Jaguar", "Virtual Jaguar");

	// Create the primary SDL display (16 BPP, 5/5/5 RGB format)
	surface = SDL_CreateRGBSurface(SDL_SWSURFACE, VIRTUAL_SCREEN_WIDTH,
		(vjs.hardwareTypeNTSC ? VIRTUAL_SCREEN_HEIGHT_NTSC : VIRTUAL_SCREEN_HEIGHT_PAL),
		16, 0x7C00, 0x03E0, 0x001F, 0);
		
	surface_r = SDL_CreateRGBSurface(SDL_SWSURFACE, VIRTUAL_SCREEN_WIDTH,
		(vjs.hardwareTypeNTSC ? VIRTUAL_SCREEN_HEIGHT_NTSC : VIRTUAL_SCREEN_HEIGHT_PAL),
		16, 0, 0, 0, 0);

	if (surface == NULL)
	{
		//WriteLog("VJ: Could not create primary SDL surface: %s\n", SDL_GetError());
		return false;
	}
		

	// Initialize Joystick support under SDL
	if (vjs.useJoystick)
	{
		if (SDL_NumJoysticks() <= 0)
		{
			vjs.useJoystick = false;
			printf("VJ: No joystick(s) or joypad(s) detected on your system. Using keyboard...\n");
		}
		else
		{
			if ((joystick = SDL_JoystickOpen(vjs.joyport)) == 0)
			{
				vjs.useJoystick = false;
				printf("VJ: Unable to open a Joystick on port: %d\n", (int)vjs.joyport);
			}
			else
				printf("VJ: Using: %s\n", SDL_JoystickName(vjs.joyport));
		}
	}

	// Set up the backbuffer
//To be safe, this should be 1280 * 625 * 2...
	backbuffer = (int16_t *)malloc(1280 * 625 * sizeof(int16_t));
// backbuffer = (int16_t *)malloc(1280 * 625 * sizeof(int16_t));
//	memset(backbuffer, 0x44, VIRTUAL_SCREEN_WIDTH * VIRTUAL_SCREEN_HEIGHT_NTSC * sizeof(int16_t));
	memset(backbuffer, 0x44, VIRTUAL_SCREEN_WIDTH *
		(vjs.hardwareTypeNTSC ? VIRTUAL_SCREEN_HEIGHT_NTSC : VIRTUAL_SCREEN_HEIGHT_PAL)
		* sizeof(int16_t));

	return true;
}

//
// Free various SDL components
//
void VideoDone(void)
{
	if (joystick) SDL_JoystickClose(joystick);
	if (surface) SDL_FreeSurface(surface);
	if (backbuffer) free(backbuffer);
}

//
// Render the backbuffer to the primary screen surface
//
void RenderBackbuffer(void)
{
	if (SDL_MUSTLOCK(surface))
		SDL_LockSurface(surface);

	memcpy(surface->pixels, backbuffer, tom_getVideoModeWidth() * tom_getVideoModeHeight() * 2);

	if (SDL_MUSTLOCK(surface))
		SDL_UnlockSurface(surface);

#ifdef SCALE_TO_SCREEN
	surface_r = SDL_DisplayFormat(surface);
	bitmap_scale(0, 0, surface_r->w, surface_r->h, mainSurface->w, mainSurface->h, surface_r->w, 0, (uint16_t* restrict) surface_r->pixels, (uint16_t* restrict)mainSurface->pixels);
	//SDL_SoftStretch(surface_r, NULL, mainSurface, NULL);
#else
	SDL_BlitSurface(surface, NULL, mainSurface, NULL);
#endif
	SDL_Flip(mainSurface);
}

//
// Resize the main SDL screen & backbuffer
//
void ResizeScreen(uint32_t width, uint32_t height)
{
	SDL_FreeSurface(surface);
	surface = SDL_CreateRGBSurface(SDL_SWSURFACE, width, height, 16, 0x7C00, 0x03E0, 0x001F, 0);

	if (surface == NULL)
	{
		//WriteLog("Video: Could not create primary SDL surface: %s", SDL_GetError());
//This is just crappy. We shouldn't exit this way--it leaves all kinds of memory leaks
//as well as screwing up SDL... !!! FIX !!!
		exit(1);
	}


#ifdef SCALE_TO_SCREEN
	mainSurface = SDL_SetVideoMode(0, 0, 16, SDL_HWSURFACE | SDL_HWPALETTE | SDL_ASYNCBLIT);
#else
	mainSurface = SDL_SetVideoMode(width, height, 16, mainSurfaceFlags);
#endif
	if (mainSurface == NULL)
	{
		//WriteLog("Video: SDL is unable to set the video mode: %s\n", SDL_GetError());
		exit(1);
	}
}

//
// Return the screen's pitch
//
uint32_t GetSDLScreenPitch(void)
{
	return surface->pitch;
}

//
// Fullscreen <-> window switching
//
void ToggleFullscreen(void)
{
	vjs.fullscreen = !vjs.fullscreen;
	mainSurfaceFlags &= ~SDL_FULLSCREEN;

	if (vjs.fullscreen)
		mainSurfaceFlags |= SDL_FULLSCREEN;

	mainSurface = SDL_SetVideoMode(tom_width, tom_height, 16, mainSurfaceFlags);

	if (mainSurface == NULL)
	{
		//WriteLog("Video: SDL is unable to set the video mode: %s\n", SDL_GetError());
		exit(1);
	}

	SDL_WM_SetCaption("Virtual Jaguar", "Virtual Jaguar");
}
