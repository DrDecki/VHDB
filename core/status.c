#include "vhdb_status.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

static const char *skip_prefix(const char *s)
{
	while (*s == ' ' || *s == '\t')
		s++;
	if (s[0] == 'v' || s[0] == 'V') {
		const char *p = s + 1;
		if (*p == '.')
			p++;
		if (isdigit((unsigned char)*p))
			return p;
	}
	return s;
}

static int next_token(const char **cursor, unsigned long *number, char *word,
		      size_t word_size)
{
	const char *s = *cursor;
	size_t n = 0;

	while (*s && !isalnum((unsigned char)*s))
		s++;
	if (!*s) {
		*cursor = s;
		return 0;
	}

	if (isdigit((unsigned char)*s)) {
		unsigned long value = 0;
		while (isdigit((unsigned char)*s)) {
			if (value < 100000000UL)
				value = value * 10 + (unsigned long)(*s - '0');
			s++;
		}
		*number = value;
		*cursor = s;
		return 1;
	}

	while (isalpha((unsigned char)*s)) {
		if (n + 1 < word_size)
			word[n++] = (char)tolower((unsigned char)*s);
		s++;
	}
	word[n] = 0;
	*cursor = s;
	return 2;
}

static int is_blank_version(const char *s)
{
	while (*s) {
		if (isalpha((unsigned char)*s) && *s != 'v' && *s != 'V')
			return 0;
		if (isdigit((unsigned char)*s) && *s != '0')
			return 0;
		s++;
	}
	return 1;
}

int vhdb_version_compare(const char *installed, const char *catalogue)
{
	const char *a, *b;
	int saw_number = 0;

	if (!installed || !catalogue)
		return VHDB_VER_UNKNOWN;
	if (!installed[0] || !catalogue[0])
		return VHDB_VER_UNKNOWN;
	if (is_blank_version(installed) || is_blank_version(catalogue))
		return VHDB_VER_UNKNOWN;

	if (strcmp(installed, catalogue) == 0)
		return VHDB_VER_SAME;

	a = skip_prefix(installed);
	b = skip_prefix(catalogue);

	for (;;) {
		unsigned long na = 0, nb = 0;
		char wa[32], wb[32];
		int ta, tb;

		ta = next_token(&a, &na, wa, sizeof(wa));
		tb = next_token(&b, &nb, wb, sizeof(wb));

		if (ta == 0 && tb == 0)
			return saw_number ? VHDB_VER_SAME : VHDB_VER_UNKNOWN;

		if (ta == 0) {
			if (tb == 1)
				return nb > 0 ? VHDB_VER_NEWER : VHDB_VER_SAME;
			return VHDB_VER_UNKNOWN;
		}
		if (tb == 0) {
			if (ta == 1)
				return na > 0 ? VHDB_VER_OLDER : VHDB_VER_SAME;
			return VHDB_VER_UNKNOWN;
		}

		if (ta != tb)
			return VHDB_VER_UNKNOWN;

		if (ta == 1) {
			saw_number = 1;
			if (na < nb)
				return VHDB_VER_NEWER;
			if (na > nb)
				return VHDB_VER_OLDER;
			continue;
		}

		if (strcmp(wa, wb) != 0)
			return VHDB_VER_UNKNOWN;
	}
}

const char *vhdb_state_name(int state)
{
	switch (state) {
	case VHDB_STATE_NOT_INSTALLED: return "NOT INSTALLED";
	case VHDB_STATE_INSTALLED: return "INSTALLED";
	case VHDB_STATE_UPDATE: return "UPDATE";
	case VHDB_STATE_UNKNOWN_VERSION: return "UNKNOWN VERSION";
	case VHDB_STATE_ROLLING: return "ROLLING BUILD";
	default: return "UNKNOWN";
	}
}

static int kind_from_word(const char *word)
{
	if (strcmp(word, "gamefiles") == 0)
		return VHDB_NEED_KIND_GAMEFILES;
	if (strcmp(word, "plugin") == 0)
		return VHDB_NEED_KIND_PLUGIN;
	if (strcmp(word, "datafile") == 0)
		return VHDB_NEED_KIND_DATAFILE;
	if (strcmp(word, "firmware") == 0)
		return VHDB_NEED_KIND_FIRMWARE;
	return VHDB_NEED_KIND_OTHER;
}

static const char *entry_at(const char *needs, int index)
{
	const char *p = needs;
	int seen = 0;

	if (!needs || !needs[0])
		return NULL;

	while (seen < index) {
		while (*p && *p != ';')
			p++;
		if (!*p)
			return NULL;
		p++;
		seen++;
	}
	if (!*p)
		return NULL;
	return p;
}

static void copy_field(char *dst, size_t dst_size, const char *start,
		       const char *end)
{
	size_t n = 0;

	while (start < end && n + 1 < dst_size)
		dst[n++] = *start++;
	dst[n] = 0;
}

int vhdb_needs_count(const char *needs)
{
	int count = 1;
	const char *p = needs;

	if (!needs || !needs[0])
		return 0;
	while (*p) {
		if (*p == ';')
			count++;
		p++;
	}
	if (p > needs && p[-1] == ';')
		count--;
	return count;
}

int vhdb_needs_get(const char *needs, int index, vhdb_need *out)
{
	const char *start, *p, *field_start;
	char kind[32];
	int field = 0;

	memset(out, 0, sizeof(*out));
	out->state = VHDB_NEED_UNCHECKED;

	start = entry_at(needs, index);
	if (!start)
		return 0;

	p = start;
	field_start = start;
	kind[0] = 0;

	for (;;) {
		if (*p == '|' || *p == ';' || *p == 0) {
			if (field == 0)
				copy_field(kind, sizeof(kind), field_start, p);
			else if (field == 1)
				copy_field(out->path, sizeof(out->path),
					   field_start, p);
			else if (field == 2)
				copy_field(out->text, sizeof(out->text),
					   field_start, p);
			field++;
			if (*p != '|')
				break;
			field_start = p + 1;
		}
		p++;
	}

	out->kind = kind_from_word(kind);
	if (!out->text[0] && kind[0])
		copy_field(out->text, sizeof(out->text), kind,
			   kind + strlen(kind));
	return 1;
}

int vhdb_needs_check(const char *needs, int index, vhdb_need *out,
		     vhdb_path_exists_fn exists, void *user)
{
	if (!vhdb_needs_get(needs, index, out))
		return 0;

	if (out->path[0] && exists)
		out->state = exists(out->path, user) ? VHDB_NEED_MET
						    : VHDB_NEED_MISSING;
	else
		out->state = VHDB_NEED_UNCHECKED;

	return 1;
}

void vhdb_status_of(const vhdb_db *db, const vhdb_record *rec,
		    const vhdb_installed *installed, vhdb_status *out,
		    vhdb_path_exists_fn exists, void *user)
{
	const char *needs;
	int total, i;

	memset(out, 0, sizeof(*out));

	if (!installed ||
	    (!installed->version[0] && !installed->has_eboot && !installed->has_aux)) {
		out->state = VHDB_STATE_NOT_INSTALLED;
	} else if (rec->flags & VHDB_FLAG_ROLLING) {
		out->state = VHDB_STATE_ROLLING;
	} else if (installed->has_hash &&
		   memcmp(installed->hash, rec->hash, 16) == 0) {
		out->state = VHDB_STATE_INSTALLED;
		out->trusted = 1;
	} else if (installed->has_aux && (rec->flags & VHDB_FLAG_HAS_AUX)) {
		int same = memcmp(installed->aux, rec->aux, 16) == 0;

		if (same && installed->has_eboot && (rec->flags & VHDB_FLAG_HAS_EBOOT))
			same = memcmp(installed->eboot, rec->eboot, 16) == 0;
		out->state = same ? VHDB_STATE_INSTALLED : VHDB_STATE_UPDATE;
		out->trusted = 1;
		out->by_content = 1;
	} else if (installed->has_eboot && (rec->flags & VHDB_FLAG_HAS_EBOOT)) {
		out->state = memcmp(installed->eboot, rec->eboot, 16) == 0
				     ? VHDB_STATE_INSTALLED
				     : VHDB_STATE_UPDATE;
		out->trusted = 1;
		out->by_content = 1;
	} else {
		int cmp = vhdb_version_compare(installed->version,
					       vhdb_str(db, rec->version));
		if (cmp == VHDB_VER_NEWER)
			out->state = VHDB_STATE_UPDATE;
		else if (cmp == VHDB_VER_UNKNOWN)
			out->state = VHDB_STATE_UNKNOWN_VERSION;
		else
			out->state = VHDB_STATE_INSTALLED;
		out->trusted = installed->has_hash;
	}

	needs = vhdb_str(db, rec->needs);
	total = vhdb_needs_count(needs);
	out->needs_total = total;

	for (i = 0; i < total; i++) {
		vhdb_need need;
		if (!vhdb_needs_check(needs, i, &need, exists, user))
			continue;
		if (need.state == VHDB_NEED_UNCHECKED)
			continue;
		out->needs_checked++;
		if (need.state == VHDB_NEED_MET)
			out->needs_met++;
	}
}

int vhdb_install_kind(const vhdb_record *rec)
{
	switch (rec->platform) {
	case VHDB_PLATFORM_VITA: return VHDB_INSTALL_VPK;
	case VHDB_PLATFORM_PSP: return VHDB_INSTALL_PSP;
	case VHDB_PLATFORM_PLUGIN: return VHDB_INSTALL_MANUAL;
	default: return VHDB_INSTALL_PC;
	}
}

int vhdb_can_install(const vhdb_record *rec, int client)
{
	int kind = vhdb_install_kind(rec);

	if (client == VHDB_CLIENT_PC)
		return 1;
	return kind == VHDB_INSTALL_VPK || kind == VHDB_INSTALL_PSP;
}

int vhdb_has_data_file(const vhdb_record *rec)
{
	return (rec->flags & VHDB_FLAG_HAS_DATA) ? 1 : 0;
}

const char *vhdb_install_label(int kind)
{
	switch (kind) {
	case VHDB_INSTALL_VPK: return "VPK";
	case VHDB_INSTALL_PSP: return "PSP";
	case VHDB_INSTALL_MANUAL: return "PLUGIN";
	default: return "PC TOOL";
	}
}

const char *vhdb_list_label(const vhdb_record *rec, const vhdb_status *status,
			    int client)
{
	if (!vhdb_can_install(rec, client))
		return "PC ONLY";
	if (status->state == VHDB_STATE_UPDATE)
		return "UPDATE";
	if (status->state != VHDB_STATE_NOT_INSTALLED &&
	    status->needs_checked > 0 && status->needs_met < status->needs_checked)
		return "NEEDS FILES";
	return vhdb_state_name(status->state);
}
