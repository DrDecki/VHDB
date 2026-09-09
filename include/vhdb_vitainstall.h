#ifndef VHDB_VITAINSTALL_H
#define VHDB_VITAINSTALL_H

#include <stdint.h>

#include "vhdb_vitanet.h"
#include "vhdb_zip.h"

#define VHDB_DATA_DIR "ux0:data/vhdb"

int vhdb_install_from_url(const char *url, const uint8_t expected[16],
			  vhdb_progress_fn progress, void *download_label,
			  vhdb_zip_progress unpack, void *unpack_label);
int vhdb_install_package(const char *directory);
const char *vhdb_install_error(void);
int vhdb_install_mismatched(void);

#endif
