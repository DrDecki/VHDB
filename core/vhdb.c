#include "vhdb.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

uint32_t vhdb_crc32(const uint8_t *data, uint32_t length)
{
	uint32_t crc = 0xFFFFFFFFu;
	uint32_t i;
	int bit;

	for (i = 0; i < length; i++) {
		crc ^= data[i];
		for (bit = 0; bit < 8; bit++) {
			if (crc & 1u)
				crc = (crc >> 1) ^ 0xEDB88320u;
			else
				crc >>= 1;
		}
	}
	return crc ^ 0xFFFFFFFFu;
}

const char *vhdb_error(int code)
{
	switch (code) {
	case VHDB_OK: return "ok";
	case VHDB_ERR_OPEN: return "cannot open file";
	case VHDB_ERR_READ: return "short read";
	case VHDB_ERR_MAGIC: return "not a VHDB file";
	case VHDB_ERR_VERSION: return "unsupported format version";
	case VHDB_ERR_LAYOUT: return "corrupt layout";
	case VHDB_ERR_CRC: return "checksum mismatch";
	case VHDB_ERR_MEMORY: return "out of memory";
	default: return "unknown error";
	}
}

static int vhdb_check_layout(const vhdb_db *db)
{
	const vhdb_header *h = db->header;
	uint32_t need;

	if (h->entry_size != VHDB_RECORD_SIZE)
		return 0;
	if (h->records_off != VHDB_HEADER_SIZE)
		return 0;
	if (h->entry_count == 0)
		return 0;
	if (h->entry_count > 0x00100000u)
		return 0;

	need = h->records_off + h->entry_count * VHDB_RECORD_SIZE;
	if (h->strings_off != need)
		return 0;
	if (h->strings_len == 0)
		return 0;
	if (h->idx_name_off != h->strings_off + h->strings_len)
		return 0;
	if (h->idx_name_off & 3u)
		return 0;
	if (h->idx_date_off != h->idx_name_off + h->entry_count * 4u)
		return 0;
	if (h->idx_date_off + h->entry_count * 4u != db->blob_size)
		return 0;
	if (db->blob[h->strings_off + h->strings_len - 1] != 0)
		return 0;

	return 1;
}

int vhdb_load(vhdb_db *db, const char *path, int verify)
{
	FILE *file;
	long length;
	size_t got;
	uint32_t i;

	memset(db, 0, sizeof(*db));

	file = fopen(path, "rb");
	if (!file)
		return VHDB_ERR_OPEN;

	if (fseek(file, 0, SEEK_END) != 0) {
		fclose(file);
		return VHDB_ERR_READ;
	}
	length = ftell(file);
	if (length < (long)(VHDB_HEADER_SIZE + VHDB_RECORD_SIZE)) {
		fclose(file);
		return VHDB_ERR_READ;
	}
	rewind(file);

	db->blob = (uint8_t *)malloc((size_t)length);
	if (!db->blob) {
		fclose(file);
		return VHDB_ERR_MEMORY;
	}

	got = fread(db->blob, 1, (size_t)length, file);
	fclose(file);
	if (got != (size_t)length) {
		vhdb_free(db);
		return VHDB_ERR_READ;
	}
	db->blob_size = (uint32_t)length;

	db->header = (const vhdb_header *)db->blob;
	if (db->header->magic != VHDB_MAGIC) {
		vhdb_free(db);
		return VHDB_ERR_MAGIC;
	}
	if (db->header->format_version != VHDB_FORMAT_VERSION) {
		vhdb_free(db);
		return VHDB_ERR_VERSION;
	}
	if (!vhdb_check_layout(db)) {
		vhdb_free(db);
		return VHDB_ERR_LAYOUT;
	}

	if (verify) {
		uint32_t crc = vhdb_crc32(db->blob + VHDB_HEADER_SIZE,
					  db->blob_size - VHDB_HEADER_SIZE);
		if (crc != db->header->catalog_hash) {
			vhdb_free(db);
			return VHDB_ERR_CRC;
		}
	}

	db->count = db->header->entry_count;
	db->records = (const vhdb_record *)(db->blob + db->header->records_off);
	db->strings = (const char *)(db->blob + db->header->strings_off);
	db->order_name = (const uint32_t *)(db->blob + db->header->idx_name_off);
	db->order_date = (const uint32_t *)(db->blob + db->header->idx_date_off);

	for (i = 0; i < db->count; i++) {
		if (db->order_name[i] >= db->count || db->order_date[i] >= db->count) {
			vhdb_free(db);
			return VHDB_ERR_LAYOUT;
		}
	}

	return VHDB_OK;
}

void vhdb_free(vhdb_db *db)
{
	if (db->blob)
		free(db->blob);
	memset(db, 0, sizeof(*db));
}

uint32_t vhdb_count(const vhdb_db *db)
{
	return db->count;
}

const vhdb_record *vhdb_at(const vhdb_db *db, uint32_t index)
{
	if (index >= db->count)
		return NULL;
	return &db->records[index];
}

const vhdb_record *vhdb_by_name(const vhdb_db *db, uint32_t rank)
{
	if (rank >= db->count)
		return NULL;
	return &db->records[db->order_name[rank]];
}

const vhdb_record *vhdb_by_date(const vhdb_db *db, uint32_t rank)
{
	if (rank >= db->count)
		return NULL;
	return &db->records[db->order_date[rank]];
}

const char *vhdb_str(const vhdb_db *db, uint32_t offset)
{
	if (!db || !db->header || !db->strings)
		return "";
	if (offset >= db->header->strings_len)
		return "";
	return db->strings + offset;
}

int vhdb_titleid(const vhdb_record *rec, char *out, size_t out_size)
{
	size_t i;

	if (out_size < 13)
		return 0;
	for (i = 0; i < 12; i++) {
		out[i] = rec->titleid[i];
		if (out[i] == 0)
			return 1;
	}
	out[12] = 0;
	return 1;
}

const vhdb_record *vhdb_find_titleid(const vhdb_db *db, const char *titleid)
{
	uint32_t i;
	size_t len;

	if (!titleid || !titleid[0])
		return NULL;
	len = strlen(titleid);
	if (len > 12)
		return NULL;

	for (i = 0; i < db->count; i++) {
		const vhdb_record *rec = &db->records[i];
		if (strncmp(rec->titleid, titleid, 12) == 0) {
			if (len == 12 || rec->titleid[len] == 0)
				return rec;
		}
	}
	return NULL;
}

const char *vhdb_aux_name(uint8_t kind)
{
	switch (kind) {
	case VHDB_AUX_UNITY: return "Unity";
	case VHDB_AUX_GAMEMAKER: return "GameMaker";
	case VHDB_AUX_LPP: return "Lua Player Plus";
	case VHDB_AUX_LIFELUA: return "LifeLua";
	case VHDB_AUX_YOYO: return "YoYo Loader";
	case VHDB_AUX_GODOT: return "Godot";
	default: return "";
	}
}

const char *vhdb_aux_path(uint8_t kind)
{
	switch (kind) {
	case VHDB_AUX_UNITY: return "Media/sharedassets0.assets.resS";
	case VHDB_AUX_GAMEMAKER: return "games/game.win";
	case VHDB_AUX_LPP: return "index.lua";
	case VHDB_AUX_LIFELUA: return "main.lua";
	case VHDB_AUX_YOYO: return "game.apk";
	case VHDB_AUX_GODOT: return "game_data/game.pck";
	default: return "";
	}
}

const char *vhdb_type_name(uint8_t type)
{
	switch (type) {
	case VHDB_TYPE_GAME: return "Games";
	case VHDB_TYPE_PORT: return "Ports";
	case VHDB_TYPE_UTILITY: return "Utilities";
	case VHDB_TYPE_EMULATOR: return "Emulators";
	default: return "Other";
	}
}
