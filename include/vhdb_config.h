#ifndef VHDB_CONFIG_H
#define VHDB_CONFIG_H

#include <stddef.h>

#define VHDB_TARGET_NONE 0
#define VHDB_TARGET_FTP 1
#define VHDB_TARGET_FOLDER 2

typedef struct {
	char catalogue_url[256];
	int target;
	char host[64];
	int port;
	char user[64];
	char pass[64];
	char vpk[160];
	char data[160];
	char pspemu[160];
	char keep[256];
} vhdb_config;

int vhdb_config_dir(char *out, size_t size);
int vhdb_config_file(char *out, size_t size);
int vhdb_catalogue_file(char *out, size_t size);
int vhdb_make_dirs(const char *path);

void vhdb_config_defaults(vhdb_config *cfg);
int vhdb_config_load(vhdb_config *cfg);
int vhdb_config_save(const vhdb_config *cfg);
const char *vhdb_target_name(int target);

#endif
