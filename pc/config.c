#include "vhdb_config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#define VHDB_DEFAULT_URL "https://github.com/DrDecki/VHDB/releases/download/catalog/vhdb.bin"

static const char *home_dir(void)
{
	const char *home = getenv("HOME");
	return home && home[0] ? home : ".";
}

int vhdb_config_dir(char *out, size_t size)
{
	const char *xdg = getenv("XDG_CONFIG_HOME");

	if (xdg && xdg[0])
		return snprintf(out, size, "%s/vhdb", xdg) < (int)size;
	return snprintf(out, size, "%s/.config/vhdb", home_dir()) < (int)size;
}

int vhdb_config_file(char *out, size_t size)
{
	char dir[256];

	if (!vhdb_config_dir(dir, sizeof(dir)))
		return 0;
	return snprintf(out, size, "%s/config.txt", dir) < (int)size;
}

int vhdb_catalog_file(char *out, size_t size)
{
	char dir[256];

	if (!vhdb_config_dir(dir, sizeof(dir)))
		return 0;
	return snprintf(out, size, "%s/vhdb.bin", dir) < (int)size;
}

int vhdb_make_dirs(const char *path)
{
	char work[512];
	size_t i, len;

	len = strlen(path);
	if (len == 0 || len >= sizeof(work))
		return 0;
	memcpy(work, path, len + 1);

	for (i = 1; i < len; i++) {
		if (work[i] != '/')
			continue;
		work[i] = 0;
		mkdir(work, 0755);
		work[i] = '/';
	}
	mkdir(work, 0755);
	return 1;
}

void vhdb_config_defaults(vhdb_config *cfg)
{
	memset(cfg, 0, sizeof(*cfg));
	snprintf(cfg->catalog_url, sizeof(cfg->catalog_url), "%s", VHDB_DEFAULT_URL);
	cfg->target = VHDB_TARGET_NONE;
	cfg->port = 1337;
	snprintf(cfg->vpk, sizeof(cfg->vpk), "ux0:/VPK");
	snprintf(cfg->data, sizeof(cfg->data), "ux0:/data");
	snprintf(cfg->pspemu, sizeof(cfg->pspemu), "ux0:/pspemu/PSP/GAME");
	snprintf(cfg->keep, sizeof(cfg->keep), "%s/vhdb-downloads", home_dir());
}

const char *vhdb_target_name(int target)
{
	switch (target) {
	case VHDB_TARGET_FTP: return "ftp";
	case VHDB_TARGET_FOLDER: return "folder";
	default: return "none";
	}
}

static void trim(char *s)
{
	size_t len = strlen(s);

	while (len > 0 && (s[len - 1] == '\n' || s[len - 1] == '\r' ||
			   s[len - 1] == ' ' || s[len - 1] == '\t'))
		s[--len] = 0;
}

static void assign(vhdb_config *cfg, const char *key, const char *value)
{
	if (strcmp(key, "url") == 0)
		snprintf(cfg->catalog_url, sizeof(cfg->catalog_url), "%s", value);
	else if (strcmp(key, "kind") == 0) {
		if (strcmp(value, "ftp") == 0)
			cfg->target = VHDB_TARGET_FTP;
		else if (strcmp(value, "folder") == 0)
			cfg->target = VHDB_TARGET_FOLDER;
		else
			cfg->target = VHDB_TARGET_NONE;
	} else if (strcmp(key, "host") == 0)
		snprintf(cfg->host, sizeof(cfg->host), "%s", value);
	else if (strcmp(key, "port") == 0)
		cfg->port = atoi(value);
	else if (strcmp(key, "user") == 0)
		snprintf(cfg->user, sizeof(cfg->user), "%s", value);
	else if (strcmp(key, "pass") == 0)
		snprintf(cfg->pass, sizeof(cfg->pass), "%s", value);
	else if (strcmp(key, "vpk") == 0)
		snprintf(cfg->vpk, sizeof(cfg->vpk), "%s", value);
	else if (strcmp(key, "data") == 0)
		snprintf(cfg->data, sizeof(cfg->data), "%s", value);
	else if (strcmp(key, "pspemu") == 0)
		snprintf(cfg->pspemu, sizeof(cfg->pspemu), "%s", value);
	else if (strcmp(key, "keep") == 0)
		snprintf(cfg->keep, sizeof(cfg->keep), "%s", value);
}

int vhdb_config_load(vhdb_config *cfg)
{
	char path[512];
	char line[640];
	FILE *file;

	vhdb_config_defaults(cfg);

	if (!vhdb_config_file(path, sizeof(path)))
		return 0;
	file = fopen(path, "r");
	if (!file)
		return 0;

	while (fgets(line, sizeof(line), file)) {
		char *equals, *key, *value;

		trim(line);
		if (line[0] == 0 || line[0] == '#' || line[0] == '[')
			continue;
		equals = strchr(line, '=');
		if (!equals)
			continue;
		*equals = 0;
		key = line;
		value = equals + 1;
		while (*key == ' ' || *key == '\t')
			key++;
		trim(key);
		while (*value == ' ' || *value == '\t')
			value++;
		assign(cfg, key, value);
	}

	fclose(file);
	return 1;
}

int vhdb_config_save(const vhdb_config *cfg)
{
	char dir[256];
	char path[512];
	FILE *file;

	if (!vhdb_config_dir(dir, sizeof(dir)))
		return 0;
	vhdb_make_dirs(dir);
	if (!vhdb_config_file(path, sizeof(path)))
		return 0;

	file = fopen(path, "w");
	if (!file)
		return 0;

	fprintf(file, "url = %s\n\n[target]\n", cfg->catalog_url);
	fprintf(file, "kind = %s\n", vhdb_target_name(cfg->target));
	fprintf(file, "host = %s\n", cfg->host);
	fprintf(file, "port = %d\n", cfg->port);
	fprintf(file, "user = %s\n", cfg->user);
	fprintf(file, "pass = %s\n", cfg->pass);
	fprintf(file, "vpk = %s\n", cfg->vpk);
	fprintf(file, "data = %s\n", cfg->data);
	fprintf(file, "pspemu = %s\n", cfg->pspemu);
	fprintf(file, "keep = %s\n", cfg->keep);
	fclose(file);
	return 1;
}
