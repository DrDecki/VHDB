#ifndef VHDB_H
#define VHDB_H

#include <stdint.h>
#include <stddef.h>

#define VHDB_MAGIC 0x42444856u
#define VHDB_FORMAT_VERSION 2u
#define VHDB_HEADER_SIZE 64u
#define VHDB_RECORD_SIZE 160u

#define VHDB_FLAG_TROPHIES 0x01u
#define VHDB_FLAG_HAS_DATA 0x02u
#define VHDB_FLAG_HAS_SHOTS 0x04u
#define VHDB_FLAG_HAS_TRAILER 0x08u
#define VHDB_FLAG_HASH2 0x10u
#define VHDB_FLAG_HAS_EBOOT 0x20u
#define VHDB_FLAG_HAS_AUX 0x40u
#define VHDB_FLAG_ROLLING 0x80u

#define VHDB_AUX_NONE 0u
#define VHDB_AUX_UNITY 1u
#define VHDB_AUX_GAMEMAKER 2u
#define VHDB_AUX_LPP 3u
#define VHDB_AUX_LIFELUA 4u
#define VHDB_AUX_YOYO 5u
#define VHDB_AUX_GODOT 6u

#define VHDB_PLATFORM_VITA 0u
#define VHDB_PLATFORM_PSP 1u
#define VHDB_PLATFORM_PLUGIN 2u
#define VHDB_PLATFORM_TOOL 3u

#define VHDB_TYPE_GAME 1u
#define VHDB_TYPE_PORT 2u
#define VHDB_TYPE_UTILITY 4u
#define VHDB_TYPE_EMULATOR 5u

#define VHDB_OK 0
#define VHDB_ERR_OPEN -1
#define VHDB_ERR_READ -2
#define VHDB_ERR_MAGIC -3
#define VHDB_ERR_VERSION -4
#define VHDB_ERR_LAYOUT -5
#define VHDB_ERR_CRC -6
#define VHDB_ERR_MEMORY -7

typedef struct {
	uint32_t magic;
	uint32_t format_version;
	uint32_t built;
	uint32_t entry_count;
	uint32_t entry_size;
	uint32_t records_off;
	uint32_t strings_off;
	uint32_t strings_len;
	uint32_t idx_name_off;
	uint32_t idx_date_off;
	uint32_t catalog_hash;
	uint32_t reserved[5];
} vhdb_header;

typedef struct {
	uint32_t name;
	uint32_t author;
	uint32_t version;
	uint32_t icon;
	uint32_t description;
	uint32_t long_description;
	uint32_t changelog;
	uint32_t requirements;
	uint32_t needs;
	uint32_t url;
	uint32_t data_url;
	uint32_t source;
	uint32_t release_page;
	uint32_t trailer;
	uint32_t screenshots;
	uint32_t tags;
	uint32_t size;
	uint32_t data_size;
	uint32_t date;
	uint32_t id;
	char titleid[12];
	uint8_t hash[16];
	uint8_t hash2[16];
	uint8_t eboot[16];
	uint8_t aux[16];
	uint8_t type;
	uint8_t flags;
	uint8_t platform;
	uint8_t aux_kind;
} vhdb_record;

typedef struct {
	uint8_t *blob;
	uint32_t blob_size;
	const vhdb_header *header;
	const vhdb_record *records;
	const char *strings;
	const uint32_t *order_name;
	const uint32_t *order_date;
	uint32_t count;
} vhdb_db;

int vhdb_load(vhdb_db *db, const char *path, int verify);
void vhdb_free(vhdb_db *db);

const char *vhdb_error(int code);

uint32_t vhdb_count(const vhdb_db *db);
const vhdb_record *vhdb_at(const vhdb_db *db, uint32_t index);
const vhdb_record *vhdb_by_name(const vhdb_db *db, uint32_t rank);
const vhdb_record *vhdb_by_date(const vhdb_db *db, uint32_t rank);
const char *vhdb_str(const vhdb_db *db, uint32_t offset);
int vhdb_titleid(const vhdb_record *rec, char *out, size_t out_size);
const vhdb_record *vhdb_find_titleid(const vhdb_db *db, const char *titleid);
const char *vhdb_type_name(uint8_t type);
const char *vhdb_aux_name(uint8_t kind);
const char *vhdb_aux_path(uint8_t kind);
uint32_t vhdb_crc32(const uint8_t *data, uint32_t length);

#endif
