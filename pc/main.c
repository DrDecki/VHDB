#include "vhdb.h"
#include "vhdb_config.h"
#include "vhdb_install.h"
#include "vhdb_installed.h"
#include "vhdb_net.h"
#include "vhdb_status.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static vhdb_config config;

static void usage(void)
{
	printf("vhdb, a client for the VitaHomebrewDB catalogue\n\n");
	printf("  vhdb setup              configure where downloads go\n");
	printf("  vhdb config             show the current configuration\n");
	printf("  vhdb sync               fetch the catalogue if it changed\n");
	printf("  vhdb list [words]       list entries, filtered by words\n");
	printf("  vhdb show <id>          show one entry in full\n");
	printf("  vhdb install <id>       download and send to the console\n");
	printf("  vhdb installed          what is installed, and what is old\n");
	printf("  vhdb scan [-f]          read the console over ftp, -f rehashes\n");
	printf("\ninstall options\n");
	printf("  --data                  also fetch the data file, if any\n");
	printf("\nlist options\n");
	printf("  -n <count>              how many to print, default 25\n");
	printf("  -c <category>           games, ports, utilities, emulators\n");
	printf("  -p <platform>           vita, psp, plugin, tool\n");
	printf("  -u                      order by date instead of name\n");
}

static uint32_t read_u32(const uint8_t *p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
	       ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int local_hash(const char *path, uint32_t *out)
{
	FILE *file = fopen(path, "rb");
	uint8_t header[VHDB_HEADER_SIZE];

	if (!file)
		return 0;
	if (fread(header, 1, sizeof(header), file) != sizeof(header)) {
		fclose(file);
		return 0;
	}
	fclose(file);
	if (read_u32(header) != VHDB_MAGIC)
		return 0;
	*out = read_u32(header + 40);
	return 1;
}

static int open_catalogue(vhdb_db *db)
{
	char path[512];
	int rc;

	if (!vhdb_catalogue_file(path, sizeof(path)))
		return 0;
	rc = vhdb_load(db, path, 0);
	if (rc != VHDB_OK) {
		printf("no catalogue yet (%s), run: vhdb sync\n", vhdb_error(rc));
		return 0;
	}
	return 1;
}

static int ask(const char *label, const char *current, char *out, size_t size)
{
	char line[256];

	printf("%-28s [%s]: ", label, current && current[0] ? current : "empty");
	fflush(stdout);
	if (!fgets(line, sizeof(line), stdin))
		return 0;
	line[strcspn(line, "\r\n")] = 0;
	if (line[0])
		snprintf(out, size, "%s", line);
	return 1;
}

static int command_setup(void)
{
	char kind[32];
	char port[32];

	vhdb_config_load(&config);

	printf("Where should downloads go?\n");
	printf("  ftp     push straight to the console over VitaShell\n");
	printf("  folder  write into local folders, for a card reader\n");
	printf("  none    just download, sort it out yourself\n\n");

	snprintf(kind, sizeof(kind), "%s", vhdb_target_name(config.target));
	if (!ask("kind", kind, kind, sizeof(kind)))
		return 1;
	if (strcmp(kind, "ftp") == 0)
		config.target = VHDB_TARGET_FTP;
	else if (strcmp(kind, "folder") == 0)
		config.target = VHDB_TARGET_FOLDER;
	else
		config.target = VHDB_TARGET_NONE;

	if (config.target == VHDB_TARGET_FTP) {
		printf("\nVitaShell shows the address and port when FTP is running.\n");
		printf("The address changes with DHCP, so check it if a transfer fails.\n\n");
		ask("host", config.host, config.host, sizeof(config.host));
		snprintf(port, sizeof(port), "%d", config.port);
		ask("port", port, port, sizeof(port));
		config.port = atoi(port);
		if (config.port <= 0)
			config.port = 1337;
		ask("user, empty for anonymous", config.user, config.user,
		    sizeof(config.user));
		ask("password", config.pass, config.pass, sizeof(config.pass));
	}

	if (config.target != VHDB_TARGET_NONE) {
		printf("\n");
		ask("vpk folder", config.vpk, config.vpk, sizeof(config.vpk));
		ask("data folder", config.data, config.data, sizeof(config.data));
		ask("psp folder", config.pspemu, config.pspemu, sizeof(config.pspemu));
	}

	printf("\nPlugins and PC tools are never installed for you.\n");
	printf("They are downloaded here and you move them yourself.\n\n");
	ask("download folder", config.keep, config.keep, sizeof(config.keep));

	if (!vhdb_config_save(&config)) {
		printf("could not write the configuration\n");
		return 1;
	}

	{
		char path[512];
		vhdb_config_file(path, sizeof(path));
		printf("\nsaved to %s\n", path);
	}
	return 0;
}

static int command_config(void)
{
	char path[512];

	vhdb_config_load(&config);
	vhdb_config_file(path, sizeof(path));
	printf("config      %s\n", path);
	vhdb_catalogue_file(path, sizeof(path));
	printf("catalogue   %s\n", path);
	printf("url         %s\n", config.catalogue_url);
	printf("target      %s\n", vhdb_target_name(config.target));
	if (config.target == VHDB_TARGET_FTP)
		printf("console     %s:%d\n", config.host, config.port);
	printf("vpk         %s\n", config.vpk);
	printf("data        %s\n", config.data);
	printf("psp         %s\n", config.pspemu);
	printf("downloads   %s\n", config.keep);
	return 0;
}

static int command_sync(int force)
{
	uint8_t header[VHDB_HEADER_SIZE];
	char path[512];
	char dir[256];
	uint32_t remote, local;
	vhdb_db db;

	vhdb_config_load(&config);
	vhdb_config_dir(dir, sizeof(dir));
	vhdb_make_dirs(dir);
	vhdb_catalogue_file(path, sizeof(path));

	if (vhdb_net_init() != VHDB_NET_OK) {
		printf("cannot start networking\n");
		return 1;
	}

	if (!force && vhdb_net_head_bytes(config.catalogue_url, header,
					  sizeof(header)) == VHDB_NET_OK &&
	    read_u32(header) == VHDB_MAGIC) {
		remote = read_u32(header + 40);
		if (local_hash(path, &local) && local == remote) {
			printf("already current, catalogue %08x built %u\n",
			       local, read_u32(header + 8));
			vhdb_net_shutdown();
			return 0;
		}
		printf("catalogue changed, %u entries to fetch\n",
		       read_u32(header + 12));
	}

	printf("downloading\n");
	if (vhdb_net_download(config.catalogue_url, path, 1) != VHDB_NET_OK) {
		printf("download failed: %s\n", vhdb_net_last_error());
		vhdb_net_shutdown();
		return 1;
	}
	vhdb_net_shutdown();

	if (vhdb_load(&db, path, 1) != VHDB_OK) {
		printf("the downloaded file did not verify, keeping nothing\n");
		remove(path);
		return 1;
	}

	printf("%u entries, built %u, checksum %08x verified\n", vhdb_count(&db),
	       db.header->built, db.header->catalog_hash);
	vhdb_free(&db);
	return 0;
}

static int platform_from_word(const char *word)
{
	if (strcasecmp(word, "vita") == 0)
		return VHDB_PLATFORM_VITA;
	if (strcasecmp(word, "psp") == 0)
		return VHDB_PLATFORM_PSP;
	if (strcasecmp(word, "plugin") == 0)
		return VHDB_PLATFORM_PLUGIN;
	if (strcasecmp(word, "tool") == 0)
		return VHDB_PLATFORM_TOOL;
	return -1;
}

static int type_from_word(const char *word)
{
	if (strcasecmp(word, "games") == 0)
		return VHDB_TYPE_GAME;
	if (strcasecmp(word, "ports") == 0)
		return VHDB_TYPE_PORT;
	if (strcasecmp(word, "utilities") == 0)
		return VHDB_TYPE_UTILITY;
	if (strcasecmp(word, "emulators") == 0)
		return VHDB_TYPE_EMULATOR;
	return -1;
}

static int matches(const vhdb_db *db, const vhdb_record *rec, char **words,
		   int word_count)
{
	int i;

	for (i = 0; i < word_count; i++) {
		const char *name = vhdb_str(db, rec->name);
		const char *author = vhdb_str(db, rec->author);
		if (!strcasestr(name, words[i]) && !strcasestr(author, words[i]))
			return 0;
	}
	return 1;
}

static void print_row(const vhdb_db *db, const vhdb_record *rec)
{
	char titleid[13];
	char name[42];

	vhdb_titleid(rec, titleid, sizeof(titleid));
	snprintf(name, sizeof(name), "%s", vhdb_str(db, rec->name));

	printf("%-6u %-40s %-12s %-18s %-10s %6.1f MB %s\n", rec->id,
	       name, vhdb_str(db, rec->version), vhdb_str(db, rec->author),
	       titleid[0] ? titleid : "",
	       (double)rec->size / 1048576.0,
	       vhdb_install_label(vhdb_install_kind(rec)));
}

static int command_list(int argc, char **argv)
{
	vhdb_db db;
	int limit = 25, by_date = 0, want_type = -1, want_platform = -1;
	char *words[8];
	int word_count = 0;
	int i, shown = 0, total = 0;

	for (i = 0; i < argc; i++) {
		if (strcmp(argv[i], "-n") == 0 && i + 1 < argc)
			limit = atoi(argv[++i]);
		else if (strcmp(argv[i], "-c") == 0 && i + 1 < argc)
			want_type = type_from_word(argv[++i]);
		else if (strcmp(argv[i], "-p") == 0 && i + 1 < argc)
			want_platform = platform_from_word(argv[++i]);
		else if (strcmp(argv[i], "-u") == 0)
			by_date = 1;
		else if (word_count < 8)
			words[word_count++] = argv[i];
	}

	if (!open_catalogue(&db))
		return 1;

	for (i = 0; i < (int)vhdb_count(&db); i++) {
		const vhdb_record *rec = by_date ? vhdb_by_date(&db, (uint32_t)i)
						 : vhdb_by_name(&db, (uint32_t)i);
		if (want_platform >= 0 && rec->platform != want_platform)
			continue;
		if (want_type >= 0 && rec->type != want_type)
			continue;
		if (!matches(&db, rec, words, word_count))
			continue;
		total++;
		if (shown < limit) {
			print_row(&db, rec);
			shown++;
		}
	}

	if (total > shown)
		printf("\n%d of %d shown, use -n to see more\n", shown, total);
	else
		printf("\n%d entries\n", total);

	vhdb_free(&db);
	return 0;
}

static void print_wrapped(const char *text, int width)
{
	int column = 0;

	while (*text) {
		const char *end = text;
		int length = 0;

		while (*end && *end != ' ' && *end != '\n') {
			end++;
			length++;
		}
		if (column + length > width) {
			printf("\n");
			column = 0;
		}
		printf("%.*s", length, text);
		column += length;
		text = end;
		while (*text == ' ' || *text == '\n') {
			if (*text == '\n') {
				printf("\n");
				column = 0;
			} else if (column > 0) {
				printf(" ");
				column++;
			}
			text++;
		}
	}
	printf("\n");
}

static int command_show(const char *wanted)
{
	vhdb_db db;
	const vhdb_record *found = NULL;
	uint32_t i;

	if (!open_catalogue(&db))
		return 1;

	if (wanted[0] >= '0' && wanted[0] <= '9') {
		uint32_t want_id = (uint32_t)strtoul(wanted, NULL, 10);
		for (i = 0; i < vhdb_count(&db); i++) {
			const vhdb_record *rec = vhdb_at(&db, i);
			if (rec->id == want_id) {
				found = rec;
				break;
			}
		}
	}
	if (!found) {
		for (i = 0; i < vhdb_count(&db); i++) {
			const vhdb_record *rec = vhdb_by_name(&db, i);
			if (strcasecmp(vhdb_str(&db, rec->name), wanted) == 0) {
				found = rec;
				break;
			}
		}
	}
	if (!found) {
		printf("no entry with id or name %s\n", wanted);
		vhdb_free(&db);
		return 1;
	}

	{
		char titleid[13];
		const char *needs;
		int count, n;

		vhdb_titleid(found, titleid, sizeof(titleid));
		printf("%s\n", vhdb_str(&db, found->name));
		printf("id %u, version %s, by %s\n", found->id,
		       vhdb_str(&db, found->version), vhdb_str(&db, found->author));
		printf("%s, %s, %.2f MB, released %u\n",
		       vhdb_install_label(vhdb_install_kind(found)),
		       vhdb_type_name(found->type),
		       (double)found->size / 1048576.0, found->date);
		if (titleid[0])
			printf("title id %s\n", titleid);
		printf("\n");
		print_wrapped(vhdb_str(&db, found->long_description), 78);

		needs = vhdb_str(&db, found->needs);
		count = vhdb_needs_count(needs);
		if (count > 0) {
			printf("\nrequirements\n");
			for (n = 0; n < count; n++) {
				vhdb_need need;
				vhdb_needs_get(needs, n, &need);
				printf("  %s%s%s\n", need.text,
				       need.path[0] ? "  at " : "", need.path);
			}
		} else if (vhdb_str(&db, found->requirements)[0]) {
			printf("\nrequirements\n");
			print_wrapped(vhdb_str(&db, found->requirements), 78);
		}

		if (vhdb_has_data_file(found))
			printf("\ndata file  %s\n", vhdb_str(&db, found->data_url));
		printf("download   %s\n", vhdb_str(&db, found->url));
		if (vhdb_str(&db, found->source)[0])
			printf("source     %s\n", vhdb_str(&db, found->source));
	}

	vhdb_free(&db);
	return 0;
}

int vhdb_pc_scan(const vhdb_config *cfg, const vhdb_db *db,
		 vhdb_installed_list *installed, int rehash);

static int installed_path(char *out, size_t size)
{
	char dir[256];

	if (!vhdb_config_dir(dir, sizeof(dir)))
		return 0;
	return snprintf(out, size, "%s/installed.txt", dir) < (int)size;
}

static const vhdb_record *find_entry(const vhdb_db *db, const char *wanted)
{
	uint32_t i;

	if (wanted[0] >= '0' && wanted[0] <= '9') {
		uint32_t want_id = (uint32_t)strtoul(wanted, NULL, 10);
		for (i = 0; i < vhdb_count(db); i++) {
			const vhdb_record *rec = vhdb_at(db, i);
			if (rec->id == want_id)
				return rec;
		}
	}
	for (i = 0; i < vhdb_count(db); i++) {
		const vhdb_record *rec = vhdb_by_name(db, i);
		if (strcasecmp(vhdb_str(db, rec->name), wanted) == 0)
			return rec;
	}
	return NULL;
}

static int command_install(int argc, char **argv)
{
	vhdb_db db;
	vhdb_installed_list installed;
	char path[512];
	const vhdb_record *rec;
	int with_data = 0, i, failed = 0, done = 0;

	for (i = 0; i < argc; i++) {
		if (strcmp(argv[i], "--data") == 0)
			with_data = 1;
	}

	vhdb_config_load(&config);
	if (!open_catalogue(&db))
		return 1;

	installed_path(path, sizeof(path));
	vhdb_installed_load(&installed, path);

	if (vhdb_net_init() != VHDB_NET_OK) {
		printf("cannot start networking\n");
		vhdb_free(&db);
		return 1;
	}

	for (i = 0; i < argc; i++) {
		if (argv[i][0] == '-')
			continue;
		rec = find_entry(&db, argv[i]);
		if (!rec) {
			printf("no entry with id or name %s\n", argv[i]);
			failed++;
			continue;
		}
		if (!vhdb_pc_install(&config, &db, rec, &installed, with_data))
			failed++;
		else
			done++;
	}

	vhdb_net_shutdown();
	if (done > 0)
		vhdb_installed_save(&installed, path);
	vhdb_installed_free(&installed);
	vhdb_free(&db);
	return failed ? 1 : 0;
}

static int command_scan(int rehash)
{
	vhdb_db db;
	vhdb_installed_list installed;
	char path[512];
	int ok;

	vhdb_config_load(&config);
	if (!open_catalogue(&db))
		return 1;

	installed_path(path, sizeof(path));
	vhdb_installed_load(&installed, path);

	if (vhdb_net_init() != VHDB_NET_OK) {
		printf("cannot start networking\n");
		vhdb_free(&db);
		return 1;
	}

	ok = vhdb_pc_scan(&config, &db, &installed, rehash);
	vhdb_net_shutdown();

	if (ok)
		vhdb_installed_save(&installed, path);
	vhdb_installed_free(&installed);
	vhdb_free(&db);
	return ok ? 0 : 1;
}

static int command_installed(void)
{
	vhdb_db db;
	vhdb_installed_list installed;
	char path[512];
	int i, updates = 0;

	installed_path(path, sizeof(path));
	if (!vhdb_installed_load(&installed, path) || installed.count == 0) {
		printf("nothing installed through this client yet\n");
		vhdb_installed_free(&installed);
		return 0;
	}
	if (!open_catalogue(&db)) {
		vhdb_installed_free(&installed);
		return 1;
	}

	for (i = 0; i < installed.count; i++) {
		const vhdb_installed *entry = &installed.items[i];
		const vhdb_record *rec = NULL;
		vhdb_status status;
		uint32_t n;

		for (n = 0; n < vhdb_count(&db); n++) {
			const vhdb_record *candidate = vhdb_at(&db, n);
			if (candidate->id == entry->id) {
				rec = candidate;
				break;
			}
		}
		if (!rec) {
			printf("%-40s %-12s %s\n", "gone from the catalogue",
			       entry->version, entry->titleid);
			continue;
		}

		vhdb_status_of(&db, rec, entry, &status, NULL, NULL);
		if (status.state == VHDB_STATE_UPDATE)
			updates++;
		printf("%-6u %-38s %-11s -> %-11s %-8s %s\n", rec->id,
		       vhdb_str(&db, rec->name), entry->version,
		       vhdb_str(&db, rec->version),
		       status.by_content ? "content" : (entry->from_console ? "version" : "client"),
		       vhdb_list_label(rec, &status, VHDB_CLIENT_PC));
	}

	printf("\n%d entries, %d with an update\n", installed.count, updates);
	vhdb_free(&db);
	vhdb_installed_free(&installed);
	return 0;
}

int main(int argc, char **argv)
{
	if (argc < 2) {
		usage();
		return 0;
	}

	if (strcmp(argv[1], "setup") == 0)
		return command_setup();
	if (strcmp(argv[1], "config") == 0)
		return command_config();
	if (strcmp(argv[1], "sync") == 0)
		return command_sync(argc > 2 && strcmp(argv[2], "-f") == 0);
	if (strcmp(argv[1], "list") == 0)
		return command_list(argc - 2, argv + 2);
	if (strcmp(argv[1], "show") == 0 && argc > 2)
		return command_show(argv[2]);
	if (strcmp(argv[1], "install") == 0 && argc > 2)
		return command_install(argc - 2, argv + 2);
	if (strcmp(argv[1], "installed") == 0)
		return command_installed();
	if (strcmp(argv[1], "scan") == 0)
		return command_scan(argc > 2 && strcmp(argv[2], "-f") == 0);

	usage();
	return 1;
}
