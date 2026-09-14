#ifndef STUB_THREADMGR_H
#define STUB_THREADMGR_H
int sceKernelDelayThread(unsigned int usec);
typedef unsigned int SceSize;
typedef int SceUID;
typedef int (*SceKernelThreadEntry)(SceSize args, void *argp);
SceUID sceKernelCreateThread(const char *name, SceKernelThreadEntry entry, int initPriority, int stackSize, unsigned int attr, int cpuAffinityMask, const void *option);
int sceKernelStartThread(SceUID thid, SceSize arglen, void *argp);
int sceKernelWaitThreadEnd(SceUID thid, int *stat, unsigned int *timeout);
int sceKernelDeleteThread(SceUID thid);
#endif
