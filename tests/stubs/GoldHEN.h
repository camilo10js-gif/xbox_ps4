#pragma once
#include <stdint.h>
struct proc_info {int pid; char name[40]; char path[64]; char titleid[16]; char contentid[64]; char version[6]; uint64_t base_address;} __attribute__((packed));
extern "C" int sys_sdk_proc_info(struct proc_info*);
