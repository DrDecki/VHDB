#include "vhdb_icons.h"
#include "vhdb_vitanet.h"
#include "vhdb_zip.h"

#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/threadmgr.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <vita2d.h>

#define ICON_DIR "ux0:data/vhdb/icons"
#define ICON_URL "https://drdecki.github.io/VitaHomebrewDB/icons/"
#define ICON_PACK_URL "https://drdecki.github.io/VitaHomebrewDB/icons.zip"
#define ICON_PACK_FILE "ux0:data/vhdb/icons.zip"
#define ICON_PACK_MARK ICON_DIR "/.pack"

#define STATE_UNKNOWN 0
#define STATE_QUEUED 1
#define STATE_ON_DISK 2
#define STATE_LOADED 3
#define STATE_MISSING 4

#define CACHE_LIMIT 48

static const vhdb_db *catalog;
static uint8_t *states;
static vita2d_texture **textures;
static uint32_t *recent;
static int recent_count;
static uint32_t entry_count;

static volatile int wanted = -1;
static volatile int running;
static SceUID worker;

static void round_corners(vita2d_texture *texture)
{
	uint8_t *pixels = (uint8_t *)vita2d_texture_get_datap(texture);
	unsigned int width = vita2d_texture_get_width(texture);
	unsigned int height = vita2d_texture_get_height(texture);
	unsigned int stride = vita2d_texture_get_stride(texture);
	SceGxmTextureFormat format = vita2d_texture_get_format(texture);
	float center_x = (float)width / 2.0f - 0.5f;
	float center_y = (float)height / 2.0f - 0.5f;
	float radius = (width < height ? (float)width : (float)height) / 2.0f;
	float inner = radius - 1.5f;
	unsigned int x, y;

	if (!pixels || width == 0 || height == 0)
		return;
	if (format != SCE_GXM_TEXTURE_FORMAT_A8B8G8R8 || stride < width * 4)
		return;

	for (y = 0; y < height; y++) {
		uint8_t *row = pixels + y * stride;

		for (x = 0; x < width; x++) {
			float dx = (float)x - center_x;
			float dy = (float)y - center_y;
			float distance = sqrtf(dx * dx + dy * dy);
			uint8_t *alpha = &row[x * 4 + 3];

			if (distance >= radius) {
				*alpha = 0;
			} else if (distance > inner) {
				float edge = (radius - distance) / (radius - inner);
				*alpha = (uint8_t)((float)(*alpha) * edge);
			}
		}
	}
}

static void icon_path(uint32_t index, char *out, size_t size)
{
	const vhdb_record *rec = vhdb_at(catalog, index);

	snprintf(out, size, "%s/%s", ICON_DIR, vhdb_str(catalog, rec->icon));
}

static int fetch_one(uint32_t index)
{
	const vhdb_record *rec = vhdb_at(catalog, index);
	const char *name = vhdb_str(catalog, rec->icon);
	char url[512];
	char path[512];

	if (!name[0])
		return 0;

	snprintf(url, sizeof(url), "%s%s", ICON_URL, name);
	icon_path(index, path, sizeof(path));

	return vhdb_net_fetch(url, path, NULL, NULL);
}

static int worker_main(SceSize args, void *argp)
{
	(void)args;
	(void)argp;

	while (running) {
		int index = wanted;

		if (index < 0) {
			sceKernelDelayThread(40 * 1000);
			continue;
		}

		states[index] = fetch_one((uint32_t)index) ? STATE_ON_DISK
							   : STATE_MISSING;
		wanted = -1;
	}
	return 0;
}

int vhdb_icons_have_pack(void)
{
	SceIoStat stat;

	memset(&stat, 0, sizeof(stat));
	return sceIoGetstat(ICON_PACK_MARK, &stat) >= 0;
}

int vhdb_icons_fetch_pack(vhdb_progress_fn progress, void *download_label,
			  vhdb_zip_progress unpack, void *unpack_label)
{
	SceUID mark;

	sceIoMkdir(ICON_DIR, 0777);

	if (!vhdb_net_fetch(ICON_PACK_URL, ICON_PACK_FILE, progress, download_label))
		return 0;

	if (!vhdb_zip_extract(ICON_PACK_FILE, ICON_DIR, unpack, unpack_label)) {
		sceIoRemove(ICON_PACK_FILE);
		return 0;
	}

	sceIoRemove(ICON_PACK_FILE);

	mark = sceIoOpen(ICON_PACK_MARK, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC,
			 0777);
	if (mark >= 0) {
		sceIoWrite(mark, "1", 1);
		sceIoClose(mark);
	}

	if (states)
		memset(states, STATE_UNKNOWN, entry_count);
	return 1;
}

int vhdb_icons_start(const vhdb_db *db)
{
	catalog = db;
	entry_count = vhdb_count(db);

	states = (uint8_t *)calloc(entry_count, 1);
	textures = (vita2d_texture **)calloc(entry_count, sizeof(*textures));
	recent = (uint32_t *)calloc(CACHE_LIMIT, sizeof(*recent));

	if (!states || !textures || !recent) {
		vhdb_icons_stop();
		return 0;
	}

	sceIoMkdir(ICON_DIR, 0777);

	running = 1;
	worker = sceKernelCreateThread("vhdb_icons", worker_main, 0x10000100, 512 * 1024,
				       0, 0, NULL);
	if (worker < 0) {
		running = 0;
		return 0;
	}
	sceKernelStartThread(worker, 0, NULL);
	return 1;
}

void vhdb_icons_stop(void)
{
	uint32_t i;

	if (running) {
		running = 0;
		sceKernelWaitThreadEnd(worker, NULL, NULL);
		sceKernelDeleteThread(worker);
	}

	if (textures) {
		for (i = 0; i < entry_count; i++) {
			if (textures[i])
				vita2d_free_texture(textures[i]);
		}
		free(textures);
		textures = NULL;
	}

	free(states);
	free(recent);
	states = NULL;
	recent = NULL;
}

static void forget_oldest(void)
{
	uint32_t victim;
	int i;

	if (recent_count < CACHE_LIMIT)
		return;

	victim = recent[0];
	if (textures[victim]) {
		vita2d_free_texture(textures[victim]);
		textures[victim] = NULL;
		states[victim] = STATE_ON_DISK;
	}

	for (i = 1; i < recent_count; i++)
		recent[i - 1] = recent[i];
	recent_count--;
}

static void remember(uint32_t index)
{
	int i;

	for (i = 0; i < recent_count; i++) {
		if (recent[i] != index)
			continue;
		for (; i + 1 < recent_count; i++)
			recent[i] = recent[i + 1];
		recent_count--;
		break;
	}

	forget_oldest();
	recent[recent_count++] = index;
}

vita2d_texture *vhdb_icon_for(uint32_t index)
{
	char path[512];
	SceIoStat stat;

	if (!states || index >= entry_count)
		return NULL;

	if (states[index] == STATE_LOADED) {
		remember(index);
		return textures[index];
	}
	if (states[index] == STATE_MISSING || states[index] == STATE_QUEUED)
		return NULL;

	icon_path(index, path, sizeof(path));

	if (states[index] == STATE_UNKNOWN) {
		memset(&stat, 0, sizeof(stat));
		if (sceIoGetstat(path, &stat) < 0) {
			if (wanted < 0) {
				states[index] = STATE_QUEUED;
				wanted = (int)index;
			}
			return NULL;
		}
		states[index] = STATE_ON_DISK;
	}

	textures[index] = vita2d_load_PNG_file(path);
	if (!textures[index]) {
		states[index] = STATE_MISSING;
		return NULL;
	}
	round_corners(textures[index]);

	states[index] = STATE_LOADED;
	remember(index);
	return textures[index];
}

void vhdb_icons_retry(uint32_t index)
{
	if (states && index < entry_count && states[index] == STATE_MISSING)
		states[index] = STATE_UNKNOWN;
}
