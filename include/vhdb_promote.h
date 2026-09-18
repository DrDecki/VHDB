#ifndef VHDB_PROMOTE_H
#define VHDB_PROMOTE_H

int vhdb_promote_directory(const char *directory);
int vhdb_delete_package(const char *titleid);
const char *vhdb_promote_error(void);
void vhdb_remove_tree(const char *path);

#endif
