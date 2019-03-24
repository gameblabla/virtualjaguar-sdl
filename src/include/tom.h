//
// TOM Header file
//

#ifndef __TOM_H__
#define __TOM_H__

#include "jaguar.h"

#define VIDEO_MODE_16BPP_CRY	0
#define VIDEO_MODE_24BPP_RGB	1
#define VIDEO_MODE_16BPP_DIRECT 2
#define VIDEO_MODE_16BPP_RGB	3

// 68000 Interrupt bit positions (enabled at $F000E0)

enum { IRQ_VBLANK = 0, IRQ_GPU, IRQ_OPFLAG, IRQ_TIMER, IRQ_DSP };

void tom_init(void);
void tom_reset(void);
void tom_done(void);

uint8 TOMReadByte(uint32_t offset, uint32_t who);
uint16_t TOMReadWord(uint32_t offset, uint32_t who);
void TOMWriteByte(uint32_t offset, uint8 data, uint32_t who);
void TOMWriteWord(uint32_t offset, uint16_t data, uint32_t who);

//void TOMExecScanline(int16 * backbuffer, int32 scanline, uint8_t render);
void TOMExecScanline(uint16_t scanline, uint8_t render);
uint32_t tom_getVideoModeWidth(void);
uint32_t tom_getVideoModeHeight(void);
uint8 tom_getVideoMode(void);
uint8 * tom_get_ram_pointer(void);
uint16_t tom_get_hdb(void);
uint16_t tom_get_vdb(void);
//uint16_t tom_get_scanline(void);
//uint32_t tom_getHBlankWidthInPixels(void);

int	tom_irq_enabled(int irq);
uint16_t tom_irq_control_reg(void);
void tom_set_irq_latch(int irq, int enabled);
void TOMExecPIT(uint32_t cycles);
void tom_set_pending_jerry_int(void);
void tom_set_pending_timer_int(void);
void tom_set_pending_object_int(void);
void tom_set_pending_gpu_int(void);
void tom_set_pending_video_int(void);
void TOMResetPIT(void);

//uint32_t TOMGetSDLScreenPitch(void);
void TOMResetBackbuffer(int16 * backbuffer);

// Exported variables

extern uint32_t tom_width;
extern uint32_t tom_height;

#endif	// __TOM_H__
