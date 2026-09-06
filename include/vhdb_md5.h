#ifndef VHDB_MD5_H
#define VHDB_MD5_H

#include <stdint.h>
#include <stddef.h>

typedef struct {
	uint32_t state[4];
	uint32_t count[2];
	uint8_t buffer[64];
} vhdb_md5_ctx;

void vhdb_md5_init(vhdb_md5_ctx *ctx);
void vhdb_md5_update(vhdb_md5_ctx *ctx, const uint8_t *data, size_t length);
void vhdb_md5_final(vhdb_md5_ctx *ctx, uint8_t out[16]);
int vhdb_md5_file(const char *path, uint8_t out[16]);
void vhdb_md5_hex(const uint8_t digest[16], char out[33]);

#endif
