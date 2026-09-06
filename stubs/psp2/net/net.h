#ifndef STUB_NET_H
#define STUB_NET_H
#define SCE_NET_ERROR_ENOTINIT 0x80410100
typedef struct { void *memory; int size; int flags; } SceNetInitParam;
int sceNetInit(SceNetInitParam *param);
int sceNetTerm(void);
int sceNetShowNetstat(void);
#endif
