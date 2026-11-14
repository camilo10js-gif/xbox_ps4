#pragma once
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <orbis/libkernel.h>
#include <Detour.h>
#define VM_PROT_ALL 7
#ifdef __cplusplus
extern "C" {
#endif
int orbis_syscall(int, ...);
int sys_dynlib_dlsym(int, const char*, void*);
int sys_dynlib_load_prx(const char*, int*);
#ifdef __cplusplus
}
#endif
