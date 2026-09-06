#ifndef STUB_PROMOTER_H
#define STUB_PROMOTER_H
int scePromoterUtilityInit(void);
int scePromoterUtilityExit(void);
int scePromoterUtilityPromotePkg(const char *path, int sync);
int scePromoterUtilityGetState(int *state);
int scePromoterUtilityGetResult(int *result);
#endif
