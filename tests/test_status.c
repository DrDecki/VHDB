#include "vhdb_status.h"
#include "vhdb_sfo.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;

static void expect_cmp(const char *installed, const char *catalogue, int want)
{
	int got = vhdb_version_compare(installed, catalogue);
	static const char *names[] = {"SAME", "OLDER", "NEWER", "UNKNOWN"};

	if (got != want) {
		printf("FAIL compare(%s, %s) = %s, expected %s\n", installed,
		       catalogue, names[got], names[want]);
		failures++;
	}
}

static int fake_exists(const char *path, void *user)
{
	const char *present = (const char *)user;
	return strcmp(path, present) == 0;
}

int main(void)
{
	printf("version comparison\n");
	expect_cmp("1.0", "1.0", VHDB_VER_SAME);
	expect_cmp("v.1.0.0", "1.0.0", VHDB_VER_SAME);
	expect_cmp("1.0", "1.1", VHDB_VER_NEWER);
	expect_cmp("1.2", "1.10", VHDB_VER_NEWER);
	expect_cmp("2.0", "1.9", VHDB_VER_OLDER);
	expect_cmp("v.1.0", "v.1.0.1", VHDB_VER_NEWER);
	expect_cmp("1.0.1", "1.0", VHDB_VER_OLDER);
	expect_cmp("1.0", "1.0.0", VHDB_VER_SAME);
	expect_cmp("20210401", "20220101", VHDB_VER_NEWER);
	expect_cmp("v.0-beta", "v.0-beta", VHDB_VER_SAME);
	expect_cmp("v.0-beta", "v.0-rc", VHDB_VER_UNKNOWN);
	expect_cmp("1.0b", "1.0c", VHDB_VER_UNKNOWN);
	expect_cmp("Rev4", "Rev5", VHDB_VER_NEWER);
	expect_cmp("", "1.0", VHDB_VER_UNKNOWN);
	expect_cmp("1.0", "", VHDB_VER_UNKNOWN);
	expect_cmp("00.00", "v.1.2.3", VHDB_VER_UNKNOWN);
	expect_cmp("0.00", "1.0", VHDB_VER_UNKNOWN);
	expect_cmp("02.02", "v.2.02", VHDB_VER_SAME);
	expect_cmp("00.57", "v.0.57", VHDB_VER_SAME);
	expect_cmp("01.00", "v.1.1", VHDB_VER_NEWER);

	printf("needs parsing\n");
	{
		const char *needs = "gamefiles|ux0:data/fc/Data|Far Cry PC data folder;"
				    "plugin||kubridge.skprx in ur0:tai;"
				    "firmware||3.60 or higher";
		vhdb_need need;
		int total = vhdb_needs_count(needs);

		if (total != 3) {
			printf("FAIL needs count = %d, expected 3\n", total);
			failures++;
		}

		vhdb_needs_get(needs, 0, &need);
		if (need.kind != VHDB_NEED_KIND_GAMEFILES ||
		    strcmp(need.path, "ux0:data/fc/Data") != 0 ||
		    strcmp(need.text, "Far Cry PC data folder") != 0) {
			printf("FAIL needs entry 0: kind %d path %s text %s\n",
			       need.kind, need.path, need.text);
			failures++;
		}

		vhdb_needs_get(needs, 1, &need);
		if (need.kind != VHDB_NEED_KIND_PLUGIN || need.path[0] != 0) {
			printf("FAIL needs entry 1\n");
			failures++;
		}

		vhdb_needs_get(needs, 2, &need);
		if (need.kind != VHDB_NEED_KIND_FIRMWARE ||
		    strcmp(need.text, "3.60 or higher") != 0) {
			printf("FAIL needs entry 2\n");
			failures++;
		}

		if (vhdb_needs_get(needs, 3, &need) != 0) {
			printf("FAIL needs entry 3 should not exist\n");
			failures++;
		}

		if (vhdb_needs_count("") != 0 || vhdb_needs_count(NULL) != 0) {
			printf("FAIL empty needs\n");
			failures++;
		}

		vhdb_needs_check(needs, 0, &need, fake_exists,
				 (void *)"ux0:data/fc/Data");
		if (need.state != VHDB_NEED_MET) {
			printf("FAIL needs check should be met\n");
			failures++;
		}
		vhdb_needs_check(needs, 0, &need, fake_exists, (void *)"nowhere");
		if (need.state != VHDB_NEED_MISSING) {
			printf("FAIL needs check should be missing\n");
			failures++;
		}
		vhdb_needs_check(needs, 1, &need, fake_exists, (void *)"nowhere");
		if (need.state != VHDB_NEED_UNCHECKED) {
			printf("FAIL needs without a path is unchecked\n");
			failures++;
		}
	}

	printf("param.sfo\n");
	{
		vhdb_sfo sfo;
		int rc = vhdb_sfo_parse_file("fake_param.sfo", &sfo);
		if (rc != VHDB_SFO_OK) {
			printf("FAIL sfo parse returned %d\n", rc);
			failures++;
		} else {
			if (strcmp(sfo.title_id, "TMCV00001") != 0 ||
			    strcmp(sfo.app_ver, "01.02") != 0 ||
			    strcmp(sfo.title, "Zelda Minish Cap") != 0 ||
			    strcmp(sfo.category, "gd") != 0) {
				printf("FAIL sfo fields: %s %s %s %s\n",
				       sfo.title_id, sfo.app_ver, sfo.title,
				       sfo.category);
				failures++;
			}
		}

		{
			uint8_t junk[64];
			memset(junk, 0xAB, sizeof(junk));
			if (vhdb_sfo_parse(junk, sizeof(junk), &sfo) != VHDB_SFO_ERR_MAGIC) {
				printf("FAIL junk sfo should fail on magic\n");
				failures++;
			}
			if (vhdb_sfo_parse(junk, 4, &sfo) != VHDB_SFO_ERR_SIZE) {
				printf("FAIL short sfo should fail on size\n");
				failures++;
			}
		}
	}

	printf("install policy\n");
	{
		vhdb_record rec;
		vhdb_status st;

		memset(&rec, 0, sizeof(rec));
		memset(&st, 0, sizeof(st));

		rec.platform = VHDB_PLATFORM_VITA;
		if (vhdb_install_kind(&rec) != VHDB_INSTALL_VPK ||
		    !vhdb_can_install(&rec, VHDB_CLIENT_VITA)) {
			printf("FAIL vita vpk should install on the vita\n");
			failures++;
		}

		rec.platform = VHDB_PLATFORM_PLUGIN;
		if (vhdb_can_install(&rec, VHDB_CLIENT_VITA)) {
			printf("FAIL plugins must not install on the vita\n");
			failures++;
		}
		if (!vhdb_can_install(&rec, VHDB_CLIENT_PC)) {
			printf("FAIL plugins must download on the pc\n");
			failures++;
		}
		if (strcmp(vhdb_list_label(&rec, &st, VHDB_CLIENT_VITA), "PC ONLY") != 0) {
			printf("FAIL plugin label on the vita\n");
			failures++;
		}

		rec.platform = VHDB_PLATFORM_TOOL;
		if (vhdb_can_install(&rec, VHDB_CLIENT_VITA)) {
			printf("FAIL pc tools must not install on the vita\n");
			failures++;
		}

		rec.platform = VHDB_PLATFORM_VITA;
		st.state = VHDB_STATE_INSTALLED;
		st.needs_total = 2;
		st.needs_checked = 2;
		st.needs_met = 1;
		if (strcmp(vhdb_list_label(&rec, &st, VHDB_CLIENT_VITA), "NEEDS FILES") != 0) {
			printf("FAIL installed with a missing file\n");
			failures++;
		}
		st.state = VHDB_STATE_UPDATE;
		if (strcmp(vhdb_list_label(&rec, &st, VHDB_CLIENT_VITA), "UPDATE") != 0) {
			printf("FAIL update wins over missing files\n");
			failures++;
		}
		st.state = VHDB_STATE_NOT_INSTALLED;
		if (strcmp(vhdb_list_label(&rec, &st, VHDB_CLIENT_VITA), "NOT INSTALLED") != 0) {
			printf("FAIL not installed keeps its label\n");
			failures++;
		}
	}

	printf("content hashes\n");
	{
		vhdb_db db;
		vhdb_record rec;
		vhdb_installed inst;
		vhdb_status st;

		memset(&db, 0, sizeof(db));
		memset(&rec, 0, sizeof(rec));
		memset(&inst, 0, sizeof(inst));

		memset(rec.eboot, 0xAA, 16);
		rec.flags = VHDB_FLAG_HAS_EBOOT;
		snprintf(inst.version, sizeof(inst.version), "00.00");

		memset(inst.eboot, 0xAA, 16);
		inst.has_eboot = 1;
		vhdb_status_of(&db, &rec, &inst, &st, NULL, NULL);
		if (st.state != VHDB_STATE_INSTALLED || !st.by_content) {
			printf("FAIL matching eboot should read as installed\n");
			failures++;
		}

		memset(inst.eboot, 0xBB, 16);
		vhdb_status_of(&db, &rec, &inst, &st, NULL, NULL);
		if (st.state != VHDB_STATE_UPDATE || !st.by_content) {
			printf("FAIL differing eboot should read as an update\n");
			failures++;
		}

		memset(rec.aux, 0xCC, 16);
		rec.flags |= VHDB_FLAG_HAS_AUX;
		rec.aux_kind = VHDB_AUX_GODOT;
		memset(inst.eboot, 0xAA, 16);
		memset(inst.aux, 0xCC, 16);
		inst.has_aux = 1;
		vhdb_status_of(&db, &rec, &inst, &st, NULL, NULL);
		if (st.state != VHDB_STATE_INSTALLED) {
			printf("FAIL matching godot data should read as installed\n");
			failures++;
		}

		memset(inst.aux, 0xDD, 16);
		vhdb_status_of(&db, &rec, &inst, &st, NULL, NULL);
		if (st.state != VHDB_STATE_UPDATE) {
			printf("FAIL changed godot data should read as an update\n");
			failures++;
		}

		memset(&inst, 0, sizeof(inst));
		snprintf(inst.version, sizeof(inst.version), "00.00");
		vhdb_status_of(&db, &rec, &inst, &st, NULL, NULL);
		if (st.state != VHDB_STATE_UNKNOWN_VERSION || st.by_content) {
			printf("FAIL without hashes it falls back to versions\n");
			failures++;
		}
	}

	printf("\n%s\n", failures == 0 ? "all checks passed" : "checks failed");
	return failures == 0 ? 0 : 1;
}
