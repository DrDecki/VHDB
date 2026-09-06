#ifndef VHDB_SHA1_H
#define VHDB_SHA1_H

#include <stdint.h>
#include <stddef.h>

typedef struct {
	uint32_t state[5];
	uint64_t count;
	uint8_t buffer[64];
} vhdb_sha1_ctx;

void vhdb_sha1_init(vhdb_sha1_ctx *ctx);
void vhdb_sha1_update(vhdb_sha1_ctx *ctx, const uint8_t *data, size_t length);
void vhdb_sha1_final(vhdb_sha1_ctx *ctx, uint8_t out[20]);

#endif
