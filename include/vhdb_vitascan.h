#ifndef VHDB_VITASCAN_H
#define VHDB_VITASCAN_H

#include "vhdb.h"
#include "vhdb_installed.h"

typedef void (*vhdb_scan_progress)(int done, const char *name, void *user);

int vhdb_app_installed(const char *titleid);
int vhdb_prune_installed(vhdb_installed_list *installed);
int vhdb_vita_scan(const vhdb_db *db, vhdb_installed_list *installed,
		   vhdb_scan_progress progress, void *user);

#endif
