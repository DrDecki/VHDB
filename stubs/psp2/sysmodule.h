#ifndef STUB_SYSMODULE_H
#define STUB_SYSMODULE_H
#define SCE_SYSMODULE_NET 5
#define SCE_SYSMODULE_HTTPS 16
#define SCE_SYSMODULE_INTERNAL_PAF 0x80000000
#define SCE_SYSMODULE_INTERNAL_PROMOTER_UTIL 0x80000001
typedef struct { int flags; int result; int unused[2]; } SceSysmoduleOpt;
int sceSysmoduleLoadModule(int id);
int sceSysmoduleUnloadModule(int id);
int sceSysmoduleLoadModuleInternal(unsigned int id);
int sceSysmoduleUnloadModuleInternal(unsigned int id);
int sceSysmoduleLoadModuleInternalWithArg(unsigned int id, unsigned int argSize, void *args, SceSysmoduleOpt *option);
int sceSysmoduleUnloadModuleInternalWithArg(unsigned int id, unsigned int argSize, void *args, SceSysmoduleOpt *option);
#endif
