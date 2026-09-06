#include "vhdb_vitascan.h"
#include "vhdb_md5.h"
#include "vhdb_sfo.h"

#include <psp2/io/dirent.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>

#include <stdio.h>
#include <string.h>

#define APP_DIR "ux0:app"

int vhdb_app_installed(const char *titleid)
{
	char path[128];
	SceIoStat stat;

	if (!titleid || !titleid[0])
		return 0;

	snprintf(path, sizeof(path), "%s/%s/eboot.bin", APP_DIR, titleid);
	memset(&stat, 0, sizeof(stat));
	return sceIoGetstat(path, &stat) >= 0;
}

int vhdb_prune_installed(vhdb_installed_list *installed)
{
	int removed = 0;
	int i = 0;

	while (i < installed->count) {
		vhdb_installed *entry = &installed->items[i];

		if (entry->titleid[0] && !vhdb_app_installed(entry->titleid)) {
			vhdb_installed_remove(installed, entry->id);
			removed++;
			continue;
		}
		i++;
	}
	return removed;
}

static void hash_files(const vhdb_db *db, const vhdb_record *rec,
		       const char *titleid, vhdb_installed *entry)
{
	char path[256];

	(void)db;

	if (rec->flags & VHDB_FLAG_HAS_EBOOT) {
		snprintf(path, sizeof(path), "%s/%s/eboot.bin", APP_DIR, titleid);
		if (vhdb_md5_file(path, entry->eboot))
			entry->has_eboot = 1;
	}

	if (rec->flags & VHDB_FLAG_HAS_AUX) {
		snprintf(path, sizeof(path), "%s/%s/%s", APP_DIR, titleid,
			 vhdb_aux_path(rec->aux_kind));
		if (vhdb_md5_file(path, entry->aux))
			entry->has_aux = 1;
	}
}

int vhdb_vita_scan(const vhdb_db *db, vhdb_installed_list *installed,
		   vhdb_scan_progress progress, void *user)
{
	SceUID dir;
	SceIoDirent found;
	int matched = 0;
	int seen = 0;

	vhdb_prune_installed(installed);

	dir = sceIoDopen(APP_DIR);
	if (dir < 0)
		return -1;

	memset(&found, 0, sizeof(found));
	while (sceIoDread(dir, &found) > 0) {
		char path[256];
		vhdb_sfo sfo;
		const vhdb_record *rec;
		vhdb_installed *existing;
		vhdb_installed entry;
		const char *titleid;

		if (!SCE_S_ISDIR(found.d_stat.st_mode) || found.d_name[0] == '.') {
			memset(&found, 0, sizeof(found));
			continue;
		}

		seen++;
		titleid = found.d_name;

		snprintf(path, sizeof(path), "%s/%s/sce_sys/param.sfo", APP_DIR,
			 titleid);
		if (vhdb_sfo_parse_file(path, &sfo) == VHDB_SFO_OK &&
		    sfo.title_id[0])
			titleid = sfo.title_id;

		rec = vhdb_find_titleid(db, titleid);
		if (!rec) {
			memset(&found, 0, sizeof(found));
			continue;
		}

		matched++;
		if (progress)
			progress(matched, vhdb_str(db, rec->name), user);

		existing = vhdb_installed_find_titleid(installed, titleid);
		if (existing) {
			existing->from_console = 1;
			if (!existing->version[0])
				snprintf(existing->version,
					 sizeof(existing->version), "%s",
					 sfo.app_ver);
			existing->has_eboot = 0;
			existing->has_aux = 0;
			hash_files(db, rec, found.d_name, existing);
			memset(&found, 0, sizeof(found));
			continue;
		}

		memset(&entry, 0, sizeof(entry));
		entry.id = rec->id;
		snprintf(entry.titleid, sizeof(entry.titleid), "%.12s", titleid);
		snprintf(entry.version, sizeof(entry.version), "%s", sfo.app_ver);
		entry.from_console = 1;
		hash_files(db, rec, found.d_name, &entry);
		vhdb_installed_set(installed, &entry);

		memset(&found, 0, sizeof(found));
	}

	sceIoDclose(dir);
	return matched;
}
