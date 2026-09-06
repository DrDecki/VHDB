#include "vhdb_sha1.h"

#include <string.h>

static uint32_t rotate(uint32_t value, int bits)
{
	return (value << bits) | (value >> (32 - bits));
}

static void transform(uint32_t state[5], const uint8_t block[64])
{
	uint32_t w[80];
	uint32_t a, b, c, d, e;
	int i;

	for (i = 0; i < 16; i++)
		w[i] = ((uint32_t)block[i * 4] << 24) |
		       ((uint32_t)block[i * 4 + 1] << 16) |
		       ((uint32_t)block[i * 4 + 2] << 8) |
		       (uint32_t)block[i * 4 + 3];
	for (i = 16; i < 80; i++)
		w[i] = rotate(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);

	a = state[0];
	b = state[1];
	c = state[2];
	d = state[3];
	e = state[4];

	for (i = 0; i < 80; i++) {
		uint32_t f, k, temp;

		if (i < 20) {
			f = (b & c) | (~b & d);
			k = 0x5A827999u;
		} else if (i < 40) {
			f = b ^ c ^ d;
			k = 0x6ED9EBA1u;
		} else if (i < 60) {
			f = (b & c) | (b & d) | (c & d);
			k = 0x8F1BBCDCu;
		} else {
			f = b ^ c ^ d;
			k = 0xCA62C1D6u;
		}

		temp = rotate(a, 5) + f + e + k + w[i];
		e = d;
		d = c;
		c = rotate(b, 30);
		b = a;
		a = temp;
	}

	state[0] += a;
	state[1] += b;
	state[2] += c;
	state[3] += d;
	state[4] += e;
}

void vhdb_sha1_init(vhdb_sha1_ctx *ctx)
{
	ctx->state[0] = 0x67452301u;
	ctx->state[1] = 0xEFCDAB89u;
	ctx->state[2] = 0x98BADCFEu;
	ctx->state[3] = 0x10325476u;
	ctx->state[4] = 0xC3D2E1F0u;
	ctx->count = 0;
	memset(ctx->buffer, 0, sizeof(ctx->buffer));
}

void vhdb_sha1_update(vhdb_sha1_ctx *ctx, const uint8_t *data, size_t length)
{
	size_t index = (size_t)(ctx->count & 63);
	size_t i = 0;

	ctx->count += length;

	if (index) {
		size_t room = 64 - index;

		if (length < room) {
			memcpy(ctx->buffer + index, data, length);
			return;
		}
		memcpy(ctx->buffer + index, data, room);
		transform(ctx->state, ctx->buffer);
		i = room;
	}

	for (; i + 63 < length; i += 64)
		transform(ctx->state, data + i);

	memcpy(ctx->buffer, data + i, length - i);
}

void vhdb_sha1_final(vhdb_sha1_ctx *ctx, uint8_t out[20])
{
	uint64_t bits = ctx->count * 8;
	uint8_t tail[8];
	uint8_t padding = 0x80;
	uint8_t zero = 0;
	int i;

	for (i = 0; i < 8; i++)
		tail[i] = (uint8_t)((bits >> ((7 - i) * 8)) & 0xFF);

	vhdb_sha1_update(ctx, &padding, 1);
	while ((ctx->count & 63) != 56)
		vhdb_sha1_update(ctx, &zero, 1);
	vhdb_sha1_update(ctx, tail, 8);

	for (i = 0; i < 20; i++)
		out[i] = (uint8_t)((ctx->state[i / 4] >> ((3 - (i & 3)) * 8)) & 0xFF);
}
