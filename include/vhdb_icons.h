#ifndef VHDB_ICONS_H
#define VHDB_ICONS_H

#include "vhdb.h"
#include "vhdb_vitanet.h"
#include "vhdb_zip.h"

#include <vita2d.h>

int vhdb_icons_start(const vhdb_db *db);
void vhdb_icons_stop(void);
vita2d_texture *vhdb_icon_for(uint32_t index);
void vhdb_icons_retry(uint32_t index);

#define VHDB_PACK_IDLE 0
#define VHDB_PACK_RUNNING 1
#define VHDB_PACK_DONE 2
#define VHDB_PACK_FAILED 3

void vhdb_icons_request_pack(void);
int vhdb_icons_pack_state(void);
void vhdb_icons_pack_progress(uint64_t *done, uint64_t *total, uint32_t *files,
			      uint32_t *count);
int vhdb_icons_have_pack(void);
int vhdb_icons_fetch_pack(vhdb_progress_fn progress, void *download_label,
			  vhdb_zip_progress unpack, void *unpack_label);

#endif
