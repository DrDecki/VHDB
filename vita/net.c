#include "vhdb_vitanet.h"

#include <psp2/net/net.h>
#include <psp2/net/netctl.h>
#include <psp2/sysmodule.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>

#include <curl/curl.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NET_POOL_SIZE (512 * 1024)

#define USER_AGENT "VHDB/1.0"

static int net_ready;
static void *net_pool;
static char last_error[CURL_ERROR_SIZE];

typedef struct {
	uint8_t *data;
	unsigned int size;
	unsigned int used;
} memory_sink;

typedef struct {
	SceUID file;
	vhdb_progress_fn progress;
	void *user;
	int cancelled;
	int write_failed;
} file_sink;

const char *vhdb_net_error(void)
{
	return last_error[0] ? last_error : "no error";
}

int vhdb_net_online(void)
{
	int state = 0;

	if (sceNetCtlInetGetState(&state) < 0)
		return 0;
	return state == SCE_NETCTL_STATE_CONNECTED;
}

int vhdb_net_start(void)
{
	if (net_ready)
		return 1;

	last_error[0] = 0;

	sceSysmoduleLoadModule(SCE_SYSMODULE_NET);

	if (sceNetShowNetstat() == (int)SCE_NET_ERROR_ENOTINIT) {
		SceNetInitParam param;
		int rc;

		net_pool = malloc(NET_POOL_SIZE);
		if (!net_pool) {
			snprintf(last_error, sizeof(last_error),
				 "not enough memory for the network pool");
			return 0;
		}
		param.memory = net_pool;
		param.size = NET_POOL_SIZE;
		param.flags = 0;

		rc = sceNetInit(&param);
		if (rc < 0) {
			snprintf(last_error, sizeof(last_error),
				 "sceNetInit failed (0x%08X)", (unsigned int)rc);
			free(net_pool);
			net_pool = NULL;
			return 0;
		}
	}

	sceNetCtlInit();

	if (curl_global_init(CURL_GLOBAL_ALL) != CURLE_OK) {
		snprintf(last_error, sizeof(last_error), "cannot start curl");
		return 0;
	}

	net_ready = 1;
	return 1;
}

void vhdb_net_stop(void)
{
	if (!net_ready)
		return;

	curl_global_cleanup();
	sceNetCtlTerm();
	net_ready = 0;
}

static void common_options(CURL *curl, const char *url)
{
	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_USERAGENT, USER_AGENT);
	curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, last_error);
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 10L);
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
	curl_easy_setopt(curl, CURLOPT_SSLVERSION, CURL_SSLVERSION_TLSv1_2);
	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 20L);
	curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
	curl_easy_setopt(curl, CURLOPT_BUFFERSIZE, 32768L);
	curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
}

static size_t write_memory(void *chunk, size_t size, size_t count, void *user)
{
	memory_sink *sink = (memory_sink *)user;
	size_t total = size * count;
	size_t room = sink->size - sink->used;

	if (total > room)
		total = room;
	if (total > 0) {
		memcpy(sink->data + sink->used, chunk, total);
		sink->used += (unsigned int)total;
	}
	return size * count;
}

static size_t write_to_file(void *chunk, size_t size, size_t count, void *user)
{
	file_sink *sink = (file_sink *)user;
	int total = (int)(size * count);

	if (sceIoWrite(sink->file, chunk, total) != total) {
		sink->write_failed = 1;
		return 0;
	}
	return size * count;
}

static int report_progress(void *user, curl_off_t total, curl_off_t done,
			   curl_off_t up_total, curl_off_t up_done)
{
	file_sink *sink = (file_sink *)user;

	(void)up_total;
	(void)up_done;

	if (!sink->progress)
		return 0;
	if (!sink->progress((uint64_t)done, (uint64_t)total, sink->user)) {
		sink->cancelled = 1;
		return 1;
	}
	return 0;
}

int vhdb_net_head(const char *url, uint8_t *out, unsigned int length)
{
	CURL *curl;
	CURLcode rc;
	memory_sink sink;
	char range[64];

	if (!net_ready && !vhdb_net_start())
		return 0;

	last_error[0] = 0;
	sink.data = out;
	sink.size = length;
	sink.used = 0;

	curl = curl_easy_init();
	if (!curl)
		return 0;

	common_options(curl, url);
	snprintf(range, sizeof(range), "0-%u", length - 1);
	curl_easy_setopt(curl, CURLOPT_RANGE, range);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_memory);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &sink);

	rc = curl_easy_perform(curl);
	curl_easy_cleanup(curl);

	if (rc != CURLE_OK) {
		if (!last_error[0])
			snprintf(last_error, sizeof(last_error), "%s",
				 curl_easy_strerror(rc));
		return 0;
	}
	if (sink.used != length) {
		snprintf(last_error, sizeof(last_error),
			 "the server ignored the range request");
		return 0;
	}
	return 1;
}

int vhdb_net_fetch(const char *url, const char *path, vhdb_progress_fn progress,
		   void *user)
{
	CURL *curl;
	CURLcode rc;
	file_sink sink;
	char temp[256];

	if (!net_ready && !vhdb_net_start())
		return 0;

	last_error[0] = 0;

	snprintf(temp, sizeof(temp), "%s.part", path);
	sceIoRemove(temp);

	sink.file = sceIoOpen(temp, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
	sink.progress = progress;
	sink.user = user;
	sink.cancelled = 0;
	sink.write_failed = 0;

	if (sink.file < 0) {
		snprintf(last_error, sizeof(last_error),
			 "cannot write to the memory card");
		return 0;
	}

	curl = curl_easy_init();
	if (!curl) {
		sceIoClose(sink.file);
		sceIoRemove(temp);
		return 0;
	}

	common_options(curl, url);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_to_file);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &sink);
	curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
	curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, report_progress);
	curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &sink);

	rc = curl_easy_perform(curl);
	curl_easy_cleanup(curl);
	sceIoClose(sink.file);

	if (rc != CURLE_OK) {
		if (sink.cancelled)
			snprintf(last_error, sizeof(last_error), "cancelled");
		else if (sink.write_failed)
			snprintf(last_error, sizeof(last_error),
				 "the memory card is full");
		else if (!last_error[0])
			snprintf(last_error, sizeof(last_error), "%s",
				 curl_easy_strerror(rc));
		sceIoRemove(temp);
		return 0;
	}

	sceIoRemove(path);
	if (sceIoRename(temp, path) < 0) {
		snprintf(last_error, sizeof(last_error),
			 "cannot put the file into place");
		sceIoRemove(temp);
		return 0;
	}
	return 1;
}
