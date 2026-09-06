#include "vhdb_md5.h"

#include <stdio.h>
#include <string.h>

static const uint32_t K[64] = {
	0xd76aa478u, 0xe8c7b756u, 0x242070dbu, 0xc1bdceeeu,
	0xf57c0fafu, 0x4787c62au, 0xa8304613u, 0xfd469501u,
	0x698098d8u, 0x8b44f7afu, 0xffff5bb1u, 0x895cd7beu,
	0x6b901122u, 0xfd987193u, 0xa679438eu, 0x49b40821u,
	0xf61e2562u, 0xc040b340u, 0x265e5a51u, 0xe9b6c7aau,
	0xd62f105du, 0x02441453u, 0xd8a1e681u, 0xe7d3fbc8u,
	0x21e1cde6u, 0xc33707d6u, 0xf4d50d87u, 0x455a14edu,
	0xa9e3e905u, 0xfcefa3f8u, 0x676f02d9u, 0x8d2a4c8au,
	0xfffa3942u, 0x8771f681u, 0x6d9d6122u, 0xfde5380cu,
	0xa4beea44u, 0x4bdecfa9u, 0xf6bb4b60u, 0xbebfbc70u,
	0x289b7ec6u, 0xeaa127fau, 0xd4ef3085u, 0x04881d05u,
	0xd9d4d039u, 0xe6db99e5u, 0x1fa27cf8u, 0xc4ac5665u,
	0xf4292244u, 0x432aff97u, 0xab9423a7u, 0xfc93a039u,
	0x655b59c3u, 0x8f0ccc92u, 0xffeff47du, 0x85845dd1u,
	0x6fa87e4fu, 0xfe2ce6e0u, 0xa3014314u, 0x4e0811a1u,
	0xf7537e82u, 0xbd3af235u, 0x2ad7d2bbu, 0xeb86d391u
};

static const int S[64] = {
	7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
	5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20,
	4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
	6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21
};

static uint32_t rotate(uint32_t value, int bits)
{
	return (value << bits) | (value >> (32 - bits));
}

static void transform(uint32_t state[4], const uint8_t block[64])
{
	uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
	uint32_t m[16];
	int i;

	for (i = 0; i < 16; i++)
		m[i] = (uint32_t)block[i * 4] |
		       ((uint32_t)block[i * 4 + 1] << 8) |
		       ((uint32_t)block[i * 4 + 2] << 16) |
		       ((uint32_t)block[i * 4 + 3] << 24);

	for (i = 0; i < 64; i++) {
		uint32_t f;
		int g;

		if (i < 16) {
			f = (b & c) | (~b & d);
			g = i;
		} else if (i < 32) {
			f = (d & b) | (~d & c);
			g = (5 * i + 1) % 16;
		} else if (i < 48) {
			f = b ^ c ^ d;
			g = (3 * i + 5) % 16;
		} else {
			f = c ^ (b | ~d);
			g = (7 * i) % 16;
		}

		f = f + a + K[i] + m[g];
		a = d;
		d = c;
		c = b;
		b = b + rotate(f, S[i]);
	}

	state[0] += a;
	state[1] += b;
	state[2] += c;
	state[3] += d;
}

void vhdb_md5_init(vhdb_md5_ctx *ctx)
{
	ctx->state[0] = 0x67452301u;
	ctx->state[1] = 0xefcdab89u;
	ctx->state[2] = 0x98badcfeu;
	ctx->state[3] = 0x10325476u;
	ctx->count[0] = 0;
	ctx->count[1] = 0;
	memset(ctx->buffer, 0, sizeof(ctx->buffer));
}

void vhdb_md5_update(vhdb_md5_ctx *ctx, const uint8_t *data, size_t length)
{
	size_t index = (ctx->count[0] >> 3) & 63;
	size_t partial, i;

	ctx->count[0] += (uint32_t)(length << 3);
	if (ctx->count[0] < (uint32_t)(length << 3))
		ctx->count[1]++;
	ctx->count[1] += (uint32_t)(length >> 29);

	partial = 64 - index;
	if (length >= partial) {
		memcpy(ctx->buffer + index, data, partial);
		transform(ctx->state, ctx->buffer);
		for (i = partial; i + 63 < length; i += 64)
			transform(ctx->state, data + i);
		index = 0;
	} else {
		i = 0;
	}
	memcpy(ctx->buffer + index, data + i, length - i);
}

void vhdb_md5_final(vhdb_md5_ctx *ctx, uint8_t out[16])
{
	uint8_t tail[8];
	uint8_t padding[64];
	size_t index, pad_len;
	int i;

	for (i = 0; i < 4; i++) {
		tail[i] = (uint8_t)((ctx->count[0] >> (i * 8)) & 0xFF);
		tail[i + 4] = (uint8_t)((ctx->count[1] >> (i * 8)) & 0xFF);
	}

	memset(padding, 0, sizeof(padding));
	padding[0] = 0x80;

	index = (ctx->count[0] >> 3) & 63;
	pad_len = (index < 56) ? (56 - index) : (120 - index);
	vhdb_md5_update(ctx, padding, pad_len);
	vhdb_md5_update(ctx, tail, 8);

	for (i = 0; i < 4; i++) {
		out[i * 4] = (uint8_t)(ctx->state[i] & 0xFF);
		out[i * 4 + 1] = (uint8_t)((ctx->state[i] >> 8) & 0xFF);
		out[i * 4 + 2] = (uint8_t)((ctx->state[i] >> 16) & 0xFF);
		out[i * 4 + 3] = (uint8_t)((ctx->state[i] >> 24) & 0xFF);
	}
}

int vhdb_md5_file(const char *path, uint8_t out[16])
{
	vhdb_md5_ctx ctx;
	uint8_t chunk[32768];
	FILE *file;
	size_t got;

	file = fopen(path, "rb");
	if (!file)
		return 0;

	vhdb_md5_init(&ctx);
	while ((got = fread(chunk, 1, sizeof(chunk), file)) > 0)
		vhdb_md5_update(&ctx, chunk, got);
	fclose(file);
	vhdb_md5_final(&ctx, out);
	return 1;
}

void vhdb_md5_hex(const uint8_t digest[16], char out[33])
{
	static const char hex[] = "0123456789abcdef";
	int i;

	for (i = 0; i < 16; i++) {
		out[i * 2] = hex[(digest[i] >> 4) & 0xF];
		out[i * 2 + 1] = hex[digest[i] & 0xF];
	}
	out[32] = 0;
}
