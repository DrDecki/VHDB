#ifndef STUB_STAT_H
#define STUB_STAT_H
#include <stdint.h>
#define SCE_S_IFDIR 0x1000
#define SCE_S_ISDIR(m) (((m) & 0xF000) == SCE_S_IFDIR)
typedef struct { unsigned int st_mode; unsigned int st_attr; uint64_t st_size; } SceIoStat;
int sceIoMkdir(const char *path, int mode);
int sceIoRmdir(const char *path);
int sceIoGetstat(const char *file, SceIoStat *stat);
#endif
