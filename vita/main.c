#include "vhdb.h"
#include "vhdb_installed.h"
#include "vhdb_status.h"

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
#define CATEGORY_TOOLS 9
#define CATEGORY_ALL 10
#define CATEGORY_COUNT 11

static const char *category_names[CATEGORY_COUNT] = {
	"Updates", "Needs files", "Installed", "Games",   "Ports", "Emulators",
	"Utilities", "Plugins",   "PSP",       "PC tools", "All"
};

static vita2d_pgf *font;
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
	case CATEGORY_TOOLS:
		return rec->platform == VHDB_PLATFORM_TOOL;
	case CATEGORY_ALL:
		return 1;
	default:
		break;
	}

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
		textf(SIDEBAR_WIDTH - 46, y, COLOR_MUTED, 0.9f, "%d",
		      category_counts[i]);
	}

	text(20, 30, COLOR_ACCENT, 1.1f, "VHDB");
}

static void draw_list(void)
{
	int row;
	char line[256];

	textf(SIDEBAR_WIDTH + 24, 30, COLOR_MUTED, 0.9f, "%s, %u entries, sorted by %s",
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

		clip_text(line, sizeof(line), vhdb_str(&db, rec->author), 0.85f, 240);
		textf(SIDEBAR_WIDTH + 24, y + 40, COLOR_MUTED, 0.85f, "%s  %s", line,
		      vhdb_str(&db, rec->version));

		text(SCREEN_WIDTH - 20 - vita2d_pgf_text_width(font, 0.9f, label),
		     y + 28, status_color(&status, can_install), 0.9f, label);

		vita2d_draw_rectangle(SIDEBAR_WIDTH + 24, y + ROW_HEIGHT - 1,
				      SCREEN_WIDTH - SIDEBAR_WIDTH - 44, 1, COLOR_RULE);
	}
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
		text(x, *y, COLOR_MUTED, 0.9f, "Requirements");
		*y += 24;
		textf(x, *y, COLOR_TEXT, 0.9f, "%s", plain);
		*y += 24;
		return;
	}

	text(x, *y, COLOR_MUTED, 0.9f, "Requirements");
	*y += 24;

	for (i = 0; i < total; i++) {
		vhdb_need need;

		if (!vhdb_needs_get(needs, i, &need))
			continue;
		textf(x + 16, *y, COLOR_TEXT, 0.9f, "%s%s%s", need.text,
		      need.path[0] ? "   " : "", need.path);
		*y += 22;
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

	clip_text(line, sizeof(line), vhdb_str(&db, rec->name), 1.3f, 700);
	text(x, y, COLOR_TEXT, 1.3f, line);
	y += 34;

	textf(x, y, COLOR_MUTED, 0.9f, "%s   %s   %s   %.1f MB",
	      vhdb_str(&db, rec->version), vhdb_str(&db, rec->author),
	      vhdb_type_name(rec->type), (double)rec->size / 1048576.0);
	y += 22;

	textf(x, y, COLOR_MUTED, 0.9f, "%s   released %u   id %u",
	      titleid[0] ? titleid : "no title id", rec->date, rec->id);
	y += 30;

	text(x, y, status_color(&status, vhdb_can_install(rec, VHDB_CLIENT_VITA)),
	     1.0f, vhdb_list_label(rec, &status, VHDB_CLIENT_VITA));
	if (status.by_content)
		text(x + 200, y, COLOR_MUTED, 0.85f, "checked by file contents");
	y += 30;

	vita2d_draw_rectangle(x, y, SCREEN_WIDTH - x - 20, 1, COLOR_RULE);
	y += 24;

	clip_text(line, sizeof(line), vhdb_str(&db, rec->long_description), 0.9f,
		  SCREEN_WIDTH - x - 30);
	text(x, y, COLOR_TEXT, 0.9f, line);
	y += 32;

	draw_needs(rec, x, &y);

	if (vhdb_has_data_file(rec)) {
		textf(x, y, COLOR_WARN, 0.9f, "Needs a data file, %.1f MB",
		      (double)rec->data_size / 1048576.0);
		y += 24;
	}

	if (rec->aux_kind) {
		textf(x, y, COLOR_MUTED, 0.85f, "Engine: %s",
		      vhdb_aux_name(rec->aux_kind));
		y += 22;
	}
}

static void draw_footer(void)
{
	const char *hints = (view == VIEW_LIST)
				    ? "X details   Triangle sort   L R category"
				    : "O back";

	vita2d_draw_rectangle(0, SCREEN_HEIGHT - FOOTER_HEIGHT, SCREEN_WIDTH,
			      FOOTER_HEIGHT, COLOR_PANEL);
	text(SIDEBAR_WIDTH + 24, SCREEN_HEIGHT - 12, COLOR_MUTED, 0.85f, hints);
	textf(SCREEN_WIDTH - 150, SCREEN_HEIGHT - 12, COLOR_MUTED, 0.85f,
	      "built %u", db.header ? db.header->built : 0);
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
		text(60, 200, COLOR_TEXT, 1.1f, first);
		text(60, 240, COLOR_MUTED, 0.95f, second);
		text(60, 300, COLOR_MUTED, 0.9f, "Press the PS button to leave.");
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

	rc = vhdb_load(&db, CATALOG_PATH, 0);
	if (rc != VHDB_OK)
		fail("No catalog on this console.",
		     "Put vhdb.bin into ux0:data/vhdb/ and start again.");

	filtered = (uint32_t *)malloc(sizeof(uint32_t) * vhdb_count(&db));
	if (!filtered)
		fail("Out of memory.", "The catalog is too large to index.");

	vhdb_installed_load(&installed, INSTALLED_PATH);

	count_categories();
	rebuild_filter();
	if (filtered_count == 0) {
		category = CATEGORY_ALL;
		rebuild_filter();
	}

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
		} else {
			if (pressed & SCE_CTRL_CIRCLE)
				view = VIEW_LIST;
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

	vita2d_wait_rendering_done();
	vita2d_fini();
	vita2d_free_pgf(font);

	free(filtered);
	vhdb_installed_free(&installed);
	vhdb_free(&db);

	sceKernelExitProcess(0);
	return 0;
}
