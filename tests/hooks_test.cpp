#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <sys/mman.h>
#include "../xbox_pad.cpp"
static std::string logs;
static int scenario=0, load_calls=0, resolve_calls=0, info_calls=0, thread_calls=0, jailbreak_calls=0, wait_calls=0;
static bool stop_hook_loop=false;
static uint8_t *code;
struct Stopped {};
int sceKernelOpen(const char*,int,int) { return 9; }
int sceKernelWrite(int,const void *p,size_t n) { logs.append((const char*)p,n); return (int)n; }
int sceKernelClose(int) { return 0; }
void sceKernelSleep(unsigned int seconds) { if(stop_hook_loop && seconds==5 && g_pad_discovery_done) throw Stopped(); }
void sceKernelUsleep(unsigned int) { ++wait_calls; g_pad_discovery_done=1; }
int sceKernelLoadStartModule(const char*,size_t,const void*,int,void*,int*) { return -1; }
extern "C" int sys_sdk_jailbreak(void*) { assert(g_pad_discovery_done); ++jailbreak_calls; return 0; }
extern "C" int sceSysmoduleLoadModule(uint32_t) { return 0; }
extern "C" int sceKernelSendNotificationRequest(int,void*,size_t,int) { return 0; }
extern "C" uint64_t sceKernelGetProcessTime() { return 0; }
extern "C" int sys_sdk_proc_info(struct proc_info *p) { strcpy(p->name,"eboot.bin"); strcpy(p->titleid,"CUSA10484"); return 0; }
int scePthreadCreate(OrbisPthread*,void*,void*(*)(void*),void*,const char*) { ++thread_calls; return 0; }
const char *sceKernelGetFsSandboxRandomWord() { return scenario==5 ? nullptr : "test_sandbox"; }
int sceKernelMprotect(const void *p,size_t n,int prot) {
 uintptr_t begin=(uintptr_t)p & ~(uintptr_t)4095;
 size_t size=(((uintptr_t)p+n+4095)&~(uintptr_t)4095)-begin;
 return mprotect((void*)begin,size,prot);
}
int sceKernelMmap(void*,size_t n,int prot,int,int,long,void** p) {
 *p=mmap(nullptr,n,prot,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
 return *p==MAP_FAILED ? -1 : 0;
}
int sceKernelMunmap(void *p,size_t n) { return munmap(p,n); }
static int mock_symbol(int handle,const char *name,void **out) {
 ++resolve_calls; *out=nullptr;
 bool valid=handle==77 && (scenario!=2 || load_calls>0) && (scenario==0 || scenario==1 || scenario==2 || scenario==6 || scenario==7);
 if(!valid) return -1;
 if(!strcmp(name,"scePadReadState")) *out=code;
 if(!strcmp(name,"scePadRead")) *out=code+32;
 if(!strcmp(name,"scePadReadStateExt")) *out=code+256;
 if(!strcmp(name,"scePadReadExt")) *out=code+384;
 return *out ? 0 : -1;
}
extern "C" int sys_dynlib_dlsym(int h,const char *n,void *p) {
 if(scenario==6) { *(void**)p=nullptr; return -1; }
 return mock_symbol(h,n,(void**)p);
}
int sceKernelDlsym(int h,const char *n,void **p) { return mock_symbol(h,n,p); }
extern "C" int sys_dynlib_load_prx(const char *path,int *h) {
 ++load_calls; assert(!jailbreak_calls && !g_pad_discovery_done);
 assert(!strcmp(path,"/test_sandbox/common/lib/libScePad.sprx"));
 if(scenario==2) { *h=77; return 0; }
 *h=-1; return -1;
}
extern "C" int orbis_syscall(int n,...) {
 assert(n==592); va_list ap; va_start(ap,n);
 auto *handles=va_arg(ap,OrbisKernelModule*); size_t count=va_arg(ap,size_t); auto *available=va_arg(ap,size_t*);
 assert(count==256); *available=0;
 if(scenario==0 || scenario==6) { handles[0]=1; handles[1]=77; *available=2; }
 if(scenario==7) { for(size_t i=0;i<count;++i) handles[i]=1; *available=1000; }
 va_end(ap); return scenario==4 ? -1 : 0;
}
int sceKernelGetModuleList(OrbisKernelModule *h,size_t n,size_t *available) {
 assert(n==1024); h[0]=1; h[1]=77; *available=2; return 0;
}
int sceKernelGetModuleInfo(OrbisKernelModule h,OrbisKernelModuleInfo *i) {
 assert(i->size==sizeof(*i)); ++info_calls;
 strcpy(i->name,h==77 ? "libScePad.sprx" : "eboot.bin"); return 0;
}
static void reset() { logs.clear(); g_loglines=0; g_pad_discovery_done=0; load_calls=resolve_calls=info_calls=0; }
static void build_code() {
 memset(code,0x90,4096);
 const uint8_t prologue[]={0x55,0x48,0x89,0xe5,0x48,0x83,0xec,0x10,0x89,0x7d,0xfc,0x90,0x90,0x90,0xB8,0x0D,0,0,0x80,0xc9,0xc3};
 for(unsigned target : {64u,128u,256u,384u}) memcpy(code+target,prologue,sizeof(prologue));
 code[0]=0x31; code[1]=0xd2; code[2]=0xe9; int32_t d=64-7; memcpy(code+3,&d,4);
 code[32]=0x31; code[33]=0xc9; code[34]=0xe9; d=128-39; memcpy(code+35,&d,4);
}
int main() {
 code=(uint8_t*)mmap(nullptr,4096,7,MAP_PRIVATE|MAP_ANONYMOUS,-1,0); assert(code!=MAP_FAILED); build_code();
 reset(); scenario=0; assert(find_game_pad_module()==77 && load_calls==0 && info_calls==2);
 reset(); scenario=1; assert(find_game_pad_module()==77 && load_calls==0 && resolve_calls<400);
 reset(); scenario=2; assert(find_game_pad_module()==77 && load_calls==1);
 reset(); scenario=3; assert(find_game_pad_module()==-1 && load_calls==1);
 reset(); scenario=4; assert(find_game_pad_module()==-1 && load_calls==1);
 reset(); scenario=5; assert(find_game_pad_module()==-1 && load_calls==0);
 reset(); scenario=6; assert(find_game_pad_module()==77 && load_calls==0);
 reset(); scenario=7; assert(find_game_pad_module()==77 && info_calls==256);
 reset(); scenario=0; stop_hook_loop=true;
 try { hook_thread(nullptr); assert(false); } catch(const Stopped&) {}
 assert(g_pad_discovery_done && verify_detour(&g_dReadState) && verify_detour(&g_dRead));
 assert(logs.find("autoprueba=scePadReadState resultado=0x8000000D interceptada=1")!=std::string::npos);
 assert(logs.find("autoprueba=scePadRead resultado=0x8000000D interceptada=1")!=std::string::npos);
 assert(g_readstate_calls==g_gameStateBaseline && g_read_calls==g_gameReadBaseline);
 g_pad.connected=1; g_pad.buttons=PS4_CROSS; publish_state(); OrbisPadData data{};
 assert(((fn_ReadState)code)(10,&data)==(int)0x8000000D && data.buttons==0);
 assert(g_inject_calls==g_gameInjectBaseline);
 ((uint8_t*)&data)[76]=1; data.buttons=PS4_CIRCLE;
 assert(post_ReadState(10,&data,0)==0 && data.buttons==(PS4_CROSS|PS4_CIRCLE));
 auto once=data;post_ReadState(10,&data,0);assert(!memcmp(&data,&once,sizeof(data)));
 data.buttons=0;((uint8_t*)&data)[76]=0;post_ReadState(10,&data,0);assert(data.buttons==0);
 g_pad.connected=0;publish_state();((uint8_t*)&data)[76]=1;post_ReadState(10,&data,0);assert(data.buttons==0);
 assert(g_inject_calls-g_gameInjectBaseline==2);
 uint8_t saved=code[64]; code[64]=0x90; assert(!verify_detour(&g_dReadState)); code[64]=saved; assert(verify_detour(&g_dReadState));
 // Lifecycle exports must not launch a second pair of threads.
 g_started=0; assert(module_start(0,nullptr)==0); assert(plugin_load(0,nullptr)==0); assert(module_start(0,nullptr)==0); assert(thread_calls==2);
 assert(plugin_unload(0,nullptr)==-1 && module_stop(0,nullptr)==-1);
 // USB cannot change the root before pad discovery, including failure fallback.
 g_pad_discovery_done=0; scenario=3; wait_calls=0; stop_hook_loop=false;
 usb_thread(nullptr); assert(wait_calls==1 && jailbreak_calls==1);
 puts("PASS: preloaded/hidden/native/API lookup, error/bounds handling, actual x86 detours + originals + selftests + injection, readback failure, lifecycle guard, USB barrier");
}
