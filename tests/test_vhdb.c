#include "vhdb.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;

static void check(int condition, const char *what)
{
	if (!condition) {
		printf("FAIL %s\n", what);
		failures++;
	}
}

static void dump(const vhdb_db *db, const vhdb_record *rec)
{
	char titleid[13];

	vhdb_titleid(rec, titleid, sizeof(titleid));
	printf("  %-28s %-10s %-16s %-10s %-9s %8u B  %s\n",
	       vhdb_str(db, rec->name),
	       vhdb_str(db, rec->version),
	       vhdb_str(db, rec->author),
	       titleid,
	       vhdb_type_name(rec->type),
	       rec->size,
	       vhdb_str(db, rec->url));
}

int main(int argc, char **argv)
{
	const char *path = argc > 1 ? argv[1] : "vhdb.bin";
	vhdb_db db;
	uint32_t i, limit;
	int rc;

	check(sizeof(vhdb_header) == VHDB_HEADER_SIZE, "header is 64 bytes");
	check(sizeof(vhdb_record) == VHDB_RECORD_SIZE, "record is 160 bytes");

	rc = vhdb_load(&db, path, 1);
	if (rc != VHDB_OK) {
		printf("load failed: %s\n", vhdb_error(rc));
		return 1;
	}

	printf("file         %s\n", path);
	printf("built        %u\n", db.header->built);
	printf("entries      %u\n", vhdb_count(&db));
	printf("strings      %u bytes\n", db.header->strings_len);
	printf("catalog_hash %08x verified\n", db.header->catalog_hash);
	printf("\n");

	check(vhdb_str(&db, 0)[0] == 0, "offset 0 is the empty string");
	check(vhdb_at(&db, vhdb_count(&db)) == NULL, "out of range index is null");

	for (i = 1; i < vhdb_count(&db); i++) {
		const vhdb_record *a = vhdb_by_date(&db, i - 1);
		const vhdb_record *b = vhdb_by_date(&db, i);
		if (a->date < b->date) {
			check(0, "date index is descending");
			break;
		}
	}

	for (i = 1; i < vhdb_count(&db); i++) {
		const char *a = vhdb_str(&db, vhdb_by_name(&db, i - 1)->name);
		const char *b = vhdb_str(&db, vhdb_by_name(&db, i)->name);
		if (strcasecmp(a, b) > 0) {
			printf("FAIL name index at %u: %s before %s\n", i, a, b);
			failures++;
			break;
		}
	}

	limit = vhdb_count(&db) < 8 ? vhdb_count(&db) : 8;

	printf("newest first\n");
	for (i = 0; i < limit; i++)
		dump(&db, vhdb_by_date(&db, i));

	printf("\nby name\n");
	for (i = 0; i < limit; i++)
		dump(&db, vhdb_by_name(&db, i));

	{
		const vhdb_record *rec = vhdb_by_date(&db, 0);
		char titleid[13];
		vhdb_titleid(rec, titleid, sizeof(titleid));
		check(vhdb_find_titleid(&db, titleid) != NULL, "titleid lookup finds an entry");
		check(vhdb_find_titleid(&db, "NOPE00000") == NULL, "unknown titleid is null");
	}

	{
		uint32_t games = 0, ports = 0, utils = 0, emus = 0, other = 0;
		uint32_t needs_data = 0, trophies = 0, has_req = 0;
		for (i = 0; i < vhdb_count(&db); i++) {
			const vhdb_record *rec = vhdb_at(&db, i);
			if (rec->platform != VHDB_PLATFORM_VITA)
				continue;
			switch (rec->type) {
			case VHDB_TYPE_GAME: games++; break;
			case VHDB_TYPE_PORT: ports++; break;
			case VHDB_TYPE_UTILITY: utils++; break;
			case VHDB_TYPE_EMULATOR: emus++; break;
			default: other++; break;
			}
			if (rec->flags & VHDB_FLAG_HAS_DATA)
				needs_data++;
			if (rec->flags & VHDB_FLAG_TROPHIES)
				trophies++;
			if (vhdb_str(&db, rec->requirements)[0])
				has_req++;
		}
		printf("\nvita  games %u  ports %u  utilities %u  emulators %u  other %u\n",
		       games, ports, utils, emus, other);
		printf("vita  with data file %u  with trophies %u  with requirements %u\n",
		       needs_data, trophies, has_req);
	}

	{
		uint32_t counts[4] = {0, 0, 0, 0};
		for (i = 0; i < vhdb_count(&db); i++) {
			const vhdb_record *rec = vhdb_at(&db, i);
			if (rec->platform < 4)
				counts[rec->platform]++;
		}
		printf("platforms  vita %u  psp %u  plugins %u  tools %u\n",
		       counts[0], counts[1], counts[2], counts[3]);
	}

	vhdb_free(&db);

	printf("\n%s\n", failures == 0 ? "all checks passed" : "checks failed");
	return failures == 0 ? 0 : 1;
}
