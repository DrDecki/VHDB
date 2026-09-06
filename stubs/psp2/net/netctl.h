#ifndef STUB_NETCTL_H
#define STUB_NETCTL_H
#define SCE_NETCTL_STATE_CONNECTED 3
int sceNetCtlInit(void);
void sceNetCtlTerm(void);
int sceNetCtlInetGetState(int *state);
#endif
