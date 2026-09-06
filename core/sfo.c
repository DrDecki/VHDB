#include "vhdb_sfo.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t read_u32(const uint8_t *p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
	       ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint16_t read_u16(const uint8_t *p)
{
	return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static void copy_string(char *dst, size_t dst_size, const uint8_t *src,
			uint32_t src_len)
{
	size_t i;

	if (dst_size == 0)
		return;
	if (src_len > dst_size - 1)
		src_len = (uint32_t)(dst_size - 1);
	for (i = 0; i < src_len; i++) {
		if (src[i] == 0)
			break;
		dst[i] = (char)src[i];
	}
	dst[i] = 0;
}

int vhdb_sfo_parse(const uint8_t *data, uint32_t size, vhdb_sfo *out)
{
	uint32_t key_table, data_table, count, i;

	memset(out, 0, sizeof(*out));

	if (size < 20)
		return VHDB_SFO_ERR_SIZE;
	if (read_u32(data) != VHDB_SFO_MAGIC)
		return VHDB_SFO_ERR_MAGIC;

	key_table = read_u32(data + 8);
	data_table = read_u32(data + 12);
	count = read_u32(data + 16);

	if (count > 4096)
		return VHDB_SFO_ERR_LAYOUT;
	if (key_table > size || data_table > size)
		return VHDB_SFO_ERR_LAYOUT;
	if (20 + count * 16 > size)
		return VHDB_SFO_ERR_LAYOUT;

	for (i = 0; i < count; i++) {
		const uint8_t *entry = data + 20 + i * 16;
		uint32_t key_off = key_table + read_u16(entry);
		uint32_t data_len = read_u32(entry + 4);
		uint32_t data_off = data_table + read_u32(entry + 12);
		const char *key;
		uint32_t key_max;

		if (key_off >= size)
			continue;
		if (data_off >= size)
			continue;
		if (data_len > size - data_off)
			data_len = size - data_off;

		key = (const char *)(data + key_off);
		key_max = size - key_off;
		if (strnlen(key, key_max) == key_max)
			continue;

		if (strcmp(key, "TITLE_ID") == 0)
			copy_string(out->title_id, sizeof(out->title_id),
				    data + data_off, data_len);
		else if (strcmp(key, "APP_VER") == 0)
			copy_string(out->app_ver, sizeof(out->app_ver),
				    data + data_off, data_len);
		else if (strcmp(key, "CATEGORY") == 0)
			copy_string(out->category, sizeof(out->category),
				    data + data_off, data_len);
		else if (strcmp(key, "TITLE") == 0 && out->title[0] == 0)
			copy_string(out->title, sizeof(out->title),
				    data + data_off, data_len);
	}

	return VHDB_SFO_OK;
}

int vhdb_sfo_parse_file(const char *path, vhdb_sfo *out)
{
	FILE *file;
	long length;
	uint8_t *buffer;
	size_t got;
	int rc;

	memset(out, 0, sizeof(*out));

	file = fopen(path, "rb");
	if (!file)
		return VHDB_SFO_ERR_SIZE;
	if (fseek(file, 0, SEEK_END) != 0) {
		fclose(file);
		return VHDB_SFO_ERR_SIZE;
	}
	length = ftell(file);
	if (length < 20 || length > 1024 * 1024) {
		fclose(file);
		return VHDB_SFO_ERR_SIZE;
	}
	rewind(file);

	buffer = (uint8_t *)malloc((size_t)length);
	if (!buffer) {
		fclose(file);
		return VHDB_SFO_ERR_SIZE;
	}
	got = fread(buffer, 1, (size_t)length, file);
	fclose(file);
	if (got != (size_t)length) {
		free(buffer);
		return VHDB_SFO_ERR_SIZE;
	}

	rc = vhdb_sfo_parse(buffer, (uint32_t)length, out);
	free(buffer);
	return rc;
}
