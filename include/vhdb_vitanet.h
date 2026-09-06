#ifndef VHDB_VITANET_H
#define VHDB_VITANET_H

#include <stdint.h>

typedef int (*vhdb_progress_fn)(uint64_t done, uint64_t total, void *user);

int vhdb_net_start(void);
void vhdb_net_stop(void);
int vhdb_net_online(void);
const char *vhdb_net_error(void);

int vhdb_net_head(const char *url, uint8_t *out, unsigned int length);
int vhdb_net_fetch(const char *url, const char *path, vhdb_progress_fn progress,
		   void *user);

#endif
