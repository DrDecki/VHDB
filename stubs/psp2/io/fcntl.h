#ifndef STUB_FCNTL_H
#define STUB_FCNTL_H
#include <stdint.h>
#define SCE_O_RDONLY 0x0001
#define SCE_O_WRONLY 0x0002
#define SCE_O_CREAT 0x0200
#define SCE_O_TRUNC 0x0400
#define SCE_SEEK_SET 0
#define SCE_SEEK_CUR 1
#define SCE_SEEK_END 2
typedef int SceUID;
typedef int64_t SceOff;
SceUID sceIoOpen(const char *file, int flags, int mode);
int sceIoClose(SceUID fd);
int sceIoRead(SceUID fd, void *data, unsigned int size);
int sceIoWrite(SceUID fd, const void *data, unsigned int size);
int sceIoRemove(const char *file);
int sceIoRename(const char *oldname, const char *newname);
SceOff sceIoLseek(SceUID fd, SceOff offset, int whence);
#define SCE_SEEK_SET 0
#define SCE_SEEK_END 2
int sceIoLseek32(SceUID fd, int offset, int whence);
#endif
