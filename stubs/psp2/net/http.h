#ifndef STUB_HTTP_H
#define STUB_HTTP_H
#include <stdint.h>
#define SCE_TRUE 1
#define SCE_FALSE 0
#define SCE_HTTP_VERSION_1_1 2
#define SCE_HTTP_METHOD_GET 0
#define SCE_HTTP_HEADER_OVERWRITE 0
#define SCE_HTTPS_FLAG_SERVER_VERIFY 0x01
#define SCE_HTTPS_FLAG_CLIENT_VERIFY 0x02
#define SCE_HTTPS_FLAG_CN_CHECK 0x04
#define SCE_HTTPS_FLAG_NOT_AFTER_CHECK 0x08
#define SCE_HTTPS_FLAG_NOT_BEFORE_CHECK 0x10
#define SCE_HTTPS_FLAG_KNOWN_CA_CHECK 0x20
int sceHttpInit(unsigned int poolSize);
int sceHttpTerm(void);
int sceSslInit(unsigned int poolSize);
int sceSslTerm(void);
int sceHttpCreateTemplate(const char *userAgent, int httpVer, int autoProxyConf);
int sceHttpDeleteTemplate(int templateId);
int sceHttpCreateConnectionWithURL(int templateId, const char *url, int keepAlive);
int sceHttpDeleteConnection(int connId);
int sceHttpCreateRequestWithURL(int connId, int method, const char *url, uint64_t contentLength);
int sceHttpDeleteRequest(int reqId);
int sceHttpSendRequest(int reqId, const void *postData, unsigned int size);
int sceHttpReadData(int reqId, void *data, unsigned int size);
int sceHttpAddRequestHeader(int reqId, const char *name, const char *value, unsigned int mode);
int sceHttpGetStatusCode(int reqId, int *statusCode);
int sceHttpGetResponseContentLength(int reqId, uint64_t *contentLength);
int sceHttpSetAutoRedirect(int id, int enable);
int sceHttpSetRecvTimeOut(int id, unsigned int usec);
int sceHttpSetConnectTimeOut(int id, unsigned int usec);
int sceHttpSetResolveTimeOut(int id, unsigned int usec);
int sceHttpsDisableOption(unsigned int flags);
#endif
