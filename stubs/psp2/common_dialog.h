#ifndef STUB_COMMON_DIALOG_H
#define STUB_COMMON_DIALOG_H
#define SCE_COMMON_DIALOG_STATUS_NONE 0
#define SCE_COMMON_DIALOG_STATUS_RUNNING 1
#define SCE_COMMON_DIALOG_STATUS_FINISHED 2
#define SCE_COMMON_DIALOG_MAGIC_NUMBER 0xC0D1A109
typedef unsigned int SceCommonDialogStatus;
typedef struct { unsigned int sdkVersion; unsigned int language; unsigned int enterButtonAssign; unsigned int reserved[32]; } SceCommonDialogConfigParam;
void sceCommonDialogConfigParamInit(SceCommonDialogConfigParam *param);
int sceCommonDialogSetConfigParam(const SceCommonDialogConfigParam *param);
#endif
