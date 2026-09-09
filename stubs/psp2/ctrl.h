#ifndef STUB_CTRL_H
#define STUB_CTRL_H
#include <stdint.h>
#define SCE_CTRL_UP 0x10
#define SCE_CTRL_RIGHT 0x20
#define SCE_CTRL_DOWN 0x40
#define SCE_CTRL_LEFT 0x80
#define SCE_CTRL_LTRIGGER 0x100
#define SCE_CTRL_RTRIGGER 0x200
#define SCE_CTRL_TRIANGLE 0x1000
#define SCE_CTRL_CIRCLE 0x2000
#define SCE_CTRL_CROSS 0x4000
#define SCE_CTRL_SQUARE 0x8000
#define SCE_CTRL_START 0x800
#define SCE_CTRL_SELECT 0x1
typedef struct { unsigned int timeStamp; unsigned int buttons; unsigned char lx, ly, rx, ry; } SceCtrlData;
#define SCE_CTRL_MODE_ANALOG 1
int sceCtrlSetSamplingMode(int mode);
int sceCtrlPeekBufferPositive(int port, SceCtrlData *pad, int count);
#endif
