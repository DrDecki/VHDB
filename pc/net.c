#include "vhdb_net.h"
#include "vhdb_md5.h"

#include <curl/curl.h>
#include <stdio.h>
#include <string.h>

static char last_error[CURL_ERROR_SIZE];

typedef struct {
	uint8_t *data;
	size_t size;
	size_t used;
} memory_sink;

typedef struct {
	FILE *file;
	double last_shown;
	int show;
} file_sink;

int vhdb_net_init(void)
{
	last_error[0] = 0;
	return curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK ? VHDB_NET_OK
							         : VHDB_NET_ERR;
}

void vhdb_net_shutdown(void)
{
	curl_global_cleanup();
}

const char *vhdb_net_last_error(void)
{
	return last_error[0] ? last_error : "no error";
}

static size_t write_memory(void *chunk, size_t size, size_t count, void *user)
{
	memory_sink *sink = (memory_sink *)user;
	size_t total = size * count;
	size_t room = sink->size - sink->used;

	if (total > room)
		total = room;
	if (total == 0)
		return 0;
	memcpy(sink->data + sink->used, chunk, total);
	sink->used += total;
	return size * count;
}

static size_t write_file(void *chunk, size_t size, size_t count, void *user)
{
	file_sink *sink = (file_sink *)user;
	return fwrite(chunk, size, count, sink->file);
}

static int progress(void *user, curl_off_t total, curl_off_t now, curl_off_t a,
		    curl_off_t b)
{
	file_sink *sink = (file_sink *)user;
	double percent;

	(void)a;
	(void)b;

	if (!sink->show || total <= 0)
		return 0;

	percent = (double)now * 100.0 / (double)total;
	if (percent - sink->last_shown < 2.0 && percent < 100.0)
		return 0;
	sink->last_shown = percent;
	printf("\r  %5.1f%%  %.1f of %.1f MB", percent,
	       (double)now / 1048576.0, (double)total / 1048576.0);
	fflush(stdout);
	return 0;
}

static void common_options(CURL *curl, const char *url)
{
	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 10L);
	curl_easy_setopt(curl, CURLOPT_USERAGENT, "vhdb/1.0");
	curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, last_error);
	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 20L);
	curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
}

int vhdb_net_head_bytes(const char *url, uint8_t *out, size_t length)
{
	CURL *curl;
	CURLcode rc;
	memory_sink sink;
	char range[64];

	last_error[0] = 0;
	sink.data = out;
	sink.size = length;
	sink.used = 0;

	curl = curl_easy_init();
	if (!curl)
		return VHDB_NET_ERR;

	common_options(curl, url);
	snprintf(range, sizeof(range), "0-%lu", (unsigned long)(length - 1));
	curl_easy_setopt(curl, CURLOPT_RANGE, range);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_memory);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &sink);

	rc = curl_easy_perform(curl);
	curl_easy_cleanup(curl);

	if (rc != CURLE_OK) {
		if (!last_error[0])
			snprintf(last_error, sizeof(last_error), "%s",
				 curl_easy_strerror(rc));
		return VHDB_NET_ERR;
	}
	if (sink.used != length) {
		snprintf(last_error, sizeof(last_error),
			 "server ignored the range request");
		return VHDB_NET_ERR;
	}
	return VHDB_NET_OK;
}

int vhdb_net_download(const char *url, const char *path, int show_progress)
{
	CURL *curl;
	CURLcode rc;
	file_sink sink;
	char temp[1024];

	last_error[0] = 0;
	snprintf(temp, sizeof(temp), "%s.part", path);

	sink.file = fopen(temp, "wb");
	sink.last_shown = -100.0;
	sink.show = show_progress;
	if (!sink.file) {
		snprintf(last_error, sizeof(last_error), "cannot write %.180s", temp);
		return VHDB_NET_ERR;
	}

	curl = curl_easy_init();
	if (!curl) {
		fclose(sink.file);
		remove(temp);
		return VHDB_NET_ERR;
	}

	common_options(curl, url);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_file);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &sink);
	curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
	curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progress);
	curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &sink);

	rc = curl_easy_perform(curl);
	curl_easy_cleanup(curl);
	fclose(sink.file);

	if (show_progress)
		printf("\n");

	if (rc != CURLE_OK) {
		if (!last_error[0])
			snprintf(last_error, sizeof(last_error), "%s",
				 curl_easy_strerror(rc));
		remove(temp);
		return VHDB_NET_ERR;
	}

	remove(path);
	if (rename(temp, path) != 0) {
		snprintf(last_error, sizeof(last_error), "cannot move %.180s into place",
			 temp);
		remove(temp);
		return VHDB_NET_ERR;
	}
	return VHDB_NET_OK;
}

static size_t read_file(void *chunk, size_t size, size_t count, void *user)
{
	FILE *file = (FILE *)user;
	return fread(chunk, size, count, file);
}

static void credentials(CURL *curl, const char *user, const char *pass)
{
	char login[160];

	curl_easy_setopt(curl, CURLOPT_FTP_USE_EPSV, 0L);

	if (user && user[0]) {
		snprintf(login, sizeof(login), "%s:%s", user, pass ? pass : "");
		curl_easy_setopt(curl, CURLOPT_USERPWD, login);
	} else {
		curl_easy_setopt(curl, CURLOPT_USERPWD, "anonymous:vhdb@local");
	}
}

int vhdb_net_ftp_upload(const char *url, const char *user, const char *pass,
			const char *path, int show_progress)
{
	CURL *curl;
	CURLcode rc;
	FILE *file;
	file_sink sink;
	long size;

	last_error[0] = 0;

	file = fopen(path, "rb");
	if (!file) {
		snprintf(last_error, sizeof(last_error), "cannot read the local file");
		return VHDB_NET_ERR;
	}
	fseek(file, 0, SEEK_END);
	size = ftell(file);
	rewind(file);

	curl = curl_easy_init();
	if (!curl) {
		fclose(file);
		return VHDB_NET_ERR;
	}

	sink.file = NULL;
	sink.last_shown = -100.0;
	sink.show = show_progress;

	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, last_error);
	curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);
	curl_easy_setopt(curl, CURLOPT_READFUNCTION, read_file);
	curl_easy_setopt(curl, CURLOPT_READDATA, file);
	curl_easy_setopt(curl, CURLOPT_INFILESIZE_LARGE, (curl_off_t)size);
	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 20L);
	curl_easy_setopt(curl, CURLOPT_FTP_CREATE_MISSING_DIRS, 1L);
	curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
	curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progress);
	curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &sink);
	credentials(curl, user, pass);

	rc = curl_easy_perform(curl);
	curl_easy_cleanup(curl);
	fclose(file);

	if (show_progress)
		printf("\n");

	if (rc != CURLE_OK) {
		if (!last_error[0])
			snprintf(last_error, sizeof(last_error), "%s",
				 curl_easy_strerror(rc));
		return VHDB_NET_ERR;
	}
	return VHDB_NET_OK;
}

int vhdb_net_ftp_list(const char *url, const char *user, const char *pass,
		      char *out, size_t size)
{
	CURL *curl;
	CURLcode rc;
	memory_sink sink;

	last_error[0] = 0;
	sink.data = (uint8_t *)out;
	sink.size = size - 1;
	sink.used = 0;
	memset(out, 0, size);

	curl = curl_easy_init();
	if (!curl)
		return VHDB_NET_ERR;

	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, last_error);
	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 20L);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_memory);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &sink);
	credentials(curl, user, pass);

	rc = curl_easy_perform(curl);
	curl_easy_cleanup(curl);

	if (rc != CURLE_OK) {
		if (!last_error[0])
			snprintf(last_error, sizeof(last_error), "%s",
				 curl_easy_strerror(rc));
		return VHDB_NET_ERR;
	}
	out[sink.used] = 0;
	return VHDB_NET_OK;
}

int vhdb_net_ftp_get(const char *url, const char *user, const char *pass,
		     uint8_t *out, size_t size, size_t *got)
{
	CURL *curl;
	CURLcode rc;
	memory_sink sink;

	last_error[0] = 0;
	sink.data = out;
	sink.size = size;
	sink.used = 0;

	curl = curl_easy_init();
	if (!curl)
		return VHDB_NET_ERR;

	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, last_error);
	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 20L);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_memory);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &sink);
	credentials(curl, user, pass);

	rc = curl_easy_perform(curl);
	curl_easy_cleanup(curl);

	if (rc != CURLE_OK) {
		if (!last_error[0])
			snprintf(last_error, sizeof(last_error), "%s",
				 curl_easy_strerror(rc));
		return VHDB_NET_ERR;
	}
	if (got)
		*got = sink.used;
	return VHDB_NET_OK;
}

static size_t write_md5(void *chunk, size_t size, size_t count, void *user)
{
	vhdb_md5_ctx *ctx = (vhdb_md5_ctx *)user;
	vhdb_md5_update(ctx, (const uint8_t *)chunk, size * count);
	return size * count;
}

int vhdb_net_ftp_md5(const char *url, const char *user, const char *pass,
		     uint8_t out[16], long *size)
{
	CURL *curl;
	CURLcode rc;
	vhdb_md5_ctx ctx;
	curl_off_t downloaded = 0;

	last_error[0] = 0;
	vhdb_md5_init(&ctx);

	curl = curl_easy_init();
	if (!curl)
		return VHDB_NET_ERR;

	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, last_error);
	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 20L);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_md5);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &ctx);
	credentials(curl, user, pass);

	rc = curl_easy_perform(curl);
	if (rc == CURLE_OK)
		curl_easy_getinfo(curl, CURLINFO_SIZE_DOWNLOAD_T, &downloaded);
	curl_easy_cleanup(curl);

	if (rc != CURLE_OK) {
		if (!last_error[0])
			snprintf(last_error, sizeof(last_error), "%s",
				 curl_easy_strerror(rc));
		return VHDB_NET_ERR;
	}
	if (downloaded == 0) {
		snprintf(last_error, sizeof(last_error), "the file is empty");
		return VHDB_NET_ERR;
	}

	vhdb_md5_final(&ctx, out);
	if (size)
		*size = (long)downloaded;
	return VHDB_NET_OK;
}
