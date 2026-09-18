#include "vhdb_vitainstall.h"
#include "vhdb_md5.h"
#include "vhdb_promote.h"
#include "vhdb_vitanet.h"
#include "vhdb_zip.h"

#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>

#include <stdio.h>
#include <string.h>

#define TEMP_VPK VHDB_DATA_DIR "/download.vpk"
#define TEMP_DIR VHDB_DATA_DIR "/pkg"

static char last_error[192];
static int mismatched;

const char *vhdb_install_error(void)
{
	return last_error[0] ? last_error : "no error";
}

int vhdb_install_mismatched(void)
{
	return mismatched;
}

static int download_and_unpack(const char *url, const uint8_t expected[16],
			       vhdb_progress_fn progress, void *download_label,
			       vhdb_zip_progress unpack, void *unpack_label)
{
	uint8_t digest[16];
	static const uint8_t zero[16] = {0};

	last_error[0] = 0;
	mismatched = 0;

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
			mismatched = 1;
			sceIoRemove(TEMP_VPK);
			return 0;
		}
	}

	vhdb_remove_tree(TEMP_DIR);
	sceIoMkdir(TEMP_DIR, 0777);

	if (!vhdb_zip_extract(TEMP_VPK, TEMP_DIR, unpack, unpack_label)) {
		snprintf(last_error, sizeof(last_error), "%s", vhdb_zip_error());
		sceIoRemove(TEMP_VPK);
		vhdb_remove_tree(TEMP_DIR);
		return 0;
	}

	sceIoRemove(TEMP_VPK);
	return 1;
}

int vhdb_install_package(const char *directory)
{
	if (!vhdb_promote_directory(directory)) {
		snprintf(last_error, sizeof(last_error), "%s", vhdb_promote_error());
		return 0;
	}
	return 1;
}

int vhdb_install_from_url(const char *url, const uint8_t expected[16],
			  vhdb_progress_fn progress, void *download_label,
			  vhdb_zip_progress unpack, void *unpack_label)
{
	if (!download_and_unpack(url, expected, progress, download_label, unpack,
				 unpack_label))
		return 0;

	if (!vhdb_install_package(TEMP_DIR)) {
		vhdb_remove_tree(TEMP_DIR);
		return 0;
	}

	vhdb_remove_tree(TEMP_DIR);
	return 1;
}

int vhdb_stage_update(const char *url, const uint8_t expected[16],
		      vhdb_progress_fn progress, void *download_label,
		      vhdb_zip_progress unpack, void *unpack_label)
{
	return download_and_unpack(url, expected, progress, download_label, unpack,
				   unpack_label);
}
