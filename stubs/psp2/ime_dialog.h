#ifndef STUB_IME_DIALOG_H
#define STUB_IME_DIALOG_H
#include <stdint.h>
#include <psp2/common_dialog.h>
#define SCE_IME_TYPE_DEFAULT 1
#define SCE_IME_DIALOG_BUTTON_ENTER 1
#define SCE_IME_DIALOG_MAX_TEXT_LENGTH 512
typedef uint16_t SceWChar16;
typedef struct {
	unsigned int sdkVersion;
	unsigned int inputMethod;
	uint64_t supportedLanguages;
	int languagesForced;
	unsigned int type;
	unsigned int option;
	void *filter;
	unsigned int dialogMode;
	unsigned int textBoxMode;
	const SceWChar16 *title;
	unsigned int maxTextLength;
	SceWChar16 *initialText;
	SceWChar16 *inputTextBuffer;
	unsigned int reserved[16];
} SceImeDialogParam;
typedef struct {
	unsigned int sdkVersion;
	int result;
	int button;
	unsigned int reserved[32];
} SceImeDialogResult;
void sceImeDialogParamInit(SceImeDialogParam *param);
int sceImeDialogInit(const SceImeDialogParam *param);
SceCommonDialogStatus sceImeDialogGetStatus(void);
int sceImeDialogGetResult(SceImeDialogResult *result);
int sceImeDialogTerm(void);
#endif
