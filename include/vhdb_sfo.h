#ifndef VHDB_SFO_H
#define VHDB_SFO_H

#include <stdint.h>
#include <stddef.h>

#define VHDB_SFO_MAGIC 0x46535000u

#define VHDB_SFO_OK 0
#define VHDB_SFO_ERR_SIZE -1
#define VHDB_SFO_ERR_MAGIC -2
#define VHDB_SFO_ERR_LAYOUT -3

typedef struct {
	char title_id[16];
	char app_ver[16];
	char category[8];
	char title[128];
} vhdb_sfo;

int vhdb_sfo_parse(const uint8_t *data, uint32_t size, vhdb_sfo *out);
int vhdb_sfo_parse_file(const char *path, vhdb_sfo *out);

#endif
