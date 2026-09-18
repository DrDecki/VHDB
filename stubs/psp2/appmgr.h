#ifndef STUB_APPMGR_H
#define STUB_APPMGR_H
typedef struct SceAppMgrExecOptParam SceAppMgrExecOptParam;
int sceAppMgrLoadExec(const char *appPath, char *const argv[],
		      SceAppMgrExecOptParam *option);
#endif
