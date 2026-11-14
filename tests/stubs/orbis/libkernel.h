#pragma once
#ifdef __cplusplus
extern "C" {
#endif
#include <stddef.h>
#include <stdint.h>
typedef void* OrbisPthread;
int sceKernelOpen(const char*, int, int);
int sceKernelWrite(int, const void*, size_t);
int sceKernelClose(int);
int sceKernelLoadStartModule(const char*, size_t, const void*, int, void*, int*);
int sceKernelDlsym(int, const char*, void**);
void sceKernelSleep(unsigned int);
void sceKernelUsleep(unsigned int);
int scePthreadCreate(OrbisPthread*, void*, void*(*)(void*), void*, const char*);
typedef uint32_t OrbisKernelModule;
typedef struct { void *address; uint32_t size; int32_t prot; } OrbisKernelModuleSegmentInfo;
typedef struct { size_t size; char name[256]; OrbisKernelModuleSegmentInfo segmentInfo[4]; uint32_t segmentCount; uint8_t fingerprint[20]; } OrbisKernelModuleInfo;
int sceKernelGetModuleList(OrbisKernelModule*, size_t, size_t*);
int sceKernelGetModuleInfo(OrbisKernelModule, OrbisKernelModuleInfo*);

const char *sceKernelGetFsSandboxRandomWord(void);
int sceKernelMprotect(const void*, size_t, int);
int sceKernelMmap(void*, size_t, int, int, int, long, void**);
int sceKernelMunmap(void*, size_t);
#ifdef __cplusplus
}
#endif
