#ifndef VHDB_NET_H
#define VHDB_NET_H

#include <stdint.h>
#include <stddef.h>

#define VHDB_NET_OK 0
#define VHDB_NET_ERR -1

int vhdb_net_init(void);
void vhdb_net_shutdown(void);

int vhdb_net_head_bytes(const char *url, uint8_t *out, size_t length);
int vhdb_net_download(const char *url, const char *path, int show_progress);
const char *vhdb_net_last_error(void);

int vhdb_net_ftp_upload(const char *url, const char *user, const char *pass,
			const char *path, int show_progress);
int vhdb_net_ftp_list(const char *url, const char *user, const char *pass,
		      char *out, size_t size);
int vhdb_net_ftp_get(const char *url, const char *user, const char *pass,
		     uint8_t *out, size_t size, size_t *got);
int vhdb_net_ftp_md5(const char *url, const char *user, const char *pass,
		     uint8_t out[16], long *size);

#endif
