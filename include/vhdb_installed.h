#ifndef VHDB_INSTALLED_H
#define VHDB_INSTALLED_H

#include "vhdb_status.h"

typedef struct {
	vhdb_installed *items;
	int count;
	int capacity;
} vhdb_installed_list;

int vhdb_installed_load(vhdb_installed_list *list, const char *path);
int vhdb_installed_save(const vhdb_installed_list *list, const char *path);
vhdb_installed *vhdb_installed_find_id(vhdb_installed_list *list, uint32_t id);
vhdb_installed *vhdb_installed_find_titleid(vhdb_installed_list *list,
					    const char *titleid);
int vhdb_installed_set(vhdb_installed_list *list, const vhdb_installed *entry);
int vhdb_installed_remove(vhdb_installed_list *list, uint32_t id);
void vhdb_installed_free(vhdb_installed_list *list);

#endif
