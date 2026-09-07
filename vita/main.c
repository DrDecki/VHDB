#include "vhdb.h"
#include "vhdb_installed.h"
#include "vhdb_status.h"
#include "vhdb_vitanet.h"
#include "vhdb_vitainstall.h"
#include "vhdb_vitascan.h"
#include "vhdb_icons.h"

#include <psp2/ctrl.h>
#include <psp2/io/dirent.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <vita2d.h>

#define SCREEN_WIDTH 960
#define SCREEN_HEIGHT 544

#define SIDEBAR_WIDTH 208
#define HEADER_HEIGHT 48
#define FOOTER_HEIGHT 34
#define ROW_HEIGHT 46
#define VISIBLE_ROWS 9

#define COLOR_BACKGROUND RGBA8(0x1A, 0x1A, 0x18, 0xFF)
#define COLOR_PANEL RGBA8(0x24, 0x24, 0x22, 0xFF)
#define COLOR_SELECTED RGBA8(0x33, 0x32, 0x2E, 0xFF)
#define COLOR_TEXT RGBA8(0xE8, 0xE6, 0xE0, 0xFF)
#define COLOR_MUTED RGBA8(0x8A, 0x88, 0x80, 0xFF)
#define COLOR_ACCENT RGBA8(0xC0, 0x7A, 0x3E, 0xFF)
#define COLOR_WARN RGBA8(0xA8, 0x50, 0x40, 0xFF)
#define COLOR_RULE RGBA8(0x3A, 0x39, 0x35, 0xFF)

#define DATA_DIR "ux0:data/vhdb"
#define CATALOG_PATH DATA_DIR "/vhdb.bin"
#define INSTALLED_PATH DATA_DIR "/installed.txt"
#define CATALOG_URL "https://github.com/DrDecki/VHDB/releases/download/catalog/vhdb.bin"
#define VERSION_URL "https://github.com/DrDecki/VHDB/releases/download/catalog/version.txt"
#define CLIENT_VPK_URL "https://github.com/DrDecki/VHDB/releases/download/catalog/vhdb.vpk"
#define VERSION_PATH DATA_DIR "/version.txt"
#define DATA_ZIP DATA_DIR "/data.zip"
#define DATA_TARGET "ux0:data"
#define CLIENT_VERSION "1.0"

#define VIEW_LIST 0
#define VIEW_DETAIL 1

#define CATEGORY_UPDATES 0
#define CATEGORY_NEEDS 1
#define CATEGORY_INSTALLED 2
#define CATEGORY_GAMES 3
#define CATEGORY_PORTS 4
#define CATEGORY_EMULATORS 5
#define CATEGORY_UTILITIES 6
#define CATEGORY_PLUGINS 7
#define CATEGORY_PSP 8
#define CATEGORY_ALL 9
#define CATEGORY_COUNT 10

static const char *category_names[CATEGORY_COUNT] = {
	"Updates", "Needs files", "Installed", "Games",   "Ports", "Emulators",
	"Utilities", "Plugins",   "PSP",       "All"
};

static vita2d_pgf *font;
static vita2d_pvf *symbols;
static vhdb_db db;
static vhdb_installed_list installed;

static uint32_t *filtered;
static uint32_t filtered_count;

static int category = CATEGORY_UPDATES;
static int selected;
static int scroll;
static int view = VIEW_LIST;
static int sort_by_date = 1;
static int category_counts[CATEGORY_COUNT];

#define GLYPH_CIRCLE "!"
#define GLYPH_CROSS "\""
#define GLYPH_SQUARE "#"
#define GLYPH_L "0"
#define GLYPH_R "1"
#define GLYPH_SELECT "4"
#define GLYPH_START "5"

typedef struct {
	const char *glyph;
	const char *fallback;
	const char *label;
} hint;

static void text(int x, int y, unsigned int color, float scale, const char *value)
{
	vita2d_pgf_draw_text(font, x, y, color, scale, value);
}

static void textf(int x, int y, unsigned int color, float scale, const char *format,
		  ...)
{
	char line[512];
	va_list args;

	va_start(args, format);
	vsnprintf(line, sizeof(line), format, args);
	va_end(args);
	text(x, y, color, scale, line);
}

static void clip_text(char *out, size_t size, const char *value, float scale,
		      int width)
{
	size_t length = strlen(value);
	size_t i;

	snprintf(out, size, "%s", value);
	if (vita2d_pgf_text_width(font, scale, out) <= width)
		return;

	for (i = length; i > 3; i--) {
		snprintf(out, size, "%.*s...", (int)(i - 3), value);
		if (vita2d_pgf_text_width(font, scale, out) <= width)
			return;
	}
}

static int draw_hints(int x, int y, const hint *items, int count)
{
	int i;

	for (i = 0; i < count; i++) {
		if (symbols) {
			vita2d_pvf_draw_text(symbols, x, y + 2, COLOR_MUTED, 1.0f,
					     items[i].glyph);
			x += vita2d_pvf_text_width(symbols, 1.0f, items[i].glyph) + 8;
		} else {
			text(x, y, COLOR_MUTED, 1.0f, items[i].fallback);
			x += vita2d_pgf_text_width(font, 1.0f, items[i].fallback) + 6;
		}

		text(x, y, COLOR_MUTED, 1.0f, items[i].label);
		x += vita2d_pgf_text_width(font, 1.0f, items[i].label) + 22;
	}
	return x;
}

static void format_date(unsigned int packed, char *out, size_t size)
{
	static const char *months[12] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun",
					  "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
	unsigned int year = packed / 10000;
	unsigned int month = (packed / 100) % 100;
	unsigned int day = packed % 100;

	if (packed == 0 || month < 1 || month > 12) {
		snprintf(out, size, "unknown");
		return;
	}
	snprintf(out, size, "%u %s %u", day, months[month - 1], year);
}

static vhdb_installed *installed_for(const vhdb_record *rec)
{
	char titleid[13];
	vhdb_installed *entry = vhdb_installed_find_id(&installed, rec->id);

	if (entry)
		return entry;
	vhdb_titleid(rec, titleid, sizeof(titleid));
	return vhdb_installed_find_titleid(&installed, titleid);
}

static void status_for(const vhdb_record *rec, vhdb_status *out)
{
	vhdb_status_of(&db, rec, installed_for(rec), out, NULL, NULL);
}

static int belongs_to(const vhdb_record *rec, int which)
{
	vhdb_status status;

	switch (which) {
	case CATEGORY_GAMES:
		return rec->platform == VHDB_PLATFORM_VITA &&
		       rec->type == VHDB_TYPE_GAME;
	case CATEGORY_PORTS:
		return rec->platform == VHDB_PLATFORM_VITA &&
		       rec->type == VHDB_TYPE_PORT;
	case CATEGORY_EMULATORS:
		return rec->platform == VHDB_PLATFORM_VITA &&
		       rec->type == VHDB_TYPE_EMULATOR;
	case CATEGORY_UTILITIES:
		return rec->platform == VHDB_PLATFORM_VITA &&
		       rec->type == VHDB_TYPE_UTILITY;
	case CATEGORY_PLUGINS:
		return rec->platform == VHDB_PLATFORM_PLUGIN;
	case CATEGORY_PSP:
		return rec->platform == VHDB_PLATFORM_PSP;
	case CATEGORY_ALL:
		return rec->platform != VHDB_PLATFORM_TOOL;
	default:
		break;
	}

	if (rec->platform == VHDB_PLATFORM_TOOL)
		return 0;

	status_for(rec, &status);

	switch (which) {
	case CATEGORY_UPDATES:
		return status.state == VHDB_STATE_UPDATE;
	case CATEGORY_NEEDS:
		return status.state != VHDB_STATE_NOT_INSTALLED &&
		       status.needs_checked > 0 &&
		       status.needs_met < status.needs_checked;
	case CATEGORY_INSTALLED:
		return status.state != VHDB_STATE_NOT_INSTALLED;
	default:
		return 0;
	}
}

static void rebuild_filter(void)
{
	uint32_t i;

	filtered_count = 0;
	for (i = 0; i < vhdb_count(&db); i++) {
		const vhdb_record *rec = sort_by_date ? vhdb_by_date(&db, i)
						     : vhdb_by_name(&db, i);
		if (!belongs_to(rec, category))
			continue;
		filtered[filtered_count++] = (uint32_t)(rec - db.records);
	}

	if (selected >= (int)filtered_count)
		selected = filtered_count ? (int)filtered_count - 1 : 0;
	if (scroll > selected)
		scroll = selected;
	if (selected - scroll >= VISIBLE_ROWS)
		scroll = selected - VISIBLE_ROWS + 1;
	if (scroll < 0)
		scroll = 0;
}

static void count_categories(void)
{
	uint32_t i;
	int which;

	for (which = 0; which < CATEGORY_COUNT; which++)
		category_counts[which] = 0;

	for (i = 0; i < vhdb_count(&db); i++) {
		const vhdb_record *rec = vhdb_at(&db, i);
		for (which = 0; which < CATEGORY_COUNT; which++) {
			if (belongs_to(rec, which))
				category_counts[which]++;
		}
	}
}

static unsigned int status_color(const vhdb_status *status, int can_install)
{
	if (!can_install)
		return COLOR_MUTED;
	switch (status->state) {
	case VHDB_STATE_UPDATE:
		return COLOR_ACCENT;
	case VHDB_STATE_NOT_INSTALLED:
		return COLOR_MUTED;
	default:
		break;
	}
	if (status->needs_checked > 0 && status->needs_met < status->needs_checked)
		return COLOR_WARN;
	return COLOR_TEXT;
}

static void draw_sidebar(void)
{
	int i;

	vita2d_draw_rectangle(0, 0, SIDEBAR_WIDTH, SCREEN_HEIGHT, COLOR_PANEL);

	for (i = 0; i < CATEGORY_COUNT; i++) {
		int y = 60 + i * 38;
		unsigned int color = (i == category) ? COLOR_TEXT : COLOR_MUTED;

		if (i == category) {
			vita2d_draw_rectangle(0, y - 22, SIDEBAR_WIDTH, 32,
					      COLOR_SELECTED);
			vita2d_draw_rectangle(0, y - 22, 3, 32, COLOR_ACCENT);
		}
		text(20, y, color, 1.0f, category_names[i]);
		{
			char amount[16];

			snprintf(amount, sizeof(amount), "%d", category_counts[i]);
			text(SIDEBAR_WIDTH - 20 -
				     vita2d_pgf_text_width(font, 1.0f, amount),
			     y, COLOR_MUTED, 1.0f, amount);
		}
	}

	text(20, 30, COLOR_ACCENT, 1.0f, "VHDB");
}

static void draw_list(void)
{
	int row;
	char line[256];

	textf(SIDEBAR_WIDTH + 24, 30, COLOR_MUTED, 1.0f, "%s, %u entries, sorted by %s",
	      category_names[category], filtered_count,
	      sort_by_date ? "date" : "name");

	if (filtered_count == 0) {
		text(SIDEBAR_WIDTH + 24, 120, COLOR_MUTED, 1.0f,
		     "Nothing here yet.");
		return;
	}

	for (row = 0; row < VISIBLE_ROWS; row++) {
		int index = scroll + row;
		const vhdb_record *rec;
		vhdb_status status;
		int y = HEADER_HEIGHT + row * ROW_HEIGHT;
		int can_install;
		const char *label;

		if (index >= (int)filtered_count)
			break;

		rec = vhdb_at(&db, filtered[index]);
		status_for(rec, &status);
		can_install = vhdb_can_install(rec, VHDB_CLIENT_VITA);
		label = vhdb_list_label(rec, &status, VHDB_CLIENT_VITA);

		if (index == selected) {
			vita2d_draw_rectangle(SIDEBAR_WIDTH, y, SCREEN_WIDTH - SIDEBAR_WIDTH,
					      ROW_HEIGHT, COLOR_SELECTED);
			vita2d_draw_rectangle(SIDEBAR_WIDTH, y, 3, ROW_HEIGHT,
					      COLOR_ACCENT);
		}

		clip_text(line, sizeof(line), vhdb_str(&db, rec->name), 1.0f, 380);
		text(SIDEBAR_WIDTH + 24, y + 22, COLOR_TEXT, 1.0f, line);

		clip_text(line, sizeof(line), vhdb_str(&db, rec->author), 1.0f, 240);
		textf(SIDEBAR_WIDTH + 24, y + 40, COLOR_MUTED, 1.0f, "%s  %s", line,
		      vhdb_str(&db, rec->version));

		text(SCREEN_WIDTH - 20 - vita2d_pgf_text_width(font, 1.0f, label),
		     y + 28, status_color(&status, can_install), 1.0f, label);

		vita2d_draw_rectangle(SIDEBAR_WIDTH + 24, y + ROW_HEIGHT - 1,
				      SCREEN_WIDTH - SIDEBAR_WIDTH - 44, 1, COLOR_RULE);
	}
}

static void draw_wrapped(const char *value, int x, int *y, int width, int lines,
			 float scale, unsigned int color)
{
	char line[256];
	int drawn = 0;

	while (*value && drawn < lines) {
		size_t length = 0;
		size_t best = 0;

		while (value[length] && value[length] != '\n' &&
		       length + 1 < sizeof(line)) {
			line[length] = value[length];
			line[length + 1] = 0;
			if (vita2d_pgf_text_width(font, scale, line) > width)
				break;
			if (value[length] == ' ')
				best = length;
			length++;
		}

		if (value[length] && value[length] != '\n' && best > 0)
			length = best;

		memcpy(line, value, length);
		line[length] = 0;
		text(x, *y, color, scale, line);
		*y += (int)(26 * scale) + 4;
		drawn++;

		value += length;
		while (*value == ' ' || *value == '\n')
			value++;
	}

	if (*value && drawn == lines)
		text(x, *y, COLOR_MUTED, scale * 1.0f, "...");
}

static void draw_needs(const vhdb_record *rec, int x, int *y)
{
	const char *needs = vhdb_str(&db, rec->needs);
	int total = vhdb_needs_count(needs);
	int i;

	if (total == 0) {
		const char *plain = vhdb_str(&db, rec->requirements);
		if (!plain[0])
			return;
		text(x, *y, COLOR_MUTED, 1.0f, "Requirements");
		*y += 24;
		draw_wrapped(plain, x + 16, y, SCREEN_WIDTH - x - 50, 4, 1.0f,
			     COLOR_TEXT);
		*y += 6;
		return;
	}

	text(x, *y, COLOR_MUTED, 1.0f, "Requirements");
	*y += 24;

	for (i = 0; i < total; i++) {
		vhdb_need need;

		if (!vhdb_needs_get(needs, i, &need))
			continue;
		{
			char entry[384];

			snprintf(entry, sizeof(entry), "%s%s%s", need.text,
				 need.path[0] ? "   " : "", need.path);
			draw_wrapped(entry, x + 16, y, SCREEN_WIDTH - x - 50, 2, 1.0f,
				     COLOR_TEXT);
		}
	}
	*y += 6;
}

static void draw_detail(void)
{
	const vhdb_record *rec;
	vhdb_status status;
	char titleid[13];
	char line[512];
	int x = SIDEBAR_WIDTH + 24;
	int y = 48;

	if (filtered_count == 0)
		return;

	rec = vhdb_at(&db, filtered[selected]);
	status_for(rec, &status);
	vhdb_titleid(rec, titleid, sizeof(titleid));

	{
		vita2d_texture *icon = vhdb_icon_for(filtered[selected]);

		if (icon) {
			float width = (float)vita2d_texture_get_width(icon);
			float height = (float)vita2d_texture_get_height(icon);
			float longest = width > height ? width : height;
			float factor = longest > 0.0f ? 128.0f / longest : 1.0f;
			float box = (float)(SCREEN_WIDTH - 150);

			vita2d_draw_texture_scale(icon,
						  box + (128.0f - width * factor) / 2.0f,
						  34.0f + (128.0f - height * factor) / 2.0f,
						  factor, factor);
		}
	}

	clip_text(line, sizeof(line), vhdb_str(&db, rec->name), 1.0f, 500);
	text(x, y, COLOR_TEXT, 1.0f, line);
	y += 34;

	textf(x, y, COLOR_MUTED, 1.0f, "%s   %s   %s   %.1f MB",
	      vhdb_str(&db, rec->version), vhdb_str(&db, rec->author),
	      vhdb_type_name(rec->type), (double)rec->size / 1048576.0);
	y += 22;

	{
		char released[32];

		format_date(rec->date, released, sizeof(released));
		textf(x, y, COLOR_MUTED, 1.0f, "%s   released %s   id %u",
		      titleid[0] ? titleid : "no title id", released, rec->id);
	}
	y += 30;

	text(x, y, status_color(&status, vhdb_can_install(rec, VHDB_CLIENT_VITA)),
	     1.0f, vhdb_list_label(rec, &status, VHDB_CLIENT_VITA));
	if (status.by_content)
		text(x + 200, y, COLOR_MUTED, 1.0f, "checked by file contents");
	y += 30;

	vita2d_draw_rectangle(x, y, SCREEN_WIDTH - x - 178, 1, COLOR_RULE);
	y += 24;

	draw_wrapped(vhdb_str(&db, rec->long_description), x, &y,
		     SCREEN_WIDTH - x - 30, 8, 1.0f, COLOR_TEXT);
	y += 10;

	draw_needs(rec, x, &y);

	if (vhdb_has_data_file(rec)) {
		textf(x, y, COLOR_WARN, 1.0f, "Needs a data file, %.1f MB, press Square",
		      (double)rec->data_size / 1048576.0);
		y += 24;
	}

	if (rec->aux_kind) {
		textf(x, y, COLOR_MUTED, 1.0f, "Engine: %s",
		      vhdb_aux_name(rec->aux_kind));
		y += 22;
	}
}

static void draw_footer(void)
{
	static const hint list_hints[] = {
		{ GLYPH_CROSS, "X", "details" },
		{ GLYPH_L GLYPH_R, "L R", "category" },
		{ GLYPH_SELECT, "Select", "sync" },
		{ GLYPH_START, "Start", "scan" }
	};
	static const hint back_only[] = {
		{ GLYPH_CIRCLE, "O", "back" }
	};
	hint detail_hints[3];
	int count = 0;

	vita2d_draw_rectangle(0, SCREEN_HEIGHT - FOOTER_HEIGHT, SCREEN_WIDTH,
			      FOOTER_HEIGHT, COLOR_PANEL);

	if (view == VIEW_LIST) {
		draw_hints(SIDEBAR_WIDTH + 24, SCREEN_HEIGHT - 12, list_hints, 4);
	} else if (filtered_count == 0) {
		draw_hints(SIDEBAR_WIDTH + 24, SCREEN_HEIGHT - 12, back_only, 1);
	} else {
		const vhdb_record *rec = vhdb_at(&db, filtered[selected]);
		vhdb_status status;

		status_for(rec, &status);

		if (vhdb_can_install(rec, VHDB_CLIENT_VITA)) {
			detail_hints[count].glyph = GLYPH_CROSS;
			detail_hints[count].fallback = "X";
			if (status.state == VHDB_STATE_UPDATE)
				detail_hints[count].label = "update";
			else if (status.state == VHDB_STATE_NOT_INSTALLED)
				detail_hints[count].label = "install";
			else
				detail_hints[count].label = "reinstall";
			count++;

			if (vhdb_has_data_file(rec)) {
				detail_hints[count].glyph = GLYPH_SQUARE;
				detail_hints[count].fallback = "Square";
				detail_hints[count].label = "data files";
				count++;
			}
		}

		detail_hints[count].glyph = GLYPH_CIRCLE;
		detail_hints[count].fallback = "O";
		detail_hints[count].label = "back";
		count++;

		draw_hints(SIDEBAR_WIDTH + 24, SCREEN_HEIGHT - 12, detail_hints, count);
	}

}

static void overlay(const char *title, const char *line1, const char *line2)
{
	int width = 620;
	int height = 180;
	int x = (SCREEN_WIDTH - width) / 2;
	int y = (SCREEN_HEIGHT - height) / 2;

	vita2d_draw_rectangle(x - 2, y - 2, width + 4, height + 4, COLOR_RULE);
	vita2d_draw_rectangle(x, y, width, height, COLOR_PANEL);
	vita2d_draw_rectangle(x, y, width, 3, COLOR_ACCENT);

	text(x + 28, y + 48, COLOR_TEXT, 1.0f, title);
	if (line1)
		text(x + 28, y + 88, COLOR_MUTED, 1.0f, line1);
	if (line2)
		text(x + 28, y + 116, COLOR_MUTED, 1.0f, line2);
}

static void frame_with_overlay(const char *title, const char *line1,
			       const char *line2)
{
	vita2d_start_drawing();
	vita2d_clear_screen();
	draw_sidebar();
	draw_list();
	draw_footer();
	overlay(title, line1, line2);
	vita2d_end_drawing();
	vita2d_swap_buffers();
}

static int download_progress(uint64_t done, uint64_t total, void *user)
{
	char line[96];
	SceCtrlData pad;

	if (total > 0)
		snprintf(line, sizeof(line), "%.1f of %.1f MB",
			 (double)done / 1048576.0, (double)total / 1048576.0);
	else
		snprintf(line, sizeof(line), "%.1f MB", (double)done / 1048576.0);

	frame_with_overlay((const char *)user, line, "Circle cancels");

	sceCtrlPeekBufferPositive(0, &pad, 1);
	return (pad.buttons & SCE_CTRL_CIRCLE) ? 0 : 1;
}

static void wait_for_button(const char *title, const char *line1,
			    const char *line2)
{
	SceCtrlData pad;
	unsigned int previous = 0xFFFFFFFF;

	while (1) {
		unsigned int pressed;

		sceCtrlPeekBufferPositive(0, &pad, 1);
		pressed = pad.buttons & ~previous;
		previous = pad.buttons;

		if (pressed & (SCE_CTRL_CROSS | SCE_CTRL_CIRCLE))
			break;

		frame_with_overlay(title, line1, line2);
	}
}

static uint32_t read_u32(const uint8_t *p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
	       ((uint32_t)p[3] << 24);
}

static int reload_catalog(void)
{
	free(filtered);
	filtered = NULL;
	vhdb_free(&db);

	if (vhdb_load(&db, CATALOG_PATH, 1) != VHDB_OK)
		return 0;

	filtered = (uint32_t *)malloc(sizeof(uint32_t) * vhdb_count(&db));
	if (!filtered)
		return 0;

	count_categories();
	selected = 0;
	scroll = 0;
	rebuild_filter();
	return 1;
}

static void sync_catalog(void)
{
	uint8_t header[VHDB_HEADER_SIZE];
	char line[96];

	frame_with_overlay("Checking for a new catalog", "Starting the network",
			   NULL);

	if (!vhdb_net_start()) {
		wait_for_button("Network trouble", vhdb_net_error(), NULL);
		return;
	}
	if (!vhdb_net_online()) {
		wait_for_button("No connection",
				"Connect the console to Wi-Fi and try again.", NULL);
		return;
	}

	if (vhdb_net_head(CATALOG_URL, header, sizeof(header)) &&
	    read_u32(header) == VHDB_MAGIC) {
		if (db.header && read_u32(header + 40) == db.header->catalog_hash) {
			snprintf(line, sizeof(line), "%u entries, built %u",
				 read_u32(header + 12), read_u32(header + 8));
			wait_for_button("Already up to date", line, NULL);
			return;
		}
		snprintf(line, sizeof(line), "%u entries waiting",
			 read_u32(header + 12));
	} else {
		snprintf(line, sizeof(line), "%s", vhdb_net_error());
	}

	if (!vhdb_net_fetch(CATALOG_URL, CATALOG_PATH, download_progress,
			    (void *)"Downloading the catalog")) {
		wait_for_button("Download failed", vhdb_net_error(), NULL);
		reload_catalog();
		return;
	}

	if (!reload_catalog()) {
		wait_for_button("The catalog did not verify",
				"The old one is gone, please sync again.", NULL);
		return;
	}

	snprintf(line, sizeof(line), "%u entries, built %u", vhdb_count(&db),
		 db.header->built);
	wait_for_button("Catalog updated", line, NULL);
}

static int unpack_progress(uint32_t done, uint32_t total, const char *name,
			   void *user)
{
	char line[128];

	snprintf(line, sizeof(line), "%u of %u files", done, total);
	frame_with_overlay((const char *)user, line, name);
	return 1;
}

static void remember_installed(const vhdb_record *rec)
{
	vhdb_installed entry;

	memset(&entry, 0, sizeof(entry));
	entry.id = rec->id;
	vhdb_titleid(rec, entry.titleid, sizeof(entry.titleid));
	snprintf(entry.version, sizeof(entry.version), "%s",
		 vhdb_str(&db, rec->version));
	memcpy(entry.hash, rec->hash, 16);
	entry.has_hash = 1;
	if (rec->flags & VHDB_FLAG_HAS_EBOOT) {
		memcpy(entry.eboot, rec->eboot, 16);
		entry.has_eboot = 1;
	}
	if (rec->flags & VHDB_FLAG_HAS_AUX) {
		memcpy(entry.aux, rec->aux, 16);
		entry.has_aux = 1;
	}

	vhdb_installed_set(&installed, &entry);
	vhdb_installed_save(&installed, INSTALLED_PATH);
	count_categories();
	rebuild_filter();
}

static int confirm(const char *title, const char *line1, const char *line2)
{
	SceCtrlData pad;
	unsigned int previous = 0xFFFFFFFF;

	while (1) {
		unsigned int pressed;

		sceCtrlPeekBufferPositive(0, &pad, 1);
		pressed = pad.buttons & ~previous;
		previous = pad.buttons;

		if (pressed & SCE_CTRL_CROSS)
			return 1;
		if (pressed & SCE_CTRL_CIRCLE)
			return 0;

		frame_with_overlay(title, line1, line2);
	}
}

static int fetch_data_for(const vhdb_record *rec, int ask)
{
	char names[6][64];
	char line[160];
	char detail[160];
	int count;

	if (!vhdb_has_data_file(rec))
		return 1;

	frame_with_overlay("Starting the network", NULL, NULL);
	if (!vhdb_net_start() || !vhdb_net_online()) {
		wait_for_button("No connection",
				"Connect the console to Wi-Fi and try again.", NULL);
		return 0;
	}

	snprintf(line, sizeof(line), "Data for %s", vhdb_str(&db, rec->name));
	if (!vhdb_net_fetch(vhdb_str(&db, rec->data_url), DATA_ZIP,
			    download_progress, (void *)line)) {
		wait_for_button("The data file failed", vhdb_net_error(), NULL);
		return 0;
	}

	count = vhdb_zip_top_level(DATA_ZIP, names, 6);
	if (count <= 0) {
		sceIoRemove(DATA_ZIP);
		wait_for_button("Cannot read the archive",
				"It may not be a zip file.", NULL);
		return 0;
	}

	snprintf(line, sizeof(line), "Unpack into %s", DATA_TARGET);
	if (count == 1)
		snprintf(detail, sizeof(detail), "It holds %s", names[0]);
	else if (count == 2)
		snprintf(detail, sizeof(detail), "It holds %s and %s", names[0],
			 names[1]);
	else
		snprintf(detail, sizeof(detail), "It holds %s, %s and %d more",
			 names[0], names[1], count - 2);

	if (ask && !confirm(detail, line, "X unpacks, O keeps nothing")) {
		sceIoRemove(DATA_ZIP);
		return 0;
	}

	if (!vhdb_zip_extract(DATA_ZIP, DATA_TARGET, unpack_progress, (void *)detail)) {
		sceIoRemove(DATA_ZIP);
		wait_for_button("Unpacking failed", vhdb_zip_error(), NULL);
		return 0;
	}

	sceIoRemove(DATA_ZIP);
	count_categories();
	rebuild_filter();

	if (ask) {
		snprintf(line, sizeof(line), "%s is ready", vhdb_str(&db, rec->name));
		wait_for_button("Data unpacked", line, NULL);
	}
	return 1;
}

static void install_data_file(void)
{
	const vhdb_record *rec;

	if (filtered_count == 0)
		return;

	rec = vhdb_at(&db, filtered[selected]);

	if (!vhdb_has_data_file(rec)) {
		wait_for_button("No data file", "This one runs on its own.", NULL);
		return;
	}

	fetch_data_for(rec, 1);
}

static void install_selected(void)
{
	const vhdb_record *rec;
	char line[128];

	if (filtered_count == 0)
		return;

	rec = vhdb_at(&db, filtered[selected]);

	if (!vhdb_can_install(rec, VHDB_CLIENT_VITA)) {
		wait_for_button("Not for the console",
				"Plugins and PC tools are downloaded on a computer.",
				"You place them yourself.");
		return;
	}
	if (!vhdb_str(&db, rec->url)[0]) {
		wait_for_button("No download link",
				"This entry has nothing to fetch.", NULL);
		return;
	}

	frame_with_overlay("Starting the network", NULL, NULL);
	if (!vhdb_net_start() || !vhdb_net_online()) {
		wait_for_button("No connection",
				"Connect the console to Wi-Fi and try again.", NULL);
		return;
	}

	snprintf(line, sizeof(line), "%s %s", vhdb_str(&db, rec->name),
		 vhdb_str(&db, rec->version));

	if (!vhdb_install_from_url(vhdb_str(&db, rec->url), rec->hash,
				   download_progress, (void *)line, unpack_progress,
				   (void *)"Installing")) {
		wait_for_button("Install failed", vhdb_install_error(), NULL);
		return;
	}

	remember_installed(rec);

	if (vhdb_has_data_file(rec) && !fetch_data_for(rec, 0)) {
		wait_for_button("Installed without its data", line,
				"Press Square to try the data files again.");
		return;
	}

	wait_for_button("Installed", line, NULL);
}

static void scan_progress(int done, const char *name, void *user)
{
	char line[96];

	snprintf(line, sizeof(line), "%d found so far", done);
	frame_with_overlay((const char *)user, line, name);
}

static void scan_console(void)
{
	char line[96];
	int matched;

	matched = vhdb_vita_scan(&db, &installed, scan_progress,
				 (void *)"Reading the console");
	vhdb_installed_save(&installed, INSTALLED_PATH);
	count_categories();
	rebuild_filter();

	if (matched < 0) {
		wait_for_button("Cannot read ux0:app", "Nothing was changed.", NULL);
		return;
	}

	snprintf(line, sizeof(line), "%d apps here are in the catalog", matched);
	wait_for_button("Console checked", line,
			"Versions now come from the files themselves.");
}

static int cancelled_by_user(void)
{
	SceCtrlData pad;

	sceCtrlPeekBufferPositive(0, &pad, 1);
	return (pad.buttons & SCE_CTRL_CIRCLE) ? 1 : 0;
}

static int read_version_file(char *out, size_t size)
{
	SceUID file;
	int read;

	file = sceIoOpen(VERSION_PATH, SCE_O_RDONLY, 0777);
	if (file < 0)
		return 0;

	read = sceIoRead(file, out, (unsigned int)size - 1);
	sceIoClose(file);
	sceIoRemove(VERSION_PATH);

	if (read <= 0)
		return 0;
	out[read] = 0;

	while (read > 0 && (out[read - 1] == '\n' || out[read - 1] == '\r' ||
			    out[read - 1] == ' '))
		out[--read] = 0;
	return out[0] != 0;
}

static void check_client_update(void)
{
	char latest[32];
	char line[96];
	SceCtrlData pad;
	unsigned int previous = 0xFFFFFFFF;

	if (!vhdb_net_fetch(VERSION_URL, VERSION_PATH, NULL, NULL))
		return;
	if (!read_version_file(latest, sizeof(latest)))
		return;
	if (vhdb_version_compare(CLIENT_VERSION, latest) != VHDB_VER_NEWER)
		return;

	snprintf(line, sizeof(line), "You have %s, %s is out", CLIENT_VERSION,
		 latest);

	while (1) {
		unsigned int pressed;

		sceCtrlPeekBufferPositive(0, &pad, 1);
		pressed = pad.buttons & ~previous;
		previous = pad.buttons;

		if (pressed & SCE_CTRL_CIRCLE)
			return;
		if (pressed & SCE_CTRL_CROSS)
			break;

		frame_with_overlay("A new VHDB is out", line,
				   "X updates now, O skips");
	}

	if (!vhdb_install_from_url(CLIENT_VPK_URL, NULL, download_progress,
				   (void *)"Updating VHDB", unpack_progress,
				   (void *)"Installing VHDB")) {
		wait_for_button("Update failed", vhdb_install_error(), NULL);
		return;
	}

	wait_for_button("VHDB updated", "Close it and start it again.", NULL);
}

static void check_catalog_quietly(void)
{
	uint8_t header[VHDB_HEADER_SIZE];

	frame_with_overlay("Checking the catalog", NULL, "O skips");
	if (cancelled_by_user())
		return;

	if (!vhdb_net_head(CATALOG_URL, header, sizeof(header)) ||
	    read_u32(header) != VHDB_MAGIC)
		return;
	if (db.header && read_u32(header + 40) == db.header->catalog_hash)
		return;

	if (!vhdb_net_fetch(CATALOG_URL, CATALOG_PATH, download_progress,
			    (void *)"A newer catalog is out"))
		return;

	reload_catalog();
}

static void fetch_icon_pack(void)
{
	if (vhdb_icons_have_pack())
		return;

	frame_with_overlay("Getting the icons", "This happens once, 11 MB",
			   "O skips");
	if (cancelled_by_user())
		return;

	vhdb_icons_fetch_pack(download_progress, (void *)"Getting the icons",
			      unpack_progress, (void *)"Unpacking the icons");
}

static void startup_tasks(void)
{
	frame_with_overlay("Starting the network", NULL, "O skips");
	if (cancelled_by_user())
		return;
	if (!vhdb_net_start() || !vhdb_net_online())
		return;

	check_client_update();
	check_catalog_quietly();
	fetch_icon_pack();
}

static void move_selection(int delta)
{
	if (filtered_count == 0)
		return;

	selected += delta;
	if (selected < 0)
		selected = 0;
	if (selected >= (int)filtered_count)
		selected = (int)filtered_count - 1;

	if (selected < scroll)
		scroll = selected;
	if (selected - scroll >= VISIBLE_ROWS)
		scroll = selected - VISIBLE_ROWS + 1;
}

static void change_category(int delta)
{
	category += delta;
	if (category < 0)
		category = CATEGORY_COUNT - 1;
	if (category >= CATEGORY_COUNT)
		category = 0;
	selected = 0;
	scroll = 0;
	rebuild_filter();
}

static void fail(const char *first, const char *second)
{
	while (1) {
		vita2d_start_drawing();
		vita2d_clear_screen();
		text(60, 200, COLOR_TEXT, 1.0f, first);
		text(60, 240, COLOR_MUTED, 1.0f, second);
		text(60, 300, COLOR_MUTED, 1.0f, "Press the PS button to leave.");
		vita2d_end_drawing();
		vita2d_swap_buffers();
	}

	vita2d_wait_rendering_done();
	vita2d_fini();
	vita2d_free_pgf(font);
	sceKernelExitProcess(0);
}

int main(void)
{
	SceCtrlData pad;
	unsigned int previous = 0;
	int rc;

	sceIoMkdir("ux0:data", 0777);
	sceIoMkdir(DATA_DIR, 0777);

	vita2d_init();
	vita2d_set_clear_color(COLOR_BACKGROUND);
	font = vita2d_load_default_pgf();
	symbols = vita2d_load_custom_pvf("sa0:data/font/pvf/psexchar.pvf");

	rc = vhdb_load(&db, CATALOG_PATH, 0);
	if (rc != VHDB_OK)
		fail("No catalog on this console.",
		     "Put vhdb.bin into ux0:data/vhdb/ and start again.");

	filtered = (uint32_t *)malloc(sizeof(uint32_t) * vhdb_count(&db));
	if (!filtered)
		fail("Out of memory.", "The catalog is too large to index.");

	vhdb_installed_load(&installed, INSTALLED_PATH);
	if (vhdb_prune_installed(&installed))
		vhdb_installed_save(&installed, INSTALLED_PATH);

	vhdb_icons_start(&db);

	count_categories();
	rebuild_filter();
	if (filtered_count == 0) {
		category = CATEGORY_ALL;
		rebuild_filter();
	}

	startup_tasks();
	count_categories();
	rebuild_filter();

	while (1) {
		unsigned int pressed;

		sceCtrlPeekBufferPositive(0, &pad, 1);
		pressed = pad.buttons & ~previous;
		previous = pad.buttons;

		if (view == VIEW_LIST) {
			if (pressed & SCE_CTRL_UP)
				move_selection(-1);
			if (pressed & SCE_CTRL_DOWN)
				move_selection(1);
			if (pressed & SCE_CTRL_LEFT)
				move_selection(-VISIBLE_ROWS);
			if (pressed & SCE_CTRL_RIGHT)
				move_selection(VISIBLE_ROWS);
			if (pressed & SCE_CTRL_LTRIGGER)
				change_category(-1);
			if (pressed & SCE_CTRL_RTRIGGER)
				change_category(1);
			if (pressed & SCE_CTRL_TRIANGLE) {
				sort_by_date = !sort_by_date;
				rebuild_filter();
			}
			if ((pressed & SCE_CTRL_CROSS) && filtered_count > 0)
				view = VIEW_DETAIL;
			if (pressed & SCE_CTRL_SELECT)
				sync_catalog();
			if (pressed & SCE_CTRL_START)
				scan_console();
		} else {
			if (pressed & SCE_CTRL_CIRCLE)
				view = VIEW_LIST;
			if (pressed & SCE_CTRL_CROSS)
				install_selected();
			if (pressed & SCE_CTRL_SQUARE)
				install_data_file();
		}

		vita2d_start_drawing();
		vita2d_clear_screen();

		draw_sidebar();
		if (view == VIEW_LIST)
			draw_list();
		else
			draw_detail();
		draw_footer();

		vita2d_end_drawing();
		vita2d_swap_buffers();
	}

	vhdb_icons_stop();
	vhdb_net_stop();

	vita2d_wait_rendering_done();
	vita2d_fini();
	vita2d_free_pgf(font);
	if (symbols)
		vita2d_free_pvf(symbols);

	free(filtered);
	vhdb_installed_free(&installed);
	vhdb_free(&db);

	sceKernelExitProcess(0);
	return 0;
}
