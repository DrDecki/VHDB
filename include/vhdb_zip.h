#ifndef VHDB_ZIP_H
#define VHDB_ZIP_H

#include <stdint.h>

typedef int (*vhdb_zip_progress)(uint32_t done, uint32_t total, const char *name,
				 void *user);

int vhdb_zip_extract(const char *archive_path, const char *destination,
		     vhdb_zip_progress progress, void *user);
const char *vhdb_zip_error(void);

#endif
