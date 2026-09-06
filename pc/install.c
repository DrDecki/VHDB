#include "vhdb_install.h"
#include "vhdb_md5.h"
#include "vhdb_net.h"
#include "vhdb_status.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void url_basename(const char *url, char *out, size_t size)
{
	const char *slash = strrchr(url, '/');
	const char *name = slash ? slash + 1 : url;
	size_t n = 0;

	while (*name && n + 1 < size) {
		if (name[0] == '%' && name[1] && name[2]) {
			char hex[3];
			hex[0] = name[1];
			hex[1] = name[2];
			hex[2] = 0;
			out[n++] = (char)strtol(hex, NULL, 16);
			name += 3;
			continue;
		}
		if (*name == '?' || *name == '#')
			break;
		out[n++] = *name++;
	}
	out[n] = 0;
	if (n == 0)
		snprintf(out, size, "download.bin");
}

static void escape_path(const char *name, char *out, size_t size)
{
	size_t n = 0;

	while (*name && n + 4 < size) {
		unsigned char c = (unsigned char)*name++;
		if (c == ' ' || c == '#' || c == '?' || c == '%' || c < 32) {
			out[n++] = '%';
			out[n++] = "0123456789ABCDEF"[(c >> 4) & 0xF];
			out[n++] = "0123456789ABCDEF"[c & 0xF];
		} else {
			out[n++] = (char)c;
		}
	}
	out[n] = 0;
}

static int copy_file(const char *from, const char *to)
{
	FILE *in, *out;
	char chunk[32768];
	size_t got;

	in = fopen(from, "rb");
	if (!in)
		return 0;
	out = fopen(to, "wb");
	if (!out) {
		fclose(in);
		return 0;
	}
	while ((got = fread(chunk, 1, sizeof(chunk), in)) > 0) {
		if (fwrite(chunk, 1, got, out) != got) {
			fclose(in);
			fclose(out);
			return 0;
		}
	}
	fclose(in);
	fclose(out);
	return 1;
}

static int send_to_target(const vhdb_config *cfg, const char *folder,
			  const char *local, const char *filename)
{
	char url[1024];
	char escaped[512];
	size_t length;
	char trimmed[192];

	snprintf(trimmed, sizeof(trimmed), "%s", folder);
	length = strlen(trimmed);
	while (length > 0 && trimmed[length - 1] == '/')
		trimmed[--length] = 0;

	if (cfg->target == VHDB_TARGET_FTP) {
		escape_path(filename, escaped, sizeof(escaped));
		snprintf(url, sizeof(url), "ftp://%s:%d/%s/%s", cfg->host,
			 cfg->port, trimmed, escaped);
		printf("  sending to %s:%d /%s\n", cfg->host, cfg->port, trimmed);
		if (vhdb_net_ftp_upload(url, cfg->user, cfg->pass, local, 1) !=
		    VHDB_NET_OK) {
			printf("  transfer failed: %s\n", vhdb_net_last_error());
			printf("  is FTP running in VitaShell, and is the address still %s?\n",
			       cfg->host);
			return 0;
		}
		return 1;
	}

	if (cfg->target == VHDB_TARGET_FOLDER) {
		char destination[1024];
		vhdb_make_dirs(trimmed);
		snprintf(destination, sizeof(destination), "%s/%s", trimmed, filename);
		if (!copy_file(local, destination)) {
			printf("  could not write %s\n", destination);
			return 0;
		}
		printf("  copied to %s\n", destination);
		return 1;
	}

	return 1;
}

int vhdb_pc_install(const vhdb_config *cfg, const vhdb_db *db,
		    const vhdb_record *rec, vhdb_installed_list *installed,
		    int with_data)
{
	char filename[512];
	char local[1024];
	char digest_hex[33];
	uint8_t digest[16];
	const char *url = vhdb_str(db, rec->url);
	const char *folder = NULL;
	int kind = vhdb_install_kind(rec);
	int has_hash = 0;

	if (!url[0]) {
		printf("this entry has no download link\n");
		return 0;
	}

	vhdb_make_dirs(cfg->keep);
	url_basename(url, filename, sizeof(filename));
	snprintf(local, sizeof(local), "%s/%s", cfg->keep, filename);

	printf("%s %s\n", vhdb_str(db, rec->name), vhdb_str(db, rec->version));
	printf("  downloading %s\n", filename);
	if (vhdb_net_download(url, local, 1) != VHDB_NET_OK) {
		printf("  download failed: %s\n", vhdb_net_last_error());
		return 0;
	}

	{
		static const uint8_t zero[16] = {0};
		if (memcmp(rec->hash, zero, 16) != 0) {
			if (!vhdb_md5_file(local, digest)) {
				printf("  cannot read the file back\n");
				return 0;
			}
			if (memcmp(digest, rec->hash, 16) != 0) {
				vhdb_md5_hex(digest, digest_hex);
				printf("  checksum does not match the catalogue\n");
				printf("  got %s, keeping nothing\n", digest_hex);
				remove(local);
				return 0;
			}
			printf("  checksum verified\n");
			has_hash = 1;
		} else if (vhdb_md5_file(local, digest)) {
			has_hash = 1;
			printf("  no checksum in the catalogue, recorded ours\n");
		}
	}

	if (kind == VHDB_INSTALL_MANUAL || kind == VHDB_INSTALL_PC) {
		printf("  kept at %s\n", local);
		if (kind == VHDB_INSTALL_MANUAL)
			printf("  copy it to ur0:tai yourself and add it to config.txt\n");
		return 1;
	}

	folder = (kind == VHDB_INSTALL_PSP) ? cfg->pspemu : cfg->vpk;

	if (cfg->target == VHDB_TARGET_NONE)
		printf("  kept at %s, no target configured\n", local);
	else if (!send_to_target(cfg, folder, local, filename))
		return 0;

	if (with_data && vhdb_has_data_file(rec)) {
		char data_name[512];
		char data_local[1024];
		const char *data_url = vhdb_str(db, rec->data_url);

		url_basename(data_url, data_name, sizeof(data_name));
		snprintf(data_local, sizeof(data_local), "%s/%s", cfg->keep, data_name);
		printf("  downloading data file %s\n", data_name);
		if (vhdb_net_download(data_url, data_local, 1) != VHDB_NET_OK)
			printf("  data file failed: %s\n", vhdb_net_last_error());
		else
			printf("  data file kept at %s, unpack it into %s\n",
			       data_local, cfg->data);
	} else if (vhdb_has_data_file(rec)) {
		printf("  this one needs a data file, add --data to fetch it\n");
	}

	{
		vhdb_installed entry;

		memset(&entry, 0, sizeof(entry));
		entry.id = rec->id;
		vhdb_titleid(rec, entry.titleid, sizeof(entry.titleid));
		snprintf(entry.version, sizeof(entry.version), "%s",
			 vhdb_str(db, rec->version));
		if (has_hash) {
			memcpy(entry.hash, digest, 16);
			entry.has_hash = 1;
		}
		vhdb_installed_set(installed, &entry);
	}

	printf("  done\n");
	return 1;
}
