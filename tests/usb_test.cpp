#include <vector>
#include <cassert>
#include <cstdio>
#include <cstring>
#include "../xbox_pad.cpp"
static uint64_t fake_time = 0;
static int reads = 0, releases = 0, closes = 0, waits = 0;
static std::vector<std::vector<uint8_t>> writes;
struct Stopped {};
int sceKernelOpen(const char*, int, int) { return -1; }
int sceKernelWrite(int, const void*, size_t) { return 0; }
int sceKernelClose(int) { return 0; }
void sceKernelSleep(unsigned int) {}
void sceKernelUsleep(unsigned int us) { if (us == 10000) waits++; else if (us == 1000000 && closes) throw Stopped(); }
int sceKernelLoadStartModule(const char*, size_t, const void*, int, void*, int*) { return 1; }
extern "C" int sys_sdk_jailbreak(void*) { return 0; }
extern "C" int sceSysmoduleLoadModule(uint32_t) { return 0; }
extern "C" int sceKernelSendNotificationRequest(int, void*, size_t, int) { return 0; }
extern "C" uint64_t sceKernelGetProcessTime() { return fake_time; }
static int init(void**) { return 0; }
static int list(void*** p) { static void *devices[]={(void*)1}; *p=devices; return 1; }
static void freelist(void**, int) {}
static int desc(void*, uint8_t *d) { d[8]=0x5e; d[9]=0x04; d[10]=0x12; d[11]=0x0b; return 0; }
static int open_dev(void*, void** h) { *h=(void*)2; return 0; }
static void close_dev(void*) { closes++; }
static int claim(void*, int) { return 0; }
static int release(void*, int) { releases++; return 0; }
static int ctrl(void*, uint8_t, uint8_t, uint16_t, uint16_t, uint8_t *d, uint16_t, unsigned) {
 const uint8_t config[]={9,2,32,0,1,1,0,0x80,50,9,4,0,0,2,0xff,0x47,0xd0,0,7,5,0x82,3,64,0,1,7,5,0x02,3,64,0,1};
 memcpy(d,config,sizeof(config)); return sizeof(config);
}
static int transfer(void*, uint8_t ep, uint8_t* p, int n, int *done, unsigned) {
 if (ep == 2) { writes.emplace_back(p,p+n); *done=n; return 0; }
 assert(ep==0x82); *done=0; if (reads++ < 3) fake_time += 97000; return (int)0x80240007u;
}
int sceKernelDlsym(int, const char *name, void **p) {
 #define RESOLVE(n, f) if (!strcmp(name,n)) { *p=(void*)f; return 0; }
 RESOLVE("sceUsbdInit",init) RESOLVE("sceUsbdGetDeviceList",list) RESOLVE("sceUsbdFreeDeviceList",freelist)
 RESOLVE("sceUsbdGetDeviceDescriptor",desc) RESOLVE("sceUsbdOpen",open_dev) RESOLVE("sceUsbdClose",close_dev)
 RESOLVE("sceUsbdClaimInterface",claim) RESOLVE("sceUsbdReleaseInterface",release)
 RESOLVE("sceUsbdControlTransfer",ctrl) RESOLVE("sceUsbdInterruptTransfer",transfer)
 *p=nullptr; return -1;
}
int main() {
 g_pad_discovery_done=1; try { usb_thread(nullptr); assert(false); } catch (const Stopped&) {}
 const std::vector<std::vector<uint8_t>> expected={{0x05,0x20,0,1,0},{0x0a,0x20,1,3,0,1,0x14},{0x06,0x20,2,2,1,0}};
 assert(writes==expected); assert(reads==23); assert(waits==19); assert(releases==1 && closes==1); assert(g_pad.connected==0);
 assert(g_ready==0&&g_pad.lx==128&&g_pad.l2==0);
 uint8_t packet[48]={0x20,0,3,44,0x10,0};packet[11]=packet[13]=packet[15]=packet[17]=0;
 parse_gip(packet,17);assert(!g_pad.connected);parse_gip(packet,48);publish_state();assert(g_ready&&g_mapped.buttons==PS4_CROSS);
 packet[4]=0;packet[3]=60;parse_gip(packet,48);publish_state();assert(g_mapped.buttons==PS4_CROSS);
 puts("USB simulation passed: initialization sequence, 3 ordinary and 20 immediate timeouts, release and close");
}
