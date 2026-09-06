#include "vhdb_zip.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#ifdef __vita__
#include <psp2/io/stat.h>
#else
#include <sys/stat.h>
#include <sys/types.h>
#endif

#define TAIL_SIZE 66000
#define CHUNK_SIZE 32768

static char last_error[160];

const char *vhdb_zip_error(void)
{
	return last_error[0] ? last_error : "no error";
}

static void make_dir(const char *path)
{
#ifdef __vita__
	sceIoMkdir(path, 0777);
#else
	mkdir(path, 0777);
#endif
}

static void make_parents(const char *path)
{
	char work[512];
	size_t i, length = strlen(path);

	if (length >= sizeof(work))
		return;
	memcpy(work, path, length + 1);

	for (i = 1; i < length; i++) {
		if (work[i] != '/')
			continue;
		work[i] = 0;
		make_dir(work);
		work[i] = '/';
	}
}

static uint16_t read_u16(const uint8_t *p)
{
	return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t read_u32(const uint8_t *p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
	       ((uint32_t)p[3] << 24);
}

static int safe_name(const char *name)
{
	if (name[0] == '/' || name[0] == '\\')
		return 0;
	if (strstr(name, ".."))
		return 0;
	if (strchr(name, ':'))
		return 0;
	return 1;
}

static int extract_one(FILE *archive, const char *destination, const char *name,
		       uint32_t local_offset, uint32_t compressed,
		       uint32_t uncompressed, uint16_t method)
{
	uint8_t header[30];
	uint8_t *in;
	uint8_t *out;
	char path[512];
	FILE *file;
	z_stream stream;
	uint32_t left = compressed;
	int failed = 0;

	if (fseek(archive, (long)local_offset, SEEK_SET) != 0)
		return 0;
	if (fread(header, 1, sizeof(header), archive) != sizeof(header))
		return 0;
	if (read_u32(header) != 0x04034B50u) {
		snprintf(last_error, sizeof(last_error), "broken local header");
		return 0;
	}

	if (fseek(archive, (long)(read_u16(header + 26) + read_u16(header + 28)),
		  SEEK_CUR) != 0)
		return 0;

	snprintf(path, sizeof(path), "%s/%s", destination, name);
	make_parents(path);

	file = fopen(path, "wb");
	if (!file) {
		snprintf(last_error, sizeof(last_error), "cannot write %.100s", name);
		return 0;
	}

	if (method == 0) {
		in = (uint8_t *)malloc(CHUNK_SIZE);
		if (!in) {
			fclose(file);
			return 0;
		}
		while (left > 0) {
			uint32_t want = left < CHUNK_SIZE ? left : CHUNK_SIZE;
			size_t got = fread(in, 1, want, archive);

			if (got == 0 || fwrite(in, 1, got, file) != got) {
				failed = 1;
				break;
			}
			left -= (uint32_t)got;
		}
		free(in);
		fclose(file);
		if (failed)
			snprintf(last_error, sizeof(last_error), "the write failed");
		return !failed;
	}

	if (method != 8) {
		fclose(file);
		snprintf(last_error, sizeof(last_error), "unsupported compression");
		return 0;
	}

	in = (uint8_t *)malloc(CHUNK_SIZE);
	out = (uint8_t *)malloc(CHUNK_SIZE);
	if (!in || !out) {
		free(in);
		free(out);
		fclose(file);
		snprintf(last_error, sizeof(last_error), "not enough memory");
		return 0;
	}

	memset(&stream, 0, sizeof(stream));
	if (inflateInit2(&stream, -15) != Z_OK) {
		free(in);
		free(out);
		fclose(file);
		snprintf(last_error, sizeof(last_error), "cannot start inflate");
		return 0;
	}

	while (left > 0 && !failed) {
		uint32_t want = left < CHUNK_SIZE ? left : CHUNK_SIZE;
		size_t got = fread(in, 1, want, archive);
		int status;

		if (got == 0) {
			failed = 1;
			break;
		}
		left -= (uint32_t)got;

		stream.next_in = in;
		stream.avail_in = (unsigned int)got;

		do {
			stream.next_out = out;
			stream.avail_out = CHUNK_SIZE;
			status = inflate(&stream, Z_NO_FLUSH);

			if (status != Z_OK && status != Z_STREAM_END &&
			    status != Z_BUF_ERROR) {
				snprintf(last_error, sizeof(last_error),
					 "damaged archive entry");
				failed = 1;
				break;
			}

			{
				size_t produced = CHUNK_SIZE - stream.avail_out;

				if (produced &&
				    fwrite(out, 1, produced, file) != produced) {
					snprintf(last_error, sizeof(last_error),
						 "the memory card is full");
					failed = 1;
					break;
				}
			}
		} while (stream.avail_out == 0 && status != Z_STREAM_END);
	}

	if (!failed && stream.total_out != uncompressed && uncompressed != 0) {
		snprintf(last_error, sizeof(last_error), "size mismatch in %.80s",
			 name);
		failed = 1;
	}

	inflateEnd(&stream);
	free(in);
	free(out);
	fclose(file);
	return !failed;
}

int vhdb_zip_top_level(const char *archive_path, char names[][64], int limit)
{
	FILE *archive;
	uint8_t *tail;
	uint8_t *directory;
	long size, tail_length, marker;
	uint32_t count, directory_size, directory_offset, position = 0;
	int found = 0;
	uint32_t i;

	last_error[0] = 0;

	archive = fopen(archive_path, "rb");
	if (!archive)
		return -1;

	fseek(archive, 0, SEEK_END);
	size = ftell(archive);
	if (size < 22) {
		fclose(archive);
		return -1;
	}

	tail_length = size < TAIL_SIZE ? size : TAIL_SIZE;
	tail = (uint8_t *)malloc((size_t)tail_length);
	if (!tail) {
		fclose(archive);
		return -1;
	}

	fseek(archive, size - tail_length, SEEK_SET);
	if (fread(tail, 1, (size_t)tail_length, archive) != (size_t)tail_length) {
		free(tail);
		fclose(archive);
		return -1;
	}

	for (marker = tail_length - 22; marker >= 0; marker--) {
		if (read_u32(tail + marker) == 0x06054B50u)
			break;
	}
	if (marker < 0) {
		free(tail);
		fclose(archive);
		return -1;
	}

	count = read_u16(tail + marker + 10);
	directory_size = read_u32(tail + marker + 12);
	directory_offset = read_u32(tail + marker + 16);
	free(tail);

	directory = (uint8_t *)malloc(directory_size);
	if (!directory) {
		fclose(archive);
		return -1;
	}

	fseek(archive, (long)directory_offset, SEEK_SET);
	if (fread(directory, 1, directory_size, archive) != directory_size) {
		free(directory);
		fclose(archive);
		return -1;
	}
	fclose(archive);

	for (i = 0; i < count; i++) {
		char name[256];
		char top[64];
		uint16_t name_length, extra_length, comment_length;
		size_t length = 0;
		int known = 0;
		int n;

		if (position + 46 > directory_size ||
		    read_u32(directory + position) != 0x02014B50u)
			break;

		name_length = read_u16(directory + position + 28);
		extra_length = read_u16(directory + position + 30);
		comment_length = read_u16(directory + position + 32);

		if (name_length >= sizeof(name))
			break;
		memcpy(name, directory + position + 46, name_length);
		name[name_length] = 0;
		position += 46u + name_length + extra_length + comment_length;

		while (name[length] && name[length] != '/' && name[length] != '\\' &&
		       length + 1 < sizeof(top))
			length++;
		memcpy(top, name, length);
		top[length] = 0;
		if (!top[0])
			continue;

		for (n = 0; n < found; n++) {
			if (strcmp(names[n], top) == 0) {
				known = 1;
				break;
			}
		}
		if (known)
			continue;

		if (found < limit)
			snprintf(names[found], 64, "%s", top);
		found++;
	}

	free(directory);
	return found;
}

int vhdb_zip_extract(const char *archive_path, const char *destination,
		     vhdb_zip_progress progress, void *user)
{
	FILE *archive;
	uint8_t *tail;
	uint8_t *directory = NULL;
	long size;
	long tail_length;
	long marker = -1;
	uint32_t count, directory_size, directory_offset;
	uint32_t position = 0;
	uint32_t done = 0;
	int ok = 1;
	uint32_t i;

	last_error[0] = 0;

	archive = fopen(archive_path, "rb");
	if (!archive) {
		snprintf(last_error, sizeof(last_error), "cannot open the archive");
		return 0;
	}

	fseek(archive, 0, SEEK_END);
	size = ftell(archive);
	if (size < 22) {
		snprintf(last_error, sizeof(last_error), "the archive is truncated");
		fclose(archive);
		return 0;
	}

	tail_length = size < TAIL_SIZE ? size : TAIL_SIZE;
	tail = (uint8_t *)malloc((size_t)tail_length);
	if (!tail) {
		fclose(archive);
		snprintf(last_error, sizeof(last_error), "not enough memory");
		return 0;
	}

	fseek(archive, size - tail_length, SEEK_SET);
	if (fread(tail, 1, (size_t)tail_length, archive) != (size_t)tail_length) {
		free(tail);
		fclose(archive);
		snprintf(last_error, sizeof(last_error), "cannot read the archive");
		return 0;
	}

	for (marker = tail_length - 22; marker >= 0; marker--) {
		if (read_u32(tail + marker) == 0x06054B50u)
			break;
	}
	if (marker < 0) {
		free(tail);
		fclose(archive);
		snprintf(last_error, sizeof(last_error), "this is not a zip archive");
		return 0;
	}

	count = read_u16(tail + marker + 10);
	directory_size = read_u32(tail + marker + 12);
	directory_offset = read_u32(tail + marker + 16);
	free(tail);

	if (directory_offset == 0xFFFFFFFFu) {
		fclose(archive);
		snprintf(last_error, sizeof(last_error), "zip64 is not supported");
		return 0;
	}

	directory = (uint8_t *)malloc(directory_size);
	if (!directory) {
		fclose(archive);
		snprintf(last_error, sizeof(last_error), "not enough memory");
		return 0;
	}

	fseek(archive, (long)directory_offset, SEEK_SET);
	if (fread(directory, 1, directory_size, archive) != directory_size) {
		free(directory);
		fclose(archive);
		snprintf(last_error, sizeof(last_error), "cannot read the index");
		return 0;
	}

	make_dir(destination);

	for (i = 0; i < count && ok; i++) {
		char name[256];
		uint16_t method, name_length, extra_length, comment_length;
		uint32_t compressed, uncompressed, local_offset;

		if (position + 46 > directory_size ||
		    read_u32(directory + position) != 0x02014B50u) {
			snprintf(last_error, sizeof(last_error), "broken index");
			ok = 0;
			break;
		}

		method = read_u16(directory + position + 10);
		compressed = read_u32(directory + position + 20);
		uncompressed = read_u32(directory + position + 24);
		name_length = read_u16(directory + position + 28);
		extra_length = read_u16(directory + position + 30);
		comment_length = read_u16(directory + position + 32);
		local_offset = read_u32(directory + position + 42);

		if (name_length >= sizeof(name)) {
			snprintf(last_error, sizeof(last_error), "a name is too long");
			ok = 0;
			break;
		}
		memcpy(name, directory + position + 46, name_length);
		name[name_length] = 0;
		position += 46u + name_length + extra_length + comment_length;

		{
			char *cursor = name;
			while (*cursor) {
				if (*cursor == '\\')
					*cursor = '/';
				cursor++;
			}
		}

		if (!safe_name(name)) {
			snprintf(last_error, sizeof(last_error),
				 "the archive tries to escape its folder");
			ok = 0;
			break;
		}

		if (name[0] && name[strlen(name) - 1] == '/') {
			char path[512];
			snprintf(path, sizeof(path), "%s/%s", destination, name);
			make_parents(path);
			make_dir(path);
			continue;
		}

		if (!extract_one(archive, destination, name, local_offset, compressed,
				 uncompressed, method)) {
			ok = 0;
			break;
		}

		done++;
		if (progress && !progress(done, count, name, user)) {
			snprintf(last_error, sizeof(last_error), "cancelled");
			ok = 0;
			break;
		}
	}

	free(directory);
	fclose(archive);
	return ok;
}
