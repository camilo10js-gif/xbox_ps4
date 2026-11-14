// xbox_pad.cpp v2.3 - Xbox One/Series (GIP) por USB -> pad PS4
// Log en /data/xbox_pad.log + avisos en pantalla. Sin probar en consola real.

#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
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
attr_public uint32_t g_pluginVersion = 0x00000203;

// Funciones del SDK/sistema con alias propio (evita choques con cabeceras)
extern "C" int xp_jailbreak(void *) __asm__("sys_sdk_jailbreak");
extern "C" int xp_sysmodule(uint32_t) __asm__("sceSysmoduleLoadModule");
extern "C" int xp_notify_raw(int, void *, size_t, int) __asm__("sceKernelSendNotificationRequest");
extern "C" uint64_t xp_ptime(void) __asm__("sceKernelGetProcessTime");

#define XBOX_VID 0x045E
static const uint16_t kPids[] = {0x0B12, 0x0B00, 0x02EA, 0x02DD, 0x02D1, 0x02E3};
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
#define PS4_PS       0x00010000
#define PS4_TOUCHPAD 0x00100000

// ---------------- Log ----------------
static int g_loglines = 0;
static void logf_(const char *fmt, ...) {
    if (g_loglines > 1200) return;
    g_loglines++;
    char b[256];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(b, sizeof(b) - 2, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if (n > 250) n = 250;
    b[n++] = '\n';
    int fd = sceKernelOpen("/data/xbox_pad.log", 0x0001 | 0x0008 | 0x0200, 0666);
    if (fd >= 0) { sceKernelWrite(fd, b, n); sceKernelClose(fd); }
}

static void log_hex(const char *tag, const uint8_t *p, int n) {
    char s[200];
    int k = 0;
    for (int i = 0; i < n && i < 32 && k < 190; i++)
        k += snprintf(s + k, sizeof(s) - k, "%02X ", p[i]);
    logf_("%s %s", tag, s);
}

// ---------------- Aviso en pantalla ----------------
struct XpNotif {
    int type;
    int reqId;
    int priority;
    int msgId;
    int targetId;
    int userId;
    int unk1;
    int unk2;
    int appId;
    int errorNum;
    int unk3;
    unsigned char useIconImageUri;
    char message[1024];
    char iconImageUri[1024];
};

static void notify(const char *msg) {
    XpNotif n;
    memset(&n, 0, sizeof(n));
    n.type = 0;
    n.unk3 = 0;
    n.useIconImageUri = 1;
    snprintf(n.message, sizeof(n.message), "%s", msg);
    snprintf(n.iconImageUri, sizeof(n.iconImageUri), "cxml://psnotification/tex_icon_system");
    int r = xp_notify_raw(0, &n, sizeof(n), 0);
    logf_("aviso '%s' -> 0x%08X", msg, (unsigned)r);
}

// ---------------- Estado compartido ----------------
struct PadState {
    volatile uint32_t buttons;
    volatile uint8_t lx, ly, rx, ry, l2, r2;
    volatile int connected;
};
static PadState g_pad = {0, 0x80, 0x80, 0x80, 0x80, 0, 0, 0};

// ---------------- libSceUsbd ----------------
// OJO: en la PS4 sceUsbdGetDeviceList recibe UN solo parametro (la lista).
typedef int (*fn_init)(void **ctx);
typedef int (*fn_list)(void ***list);
typedef void (*fn_freelist)(void **list, int unref);
typedef int (*fn_getdesc)(void *dev, uint8_t *desc);
typedef int (*fn_open)(void *dev, void **handle);
typedef void (*fn_close)(void *h);
typedef int (*fn_iface)(void *h, int iface);
typedef int (*fn_ctrl)(void *h, uint8_t bm, uint8_t req, uint16_t val, uint16_t idx, uint8_t *data, uint16_t len, unsigned to);
typedef int (*fn_xfer)(void *h, uint8_t ep, uint8_t *data, int len, int *done, unsigned to);

static fn_init p_init;
static fn_list p_list;
static fn_freelist p_freelist;
static fn_getdesc p_getdesc;
static fn_open p_open;
static fn_close p_close;
static fn_iface p_claim, p_release, p_kdactive, p_detach;
static fn_ctrl p_ctrl;
static fn_xfer p_int;

static bool load_usbd() {
    int r0 = xp_sysmodule(0xB7);  // modulo USBD
    logf_("sysmodule USBD = 0x%08X", (unsigned)r0);
    int h = sceKernelLoadStartModule("/system/common/lib/libSceUsbd.sprx", 0, 0, 0, 0, 0);
    logf_("LoadStartModule libSceUsbd = 0x%08X", (unsigned)h);
    if (h < 0) return false;
    sceKernelDlsym(h, "sceUsbdInit", (void **)&p_init);
    sceKernelDlsym(h, "sceUsbdGetDeviceList", (void **)&p_list);
    sceKernelDlsym(h, "sceUsbdFreeDeviceList", (void **)&p_freelist);
    sceKernelDlsym(h, "sceUsbdGetDeviceDescriptor", (void **)&p_getdesc);
    sceKernelDlsym(h, "sceUsbdOpen", (void **)&p_open);
    sceKernelDlsym(h, "sceUsbdClose", (void **)&p_close);
    sceKernelDlsym(h, "sceUsbdClaimInterface", (void **)&p_claim);
    sceKernelDlsym(h, "sceUsbdReleaseInterface", (void **)&p_release);
    sceKernelDlsym(h, "sceUsbdKernelDriverActive", (void **)&p_kdactive);
    sceKernelDlsym(h, "sceUsbdDetachKernelDriver", (void **)&p_detach);
    sceKernelDlsym(h, "sceUsbdControlTransfer", (void **)&p_ctrl);
    sceKernelDlsym(h, "sceUsbdInterruptTransfer", (void **)&p_int);
    logf_("dlsym: init=%d list=%d desc=%d open=%d claim=%d ctrl=%d int=%d",
          p_init != 0, p_list != 0, p_getdesc != 0, p_open != 0, p_claim != 0, p_ctrl != 0, p_int != 0);
    return p_init && p_list && p_freelist && p_getdesc && p_open && p_close && p_claim && p_ctrl && p_int;
}

// timeout en esta consola: 0x80240006 (visto en el log de AJ) o -7 (libusb)
static bool is_timeout(int r) {
    return r == -7 || (unsigned)r == 0x80240006u || (unsigned)r == 0x80240007u;
}

// ---------------- Traduccion GIP -> PS4 ----------------
static uint8_t axis16(int16_t v)    { return (uint8_t)(((int)v + 32768) >> 8); }
static uint8_t invaxis16(int16_t v) { return (uint8_t)(255 - (((int)v + 32768) >> 8)); }

static uint32_t s_main = 0, s_guide = 0;

static void parse_gip(const uint8_t *p, int n) {
    if (n >= 5 && p[0] == 0x07) {  // boton Xbox (guia) -> PS
        s_guide = (p[4] & 1) ? PS4_PS : 0;
        g_pad.buttons = s_main | s_guide;
        return;
    }
    if (n < 18 || p[0] != 0x20) return;  // 0x20 = informe de entrada

    uint8_t b0 = p[4], b1 = p[5];
    uint32_t o = 0;
    if (b0 & 0x04) o |= PS4_OPTIONS;   // Menu
    if (b0 & 0x08) o |= PS4_TOUCHPAD;  // View
    if (b0 & 0x10) o |= PS4_CROSS;     // A
    if (b0 & 0x20) o |= PS4_CIRCLE;    // B
    if (b0 & 0x40) o |= PS4_SQUARE;    // X
    if (b0 & 0x80) o |= PS4_TRIANGLE;  // Y
    if (b1 & 0x01) o |= PS4_UP;
    if (b1 & 0x02) o |= PS4_DOWN;
    if (b1 & 0x04) o |= PS4_LEFT;
    if (b1 & 0x08) o |= PS4_RIGHT;
    if (b1 & 0x10) o |= PS4_L1;
    if (b1 & 0x20) o |= PS4_R1;
    if (b1 & 0x40) o |= PS4_L3;
    if (b1 & 0x80) o |= PS4_R3;

    uint16_t lt = (uint16_t)(p[6] | (p[7] << 8));  // 0..1023
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
    s_main = o;
    g_pad.buttons = o | s_guide;
}

// ---------------- Abrir el mando ----------------
static uint8_t g_jb[128] __attribute__((aligned(16)));
static uint8_t g_rx[64] __attribute__((aligned(64)));
static uint8_t g_tx[64] __attribute__((aligned(64)));
static uint8_t g_seq = 0;

static uint32_t g_seen[16];
static int g_nseen = 0;
static void note_device(uint16_t vid, uint16_t pid) {
    uint32_t key = ((uint32_t)vid << 16) | pid;
    for (int i = 0; i < g_nseen; i++) if (g_seen[i] == key) return;
    if (g_nseen < 16) g_seen[g_nseen++] = key;
    logf_("bus: dispositivo %04X:%04X", vid, pid);
}

static bool find_and_open(void *ctx, void **hOut, int *iface, uint8_t *inEp, uint8_t *outEp) {
    (void)ctx;
    void **list = 0;
    int cnt = p_list(&list);
    static int s_lastcnt = -999;
    if (cnt != s_lastcnt) { s_lastcnt = cnt; logf_("GetDeviceList = %d", cnt); }
    if (cnt <= 0 || !list) return false;

    void *found = 0;
    for (int i = 0; i < cnt && !found; i++) {
        uint8_t d[18];
        memset(d, 0, sizeof(d));
        if (p_getdesc(list[i], d) != 0) continue;
        uint16_t vid = (uint16_t)(d[8] | (d[9] << 8));
        uint16_t pid = (uint16_t)(d[10] | (d[11] << 8));
        note_device(vid, pid);
        if (vid != XBOX_VID) continue;
        for (unsigned k = 0; k < NUM_PIDS; k++)
            if (kPids[k] == pid) found = list[i];
    }

    void *h = 0;
    bool ok = false;
    if (found) {
        int r = p_open(found, &h);
        logf_("sceUsbdOpen = 0x%08X", (unsigned)r);
        ok = (r == 0 && h);
    }
    p_freelist(list, 1);
    if (!ok) return false;

    // Descriptor de configuracion: buscar interfaz FF/47/D0 y sus endpoints
    *iface = 0; *inEp = 0x82; *outEp = 0x02;  // valores vistos en tu mando
    uint8_t cfg[256];
    memset(cfg, 0, sizeof(cfg));
    int len = p_ctrl(h, 0x80, 6, 0x0200, 0, cfg, 255, 1000);
    logf_("config descriptor: %d bytes", len);
    if (len > 9) {
        uint8_t fi = 0xFF, fin = 0, fout = 0;
        bool match = false;
        int off = 0;
        while (off + 2 <= len) {
            uint8_t dl = cfg[off], dt = cfg[off + 1];
            if (dl < 2) break;
            if (dt == 4 && dl >= 9) {
                match = (cfg[off + 3] == 0 && cfg[off + 5] == 0xFF && cfg[off + 6] == 0x47 && cfg[off + 7] == 0xD0);
                if (match && fi == 0xFF) fi = cfg[off + 2];
            } else if (dt == 5 && match && dl >= 7 && (cfg[off + 3] & 3) == 3) {
                if (cfg[off + 2] & 0x80) { if (!fin) fin = cfg[off + 2]; }
                else { if (!fout) fout = cfg[off + 2]; }
            }
            off += dl;
        }
        if (fi != 0xFF && fin && fout) { *iface = fi; *inEp = fin; *outEp = fout; }
        logf_("GIP: iface=%d IN=0x%02X OUT=0x%02X", *iface, *inEp, *outEp);
    }
    *hOut = h;
    return true;
}

static bool send_power_on(void *h, uint8_t ep) {
    g_tx[0] = 0x05; g_tx[1] = 0x20; g_tx[2] = g_seq++; g_tx[3] = 0x01; g_tx[4] = 0x00;
    int done = 0;
    int r = p_int(h, ep, g_tx, 5, &done, 1000);
    logf_("power-on -> r=0x%08X enviados=%d", (unsigned)r, done);
    return r == 0;
}

// ---------------- Hilo USB ----------------
static void *usb_thread(void *) {
    sceKernelSleep(3);
    logf_("usb_thread: inicio");
    int jr = xp_jailbreak(g_jb);
    logf_("jailbreak = %d", jr);
    if (!load_usbd()) { logf_("libSceUsbd no disponible: me rindo"); return 0; }
    void *ctx = 0;
    int ir = p_init(&ctx);
    logf_("sceUsbdInit = 0x%08X", (unsigned)ir);

    int scans = 0;
    for (;;) {
        void *h = 0;
        int iface = 0;
        uint8_t inEp = 0, outEp = 0;
        if (!find_and_open(ctx, &h, &iface, &inEp, &outEp)) {
            if (++scans == 1 || scans % 30 == 0) logf_("sin mando Xbox en el bus (intento %d)", scans);
            sceKernelUsleep(1000 * 1000);
            continue;
        }
        scans = 0;

        if (p_kdactive && p_detach && p_kdactive(h, iface) == 1) {
            int dr = p_detach(h, iface);
            logf_("detach kernel driver = 0x%08X", (unsigned)dr);
        }
        int cr = p_claim(h, iface);
        logf_("claim iface %d = 0x%08X", iface, (unsigned)cr);
        if (cr != 0) {
            p_close(h);
            sceKernelUsleep(2000 * 1000);
            continue;
        }

        g_seq = 0;
        send_power_on(h, outEp);
        g_pad.connected = 1;
        notify("Mando Xbox conectado (xbox_pad)");

        int errs = 0, npk = 0, poweron_extra = 0, resends = 0;
        int nreads = 0, ntimeouts = 0, nerrtotal = 0;
        uint8_t lastcmd = 0xFF;
        uint64_t t_start = xp_ptime();
        uint64_t t_status = t_start, t_poweron = t_start;

        for (;;) {
            int done = 0;
            bool dbg = nreads < 25;
            if (dbg) logf_("leyendo #%d ...", nreads);
            uint64_t t0 = xp_ptime();
            int r = p_int(h, inEp, g_rx, 64, &done, 100);
            uint64_t t1 = xp_ptime();
            if (dbg) logf_("   r=0x%08X done=%d tardo=%u ms", (unsigned)r, done, (unsigned)((t1 - t0) / 1000));
            nreads++;

            if (r == 0 && done > 0) {
                errs = 0;
                if (npk < 12 || g_rx[0] != lastcmd) { log_hex("rx", g_rx, done); }
                npk++;
                lastcmd = g_rx[0];
                if (g_rx[0] == 0x02 && poweron_extra < 3) {  // anuncio: repetir encendido
                    poweron_extra++;
                    send_power_on(h, outEp);
                }
                parse_gip(g_rx, done);
            } else if (is_timeout(r)) {
                errs = 0;
                ntimeouts++;
            } else {
                nerrtotal++;
                if (++errs == 1 && nerrtotal < 10) logf_("lectura r=0x%08X", (unsigned)r);
                if (errs > 20) break;
                sceKernelUsleep(5 * 1000);
            }

            // Reenviar el encendido cada 2 s mientras no llegue ningun paquete
            if (npk == 0 && resends < 6 && (t1 - t_poweron) > 2000000ULL) {
                t_poweron = t1;
                resends++;
                logf_("sin paquetes aun: reenvio encendido (%d)", resends);
                send_power_on(h, outEp);
            }

            // Resumen cada 5 s
            if ((t1 - t_status) > 5000000ULL) {
                t_status = t1;
                logf_("estado: t=%us lecturas=%d timeouts=%d errores=%d paquetes=%d",
                      (unsigned)((t1 - t_start) / 1000000ULL), nreads, ntimeouts, nerrtotal, npk);
            }
        }

        logf_("mando desconectado o con error; reintento");
        notify("Mando Xbox desconectado (xbox_pad)");
        g_pad.connected = 0;
        g_pad.buttons = 0;
        s_main = s_guide = 0;
        if (p_release) p_release(h, iface);
        p_close(h);
        sceKernelUsleep(1000 * 1000);
    }
    return 0;
}

// ---------------- Hooks de scePad ----------------
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
    if (!orig_ReadState) return -1;
    int r = orig_ReadState(h, data);
    if (g_pad.connected && data) { inject((uint8_t *)data, r != 0); return 0; }
    return r;
}

static int hook_Read(int32_t h, void *data, int32_t count) {
    if (!orig_Read) return -1;
    int r = orig_Read(h, data, count);
    if (g_pad.connected && data && count > 0) { inject((uint8_t *)data, r
