//
// CDI.H: Header file
//

#ifndef __CDI_H__
#define __CDI_H__

#include "jaguar.h"
#include "types.h"
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>

struct s_cdi_track
{
	uint8_t	filename_length;
	uint32_t	pregap_length;
	uint32_t  length;
	uint32_t	mode;
	uint32_t	start_lba;
	uint32_t  total_length;
	uint32_t	sector_size;
	uint32_t	sector_size_value;
	uint32_t	position;
};

struct s_cdi_session
{
	uint16_t nb_of_tracks;
	struct s_cdi_track *tracks;
};

struct s_cdi_descriptor
{
	uint32_t length;
	uint32_t version;
	uint32_t header_offset;
	uint16_t nb_of_sessions;
	struct s_cdi_session *sessions;
};

// Exported variables

extern int cdi_fp;
extern uint32_t cdi_load_address;
extern uint32_t cdi_code_length;
extern struct s_cdi_descriptor	* cdi_descriptor;
extern struct s_cdi_track ** cdi_tracks;
extern uint32_t cdi_tracks_count;

// Exported functions

int cdi_open(char * path);
void cdi_close(int fp);
struct s_cdi_descriptor * cdi_get_descriptor(int fp, FILE * stdfp);
void cdi_dump_descriptor(FILE * fp,struct s_cdi_descriptor * cdi_descriptor);
uint8_t * cdi_extract_boot_code(int fp, struct s_cdi_descriptor * cdi_descriptor);
void cdi_load_sector(uint32_t sector, uint8_t * buffer);

#endif	// __CDI_H__
