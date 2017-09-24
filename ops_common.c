#include "ops_common.h"
#include "ustring.h"

#include "dirent.h"

/* ------------------------------------------------------------------------- */
/*  Utility functions                                                        */
/* ------------------------------------------------------------------------- */

void repad_filename(uint8_t* name)
{
	uint8_t len = (uint8_t)ustrlen(name);

	memset(name + len, 0xa0, CBM_NAME_LENGTH - len);
}

void terminate_filename(uint8_t* name)
{
	for(uint8_t i = 0; i < CBM_NAME_LENGTH; ++i) {
		if(name[i] == 0xa0)
			name[i] = 0;
	}
}
