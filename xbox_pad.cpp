// xbox_pad.cpp - Plugin GoldHEN: mando Xbox One/Series (USB, protocolo GIP) -> PS4
// NOTA: sin probar en consola real.
// [AJUSTAR] marca lo que puede cambiar segun la version del SDK.

#include <stdint.h>
#include <string.h>
#include <orbis/libkernel.h>
#include <Common.h>
#include <GoldHEN.h>
#include <Detour.h>

#ifndef attr_public
#define attr_public __attribute__((visibility("default")))
#endif
#ifndef attr_module_hidden
#define attr_module_hidden __attribute__((visibility("hidden")))
#endif

attr_public const char *g_pluginName = "xbox_pad";
attr_public const char *g_pluginDesc = "Xbox One/Series USB controller -> PS4 pad";
attr_public const char *g_pluginAuth = "community";
attr_public uint32_t g_pluginVersion = 0x00000100;

#define XBOX_VID 0x045E
#define EP_IN    0x82
#define EP_OUT   0x02

static const uint16_t kPids[] = {
    0x0B12, 0x0B00, 0x02EA, 0x02DD, 0x02D1, 0x02E3
};
#define NUM_PIDS (sizeof(kPids) / sizeof(kPids[0]))

#define PS4_L3       0x00000002
#define PS4_R3       0x00000004
#define PS4_OPTIONS  0x00000008
#define PS4_UP       0x00000010
#define PS4_RIGHT    0x00000020
#define PS4_DOWN     0x00000040
#define PS4_LEFT     0x00000080
#define PS4_L2       0x00000100
#define PS4_R2       0x00000200
#define PS4_L1       0x00000400
#define PS4_R1       0x00000800
#define PS4_TRIANGLE 0x00001000
#define PS4_CIRCLE   0x00002000
#define PS4_CROSS    0x00004000
#define PS4_SQUARE   0x00008000
#define PS4_TOUCHPAD 0x00100000

struct PadState {
    volatile uint32_t buttons;
    volatile uint8_t lx, ly, rx, ry, l2, r2;
    volatile int connected;
};

static PadState g_pad = {0, 0x80, 0x80, 0x80, 0x80, 0, 0, 0};

typedef int   (*fn_init)(void **ctx);
typedef void *(*fn_open_vidpid)(void *ctx, uint16_t vid, uint16_t pid);
typedef int   (*fn_claim)(void *h, int iface);
typedef int   (*fn_detach)(void *h, int iface);
typedef int   (*fn_xfer)(void *h, uint8_t ep, uint8_t *data, int len, int *done, unsigned timeout);
typedef void  (*fn_close)(void *h);

static fn_init p_init;
static fn_open_vidpid p_open;
static fn_claim p_claim;
static fn_detach p_detach;
static fn_xfer p_int;
static fn_close p_close;

static bool load_usbd() {
    int h = sceKernelLoadStartModule("/system/common/lib/libSceUsbd.sprx", 0, 0, 0, 0, 0);
    if (h < 0) return false;
    sceKernelDlsym(h, "sceUsbdInit", (void **)&p_init);
    sceKernelDlsym(h, "sceUsbdOpenDeviceWithVidPid", (void **)&p_open);
    sceKernelDlsym(h, "sceUsbdClaimInterface", (void **)&p_claim);
    sceKernelDlsym(h, "sceUsbdDetachKernelDriver", (void **)&p_detach);
    sceKernelDlsym(h, "sceUsbdInterruptTransfer", (void **)&p_int);
    sceKernelDlsym(h, "sceUsbdCloseDevice", (void **)&p_close);
    return p_init && p_open && p_claim && p_int;
}

static uint8_t axis16(int16_t v)    { return (uint8_t)(((int)v + 32768) >> 8); }
static uint8_t invaxis16(int16_t v) { return (uint8_t)(255 - (((int)v + 32768) >> 8)); }

static void parse_gip(const uint8_t *p, int n) {
    if (n < 18 || p[0] != 0x20) return;

    uint8_t b0 = p[4], b1 = p[5];
    uint32_t o = 0;

    if (b0 & 0x04) o |= PS4_OPTIONS;
    if (b0 & 0x08) o |= PS4_TOUCHPAD;
    if (b0 & 0x10) o |= PS4_CROSS;
    if (b0 & 0x20) o |= PS4_CIRCLE;
    if (b0 & 0x40) o |= PS4_SQUARE;
    if (b0 & 0x80) o |= PS4_TRIANGLE;

    if (b1 & 0x01) o |= PS4_UP;
    if (b1 & 0x02) o |= PS4_DOWN;
    if (b1 & 0x04) o |= PS4_LEFT;
    if (b1 & 0x08) o |= PS4_RIGHT;
    if (b1 & 0x10) o |= PS4_L1;
    if (b1 & 0x20) o |= PS4_R1;
    if (b1 & 0x40) o |= PS4_L3;
    if (b1 & 0x80) o |= PS4_R3;

    uint16_t lt = (uint16_t)(p[6] | (p[7] << 8));
    uint16_t rt = (uint16_t)(p[8] | (p[9] << 8));
    uint8_t l2 = (uint8_t)(lt >> 2), r2 = (uint8_t)(rt >> 2);
    if (l2 > 30) o |= PS4_L2;
    if (r2 > 30) o |= PS4_R2;

    int16_t lx = (int16_t)(p[10] | (p[11] << 8));
    int16_t ly = (int16_t)(p[12] | (p[13] << 8));
    int16_t rx = (int16_t)(p[14] | (p[15] << 8));
    int16_t ry = (int16_t)(p[16] | (p[17] << 8));

    g_pad.lx = axis16(lx);
    g_pad.ly = invaxis16(ly);
    g_pad.rx = axis16(rx);
    g_pad.ry = invaxis16(ry);
    g_pad.l2 = l2;
    g_pad.r2 = r2;
    g_pad.buttons = o;
}

static void *usb_thread(void *) {
    if (!load_usbd()) return 0;
    void *ctx = 0;
    p_init(&ctx);

    for (;;) {
        void *dev = 0;
        for (unsigned i = 0; i < NUM_PIDS && !dev; i++)
            dev = p_open(ctx, XBOX_VID, kPids[i]);

        if (!dev) { sceKernelUsleep(1000 * 1000); continue; }

        if (p_detach) p_detach(dev, 0);
        if (p_claim(dev, 0) != 0) {
            if (p_close) p_close(dev);
            sceKernelUsleep(1000 * 1000);
            continue;
        }

        uint8_t init[5] = {0x05, 0x20, 0x00, 0x01, 0x00};
        int done = 0;
        p_int(dev, EP_OUT, init, 5, &done, 1000);
        g_pad.connected = 1;

        for (;;) {
            uint8_t buf[64];
            done = 0;
            int r = p_int(dev, EP_IN, buf, sizeof(buf), &done, 200);
            if (r == 0 && done > 0) parse_gip(buf, done);
            else if (r != 0 && r != -7) break;
        }

        g_pad.connected = 0;
        g_pad.buttons = 0;
        if (p_close) p_close(dev);
    }
    return 0;
}

typedef int (*fn_ReadState)(int32_t, void *);
typedef int (*fn_Read)(int32_t, void *, int32_t);

static Detour g_dReadState, g_dRead;
static fn_ReadState orig_ReadState;
static fn_Read orig_Read;

static void inject(uint8_t *d, bool fresh) {
    if (fresh) {
        memset(d, 0, 0x58);
        d[4] = d[5] = d[6] = d[7] = 0x80;
        *(float *)(d + 24) = 1.0f;
    }
    *(uint32_t *)(d + 0) |= g_pad.buttons;
    if (fresh || g_pad.lx < 0x70 || g_pad.lx > 0x90) d[4] = g_pad.lx;
    if (fresh || g_pad.ly < 0x70 || g_pad.ly > 0x90) d[5] = g_pad.ly;
    if (fresh || g_pad.rx < 0x70 || g_pad.rx > 0x90) d[6] = g_pad.rx;
    if (fresh || g_pad.ry < 0x70 || g_pad.ry > 0x90) d[7] = g_pad.ry;
    if (g_pad.l2 > d[8]) d[8] = g_pad.l2;
    if (g_pad.r2 > d[9]) d[9] = g_pad.r2;
    d[76] = 1;
}

static int hook_ReadState(int32_t h, void *data) {
    int r = orig_ReadState(h, data);
    if (g_pad.connected && data) { inject((uint8_t *)data, r != 0); return 0; }
    return r;
}

static int hook_Read(int32_t h, void *data, int32_t count) {
    int r = orig_Read(h, data, count);
    if (g_pad.connected && data && count > 0) { inject((uint8_t *)data, r <= 0); return r > 0 ? r : 1; }
    return r;
}

static void *hook_thread(void *) {
    sceKernelSleep(5);
    int h = sceKernelLoadStartModule("/system/common/lib/libScePad.sprx", 0, 0, 0, 0, 0);
    if (h < 0) return 0;

    void *a1 = 0, *a2 = 0;
    sceKernelDlsym(h, "scePadReadState", &a1);
    sceKernelDlsym(h, "scePadRead", &a2);

    // [AJUSTAR] API de Detour del SDK (funciones C)
    Detour_Construct(&g_dReadState, (DetourMode)0);
    Detour_Construct(&g_dRead, (DetourMode)0);
    if (a1) orig_ReadState = (fn_ReadState)Detour_DetourFunction(&g_dReadState, (uint64_t)a1, (void *)hook_ReadState);
    if (a2) orig_Read      = (fn_Read)Detour_DetourFunction(&g_dRead, (uint64_t)a2, (void *)hook_Read);
    return 0;
}

extern "C" {

attr_module_hidden int module_start(size_t argc, const void *args) {
    OrbisPthread t1, t2;
    scePthreadCreate(&t1, 0, hook_thread, 0, "xbox_hook");
    scePthreadCreate(&t2, 0, usb_thread, 0, "xbox_usb");
    return 0;
}

attr_module_hidden int module_stop(size_t argc, const void *args) { return 0; }

}
