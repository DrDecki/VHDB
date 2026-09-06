#include "vhdb_vitainstall.h"
#include "vhdb_md5.h"
#include "vhdb_sha1.h"
#include "vhdb_vitanet.h"
#include "vhdb_zip.h"

#include "head_bin.h"

#include <psp2/io/dirent.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/promoterutil.h>
#include <psp2/sysmodule.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEMP_VPK VHDB_DATA_DIR "/download.vpk"
#define TEMP_DIR VHDB_DATA_DIR "/pkg"

static char last_error[192];

const char *vhdb_install_error(void)
{
	return last_error[0] ? last_error : "no error";
}

static void remove_tree(const char *path)
{
	SceUID dir = sceIoDopen(path);
	SceIoDirent entry;

	if (dir < 0) {
		sceIoRemove(path);
		return;
	}

	memset(&entry, 0, sizeof(entry));
	while (sceIoDread(dir, &entry) > 0) {
		char child[512];

		if (entry.d_name[0] == '.' &&
		    (entry.d_name[1] == 0 ||
		     (entry.d_name[1] == '.' && entry.d_name[2] == 0)))
			continue;

		snprintf(child, sizeof(child), "%s/%s", path, entry.d_name);
		if (SCE_S_ISDIR(entry.d_stat.st_mode))
			remove_tree(child);
		else
			sceIoRemove(child);

		memset(&entry, 0, sizeof(entry));
	}

	sceIoDclose(dir);
	sceIoRmdir(path);
}

static void fpkg_hmac(const uint8_t *data, unsigned int length, uint8_t hmac[16])
{
	vhdb_sha1_ctx ctx;
	uint8_t digest[20];
	uint8_t buffer[64];

	vhdb_sha1_init(&ctx);
	vhdb_sha1_update(&ctx, data, length);
	vhdb_sha1_final(&ctx, digest);

	memset(buffer, 0, sizeof(buffer));
	memcpy(&buffer[0], &digest[4], 8);
	memcpy(&buffer[8], &digest[4], 8);
	memcpy(&buffer[16], &digest[12], 4);
	buffer[20] = digest[16];
	buffer[21] = digest[1];
	buffer[22] = digest[2];
	buffer[23] = digest[3];
	memcpy(&buffer[24], &buffer[16], 8);

	vhdb_sha1_init(&ctx);
	vhdb_sha1_update(&ctx, buffer, 64);
	vhdb_sha1_final(&ctx, digest);
	memcpy(hmac, digest, 16);
}

static uint32_t big_endian(const uint8_t *p)
{
	return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
	       ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static int read_sfo_ids(const char *path, char *title_id, size_t title_size,
			char *content_id, size_t content_size)
{
	uint8_t *data;
	SceUID file;
	int size;
	uint32_t key_table, data_table, count, i;

	title_id[0] = 0;
	content_id[0] = 0;

	file = sceIoOpen(path, SCE_O_RDONLY, 0777);
	if (file < 0)
		return 0;

	size = sceIoLseek32(file, 0, SCE_SEEK_END);
	sceIoLseek32(file, 0, SCE_SEEK_SET);
	if (size < 20 || size > 65536) {
		sceIoClose(file);
		return 0;
	}

	data = (uint8_t *)malloc((size_t)size);
	if (!data) {
		sceIoClose(file);
		return 0;
	}
	if (sceIoRead(file, data, size) != size) {
		free(data);
		sceIoClose(file);
		return 0;
	}
	sceIoClose(file);

	key_table = *(uint32_t *)(data + 8);
	data_table = *(uint32_t *)(data + 12);
	count = *(uint32_t *)(data + 16);

	if (count > 1024 || 20 + count * 16 > (uint32_t)size) {
		free(data);
		return 0;
	}

	for (i = 0; i < count; i++) {
		const uint8_t *entry = data + 20 + i * 16;
		uint32_t key_offset = key_table + *(uint16_t *)entry;
		uint32_t length = *(uint32_t *)(entry + 4);
		uint32_t offset = data_table + *(uint32_t *)(entry + 12);
		const char *key;

		if (key_offset >= (uint32_t)size || offset >= (uint32_t)size)
			continue;
		key = (const char *)(data + key_offset);

		if (strcmp(key, "TITLE_ID") == 0) {
			if (length >= title_size)
				length = (uint32_t)title_size - 1;
			memcpy(title_id, data + offset, length);
			title_id[length] = 0;
		} else if (strcmp(key, "CONTENT_ID") == 0) {
			if (length >= content_size)
				length = (uint32_t)content_size - 1;
			memcpy(content_id, data + offset, length);
			content_id[length] = 0;
		}
	}

	free(data);
	return title_id[0] != 0;
}

static int write_head_bin(const char *directory)
{
	char package_dir[512];
	char head_path[512];
	char sfo_path[512];
	char title_id[16];
	char content_id[64];
	uint8_t *head;
	uint8_t hmac[16];
	uint32_t offset, length, out;
	SceUID file;

	snprintf(sfo_path, sizeof(sfo_path), "%s/sce_sys/param.sfo", directory);
	if (!read_sfo_ids(sfo_path, title_id, sizeof(title_id), content_id,
			  sizeof(content_id))) {
		snprintf(last_error, sizeof(last_error),
			 "the package has no usable param.sfo");
		return 0;
	}

	head = (uint8_t *)malloc(VHDB_HEAD_BIN_SIZE);
	if (!head) {
		snprintf(last_error, sizeof(last_error), "not enough memory");
		return 0;
	}
	memcpy(head, vhdb_head_bin, VHDB_HEAD_BIN_SIZE);

	if (!content_id[0])
		snprintf(content_id, sizeof(content_id),
			 "EP9000-%s_00-0000000000000000", title_id);
	memset(&head[0x30], 0, 48);
	strncpy((char *)&head[0x30], content_id, 47);

	length = big_endian(&head[0xD0]);
	fpkg_hmac(&head[0], length, hmac);
	memcpy(&head[length], hmac, 16);

	offset = big_endian(&head[0x08]);
	length = big_endian(&head[0x10]);
	out = big_endian(&head[0xD4]);
	fpkg_hmac(&head[offset], length - 64, hmac);
	memcpy(&head[out], hmac, 16);

	length = big_endian(&head[0xE8]);
	fpkg_hmac(&head[0], length, hmac);
	memcpy(&head[length], hmac, 16);

	snprintf(package_dir, sizeof(package_dir), "%s/sce_sys/package", directory);
	sceIoMkdir(package_dir, 0777);

	snprintf(head_path, sizeof(head_path), "%s/head.bin", package_dir);
	file = sceIoOpen(head_path, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
	if (file < 0) {
		free(head);
		snprintf(last_error, sizeof(last_error), "cannot write head.bin");
		return 0;
	}
	sceIoWrite(file, head, VHDB_HEAD_BIN_SIZE);
	sceIoClose(file);
	free(head);
	return 1;
}

static void promoter_start(void)
{
	uint32_t pointers[0x100];
	uint32_t paf_args[] = {0x400000, 0xEA60, 0x40000, 0, 0};

	memset(pointers, 0, sizeof(pointers));
	pointers[0] = 0;
	pointers[1] = (uint32_t)(uintptr_t)&pointers[0];

	sceSysmoduleLoadModuleInternalWithArg(SCE_SYSMODULE_INTERNAL_PAF,
					      sizeof(paf_args), paf_args,
					      (SceSysmoduleOpt *)pointers);
	sceSysmoduleLoadModuleInternal(SCE_SYSMODULE_INTERNAL_PROMOTER_UTIL);
	scePromoterUtilityInit();
}

static void promoter_stop(void)
{
	SceSysmoduleOpt option;

	scePromoterUtilityExit();
	sceSysmoduleUnloadModuleInternal(SCE_SYSMODULE_INTERNAL_PROMOTER_UTIL);
	memset(&option, 0, sizeof(option));
	sceSysmoduleUnloadModuleInternalWithArg(SCE_SYSMODULE_INTERNAL_PAF, 0, NULL,
						&option);
}

int vhdb_install_package(const char *directory)
{
	int state = 0;
	int result = 0;

	promoter_start();

	if (scePromoterUtilityPromotePkg(directory, 0) < 0) {
		promoter_stop();
		snprintf(last_error, sizeof(last_error), "the installer refused it");
		return 0;
	}

	do {
		sceKernelDelayThread(100 * 1000);
		if (scePromoterUtilityGetState(&state) < 0)
			break;
	} while (state);

	scePromoterUtilityGetResult(&result);
	promoter_stop();

	if (result < 0) {
		snprintf(last_error, sizeof(last_error),
			 "the installer failed (0x%08X)", (unsigned int)result);
		return 0;
	}
	return 1;
}

int vhdb_install_from_url(const char *url, const uint8_t expected[16],
			  vhdb_progress_fn progress, void *download_label,
			  vhdb_zip_progress unpack, void *unpack_label)
{
	uint8_t digest[16];
	static const uint8_t zero[16] = {0};

	last_error[0] = 0;

	if (!vhdb_net_fetch(url, TEMP_VPK, progress, download_label)) {
		snprintf(last_error, sizeof(last_error), "%s", vhdb_net_error());
		return 0;
	}

	if (expected && memcmp(expected, zero, 16) != 0) {
		if (!vhdb_md5_file(TEMP_VPK, digest)) {
			snprintf(last_error, sizeof(last_error),
				 "cannot read the download back");
			sceIoRemove(TEMP_VPK);
			return 0;
		}
		if (memcmp(digest, expected, 16) != 0) {
			snprintf(last_error, sizeof(last_error),
				 "the download does not match the catalog");
			sceIoRemove(TEMP_VPK);
			return 0;
		}
	}

	remove_tree(TEMP_DIR);
	sceIoMkdir(TEMP_DIR, 0777);

	if (!vhdb_zip_extract(TEMP_VPK, TEMP_DIR, unpack, unpack_label)) {
		snprintf(last_error, sizeof(last_error), "%s", vhdb_zip_error());
		sceIoRemove(TEMP_VPK);
		remove_tree(TEMP_DIR);
		return 0;
	}

	sceIoRemove(TEMP_VPK);

	if (!write_head_bin(TEMP_DIR)) {
		remove_tree(TEMP_DIR);
		return 0;
	}

	if (!vhdb_install_package(TEMP_DIR)) {
		remove_tree(TEMP_DIR);
		return 0;
	}

	remove_tree(TEMP_DIR);
	return 1;
}
