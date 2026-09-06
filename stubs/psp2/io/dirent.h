#ifndef STUB_DIRENT_H
#define STUB_DIRENT_H
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
typedef struct { SceIoStat d_stat; char d_name[256]; void *d_private; int dummy; } SceIoDirent;
SceUID sceIoDopen(const char *dirname);
int sceIoDread(SceUID fd, SceIoDirent *dir);
int sceIoDclose(SceUID fd);
#endif
