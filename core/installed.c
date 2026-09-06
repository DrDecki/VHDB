#include "vhdb_installed.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int grow(vhdb_installed_list *list)
{
	int capacity = list->capacity ? list->capacity * 2 : 32;
	vhdb_installed *items;

	items = (vhdb_installed *)realloc(list->items,
					  (size_t)capacity * sizeof(*items));
	if (!items)
		return 0;
	list->items = items;
	list->capacity = capacity;
	return 1;
}

static int hex_value(char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}

static int parse_hash(const char *text, uint8_t out[16])
{
	int i;

	if (strlen(text) != 32)
		return 0;
	for (i = 0; i < 16; i++) {
		int hi = hex_value(text[i * 2]);
		int lo = hex_value(text[i * 2 + 1]);
		if (hi < 0 || lo < 0)
			return 0;
		out[i] = (uint8_t)((hi << 4) | lo);
	}
	return 1;
}

int vhdb_installed_load(vhdb_installed_list *list, const char *path)
{
	char line[512];
	FILE *file;

	memset(list, 0, sizeof(*list));

	file = fopen(path, "r");
	if (!file)
		return 0;

	while (fgets(line, sizeof(line), file)) {
		vhdb_installed entry;
		char *fields[7];
		char *cursor = line;
		int count = 0;

		line[strcspn(line, "\r\n")] = 0;
		if (line[0] == 0 || line[0] == '#')
			continue;

		while (count < 7) {
			fields[count++] = cursor;
			cursor = strchr(cursor, '|');
			if (!cursor)
				break;
			*cursor++ = 0;
		}
		if (count < 4)
			continue;

		memset(&entry, 0, sizeof(entry));
		entry.id = (uint32_t)strtoul(fields[0], NULL, 10);
		snprintf(entry.titleid, sizeof(entry.titleid), "%s", fields[1]);
		snprintf(entry.version, sizeof(entry.version), "%s", fields[2]);
		entry.has_hash = parse_hash(fields[3], entry.hash);
		if (count >= 5)
			entry.from_console = atoi(fields[4]);
		if (count >= 6)
			entry.has_eboot = parse_hash(fields[5], entry.eboot);
		if (count >= 7)
			entry.has_aux = parse_hash(fields[6], entry.aux);

		if (list->count == list->capacity && !grow(list))
			break;
		list->items[list->count++] = entry;
	}

	fclose(file);
	return 1;
}

int vhdb_installed_save(const vhdb_installed_list *list, const char *path)
{
	FILE *file;
	int i, n;

	file = fopen(path, "w");
	if (!file)
		return 0;

	fprintf(file, "# id|titleid|version|md5|from_console|eboot|aux\n");
	for (i = 0; i < list->count; i++) {
		const vhdb_installed *entry = &list->items[i];
		fprintf(file, "%u|%s|%s|", entry->id, entry->titleid,
			entry->version);
		if (entry->has_hash) {
			for (n = 0; n < 16; n++)
				fprintf(file, "%02x", entry->hash[n]);
		}
		fprintf(file, "|%d|", entry->from_console);
		if (entry->has_eboot) {
			for (n = 0; n < 16; n++)
				fprintf(file, "%02x", entry->eboot[n]);
		}
		fprintf(file, "|");
		if (entry->has_aux) {
			for (n = 0; n < 16; n++)
				fprintf(file, "%02x", entry->aux[n]);
		}
		fprintf(file, "\n");
	}

	fclose(file);
	return 1;
}

vhdb_installed *vhdb_installed_find_id(vhdb_installed_list *list, uint32_t id)
{
	int i;

	if (id == 0)
		return NULL;
	for (i = 0; i < list->count; i++) {
		if (list->items[i].id == id)
			return &list->items[i];
	}
	return NULL;
}

vhdb_installed *vhdb_installed_find_titleid(vhdb_installed_list *list,
					    const char *titleid)
{
	int i;

	if (!titleid || !titleid[0])
		return NULL;
	for (i = 0; i < list->count; i++) {
		if (strcmp(list->items[i].titleid, titleid) == 0)
			return &list->items[i];
	}
	return NULL;
}

int vhdb_installed_set(vhdb_installed_list *list, const vhdb_installed *entry)
{
	vhdb_installed *found = vhdb_installed_find_id(list, entry->id);

	if (!found && entry->titleid[0])
		found = vhdb_installed_find_titleid(list, entry->titleid);

	if (found) {
		*found = *entry;
		return 1;
	}
	if (list->count == list->capacity && !grow(list))
		return 0;
	list->items[list->count++] = *entry;
	return 1;
}

int vhdb_installed_remove(vhdb_installed_list *list, uint32_t id)
{
	int i;

	for (i = 0; i < list->count; i++) {
		if (list->items[i].id != id)
			continue;
		if (i + 1 < list->count)
			memmove(&list->items[i], &list->items[i + 1],
				(size_t)(list->count - i - 1) * sizeof(*list->items));
		list->count--;
		return 1;
	}
	return 0;
}

void vhdb_installed_free(vhdb_installed_list *list)
{
	if (list->items)
		free(list->items);
	memset(list, 0, sizeof(*list));
}
