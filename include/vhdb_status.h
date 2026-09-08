#ifndef VHDB_STATUS_H
#define VHDB_STATUS_H

#include "vhdb.h"

#define VHDB_VER_SAME 0
#define VHDB_VER_OLDER 1
#define VHDB_VER_NEWER 2
#define VHDB_VER_UNKNOWN 3

#define VHDB_STATE_NOT_INSTALLED 0
#define VHDB_STATE_INSTALLED 1
#define VHDB_STATE_UPDATE 2
#define VHDB_STATE_UNKNOWN_VERSION 3
#define VHDB_STATE_ROLLING 4

#define VHDB_NEED_UNCHECKED 0
#define VHDB_NEED_MET 1
#define VHDB_NEED_MISSING 2

#define VHDB_NEED_KIND_OTHER 0
#define VHDB_NEED_KIND_GAMEFILES 1
#define VHDB_NEED_KIND_PLUGIN 2
#define VHDB_NEED_KIND_DATAFILE 3
#define VHDB_NEED_KIND_FIRMWARE 4

typedef struct {
	int kind;
	char path[128];
	char text[192];
	int state;
} vhdb_need;

typedef struct {
	int state;
	int needs_total;
	int needs_met;
	int needs_checked;
	int trusted;
	int by_content;
} vhdb_status;

typedef struct {
	uint32_t id;
	char titleid[13];
	char version[32];
	uint8_t hash[16];
	int has_hash;
	uint8_t eboot[16];
	int has_eboot;
	uint8_t aux[16];
	int has_aux;
	int from_console;
} vhdb_installed;

typedef int (*vhdb_path_exists_fn)(const char *path, void *user);

int vhdb_version_compare(const char *installed, const char *catalogue);
const char *vhdb_state_name(int state);

int vhdb_needs_count(const char *needs);
int vhdb_needs_get(const char *needs, int index, vhdb_need *out);
int vhdb_needs_check(const char *needs, int index, vhdb_need *out,
		     vhdb_path_exists_fn exists, void *user);

#define VHDB_INSTALL_VPK 0
#define VHDB_INSTALL_PSP 1
#define VHDB_INSTALL_MANUAL 2
#define VHDB_INSTALL_PC 3

#define VHDB_CLIENT_VITA 0
#define VHDB_CLIENT_PC 1

int vhdb_install_kind(const vhdb_record *rec);
int vhdb_can_install(const vhdb_record *rec, int client);
int vhdb_has_data_file(const vhdb_record *rec);
const char *vhdb_install_label(int kind);
const char *vhdb_list_label(const vhdb_record *rec, const vhdb_status *status, int client);

void vhdb_status_of(const vhdb_db *db, const vhdb_record *rec,
		    const vhdb_installed *installed, vhdb_status *out,
		    vhdb_path_exists_fn exists, void *user);

#endif
