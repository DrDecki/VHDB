#include "vhdb.h"
#include "vhdb_config.h"
#include "vhdb_installed.h"
#include "vhdb_net.h"
#include "vhdb_sfo.h"
#include "vhdb_status.h"

#include <stdio.h>
#include <string.h>

#define VHDB_SCAN_LIST_SIZE 262144
#define VHDB_SCAN_SFO_SIZE 65536

static int is_directory_line(const char *line)
{
	return line[0] == 'd';
}

static const char *last_field(const char *line, size_t length)
{
	size_t i = length;

	while (i > 0 && (line[i - 1] == ' ' || line[i - 1] == '\t'))
		i--;
	while (i > 0 && line[i - 1] != ' ' && line[i - 1] != '\t')
		i--;
	return line + i;
}

static void hash_app(const vhdb_config *cfg, const vhdb_record *rec,
		     const char *titleid, vhdb_installed *entry)
{
	char url[512];

	if (rec->flags & VHDB_FLAG_HAS_EBOOT) {
		snprintf(url, sizeof(url), "ftp://%s:%d/ux0:/app/%s/eboot.bin",
			 cfg->host, cfg->port, titleid);
		if (vhdb_net_ftp_md5(url, cfg->user, cfg->pass, entry->eboot,
				     NULL) == VHDB_NET_OK)
			entry->has_eboot = 1;
	}

	if (rec->flags & VHDB_FLAG_HAS_AUX) {
		snprintf(url, sizeof(url), "ftp://%s:%d/ux0:/app/%s/%s", cfg->host,
			 cfg->port, titleid, vhdb_aux_path(rec->aux_kind));
		if (vhdb_net_ftp_md5(url, cfg->user, cfg->pass, entry->aux,
				     NULL) == VHDB_NET_OK)
			entry->has_aux = 1;
	}
}

int vhdb_pc_scan(const vhdb_config *cfg, const vhdb_db *db,
		 vhdb_installed_list *installed, int rehash)
{
	static char listing[VHDB_SCAN_LIST_SIZE];
	static uint8_t sfo_data[VHDB_SCAN_SFO_SIZE];
	char url[512];
	char *cursor;
	int found = 0, matched = 0, unknown = 0, retail = 0, hashed = 0;

	if (cfg->target != VHDB_TARGET_FTP) {
		printf("scanning needs an ftp target, run: vhdb setup\n");
		return 0;
	}

	snprintf(url, sizeof(url), "ftp://%s:%d/ux0:/app/", cfg->host, cfg->port);
	printf("listing ux0:/app on %s\n", cfg->host);

	if (vhdb_net_ftp_list(url, cfg->user, cfg->pass, listing,
			      sizeof(listing)) != VHDB_NET_OK) {
		printf("cannot list the console: %s\n", vhdb_net_last_error());
		printf("is FTP running in VitaShell, and is the address still %s?\n",
		       cfg->host);
		return 0;
	}

	cursor = listing;
	while (*cursor) {
		char *line = cursor;
		char *end = strchr(cursor, '\n');
		const char *name;
		size_t length;
		vhdb_sfo sfo;
		size_t got = 0;

		if (end) {
			*end = 0;
			cursor = end + 1;
		} else {
			cursor = line + strlen(line);
		}

		length = strlen(line);
		while (length > 0 && (line[length - 1] == '\r' ||
				      line[length - 1] == ' '))
			line[--length] = 0;
		if (length == 0 || !is_directory_line(line))
			continue;

		name = last_field(line, length);
		if (!name[0] || strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
			continue;

		found++;

		snprintf(url, sizeof(url), "ftp://%s:%d/ux0:/app/%s/sce_sys/param.sfo",
			 cfg->host, cfg->port, name);
		if (vhdb_net_ftp_get(url, cfg->user, cfg->pass, sfo_data,
				     sizeof(sfo_data), &got) != VHDB_NET_OK)
			continue;
		if (vhdb_sfo_parse(sfo_data, (uint32_t)got, &sfo) != VHDB_SFO_OK)
			continue;
		if (!sfo.title_id[0])
			snprintf(sfo.title_id, sizeof(sfo.title_id), "%s", name);

		{
			const vhdb_record *rec = vhdb_find_titleid(db, sfo.title_id);
			vhdb_installed *existing =
				vhdb_installed_find_titleid(installed, sfo.title_id);
			vhdb_installed entry;

			if (!rec) {
				if (strncmp(sfo.title_id, "PCS", 3) == 0 ||
				    strncmp(sfo.title_id, "NPX", 3) == 0) {
					retail++;
					continue;
				}
				unknown++;
				printf("  %-12s %-38s not in the catalog\n",
				       sfo.title_id, sfo.title);
				continue;
			}
			matched++;
			printf("  %-12s %s\n", sfo.title_id, sfo.title);

			if (existing) {
				existing->from_console = 1;
				if (!existing->has_hash)
					snprintf(existing->version,
						 sizeof(existing->version), "%s",
						 sfo.app_ver);
				if (rehash || (!existing->has_eboot && !existing->has_aux))
					hash_app(cfg, rec, sfo.title_id, existing);
				if (existing->has_eboot || existing->has_aux)
					hashed++;
				continue;
			}

			memset(&entry, 0, sizeof(entry));
			entry.id = rec->id;
			snprintf(entry.titleid, sizeof(entry.titleid), "%.12s",
				 sfo.title_id);
			snprintf(entry.version, sizeof(entry.version), "%s",
				 sfo.app_ver);
			entry.from_console = 1;
			hash_app(cfg, rec, sfo.title_id, &entry);
			if (entry.has_eboot || entry.has_aux)
				hashed++;
			vhdb_installed_set(installed, &entry);
		}
	}

	printf("\n%d apps on the console, %d in the catalog, %d retail, %d unknown\n",
	       found, matched, retail, unknown);
	printf("%d checked by file contents\n", hashed);
	return 1;
}
