#ifndef VHDB_INSTALL_H
#define VHDB_INSTALL_H

#include "vhdb.h"
#include "vhdb_config.h"
#include "vhdb_installed.h"

int vhdb_pc_install(const vhdb_config *cfg, const vhdb_db *db,
		    const vhdb_record *rec, vhdb_installed_list *installed,
		    int with_data);

#endif
