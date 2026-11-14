// xbox_pad.cpp v2.11 diagnostico - Xbox One/Series (GIP) por USB -> pad PS4
// Log en /data/xbox_pad.log + avisos en pantalla. Base 2.10 probada por el usuario en Dead Cells; 2.11 pendiente de prueba fisica.

#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <unistd.h>
#include <orbis/libkernel.h>
#include <orbis/_types/pad.h>
#include <Common.h>
#include <GoldHEN.h>
#include <Detour.h>
#include <HDE64.h>
#include "shared/config.h"

#ifndef attr_public
#define attr_public __attribute__((visibility("default")))
#endif
#ifndef attr_module_hidden
#define attr_module_hidden __attribute__((visibility("hidden")))
#endif

attr_public const char *g_pluginName = "xbox_pad";
attr_public const char *g_pluginDesc = "Xbox One/Series USB controller -> PS4 pad";
attr_public const char *g_pluginAuth = "community";
attr_public uint32_t g_pluginVersion = 0x00000211;

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
    if (__atomic_fetch_add(&g_loglines, 1, __ATOMIC_RELAXED) >= 1200) return;
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

// Contadores de diagnostico; los hooks pueden ejecutarse en otros hilos.
static uint32_t g_readstate_calls = 0, g_read_calls = 0, g_inject_calls = 0;
static int g_last_handle = 0, g_last_result = 0, g_last_count = 0, g_last_connected = -1;
// Resolver el pad dentro del sandbox antes de que jailbreak cambie las rutas.
static int g_pad_discovery_done = 0;

static xp::Config g_config = xp::defaultsConfig();
static char g_title[10];
static xp::State g_mapped = {};
static int g_ready = 0;
static int g_diagnostic = 0;
static unsigned g_state_lock = 0;
static void state_lock() { while (__atomic_exchange_n(&g_state_lock,1,__ATOMIC_ACQUIRE)) sceKernelUsleep(50); }
static void state_unlock() { __atomic_store_n(&g_state_lock,0,__ATOMIC_RELEASE); }
static void publish_state() {
    xp::State raw = {g_pad.buttons,{g_pad.lx,g_pad.ly,g_pad.rx,g_pad.ry},{g_pad.l2,g_pad.r2}};
    xp::State mapped=xp::map(raw,xp::selected(g_config,g_title));
    state_lock(); g_mapped=mapped; g_ready=g_pad.connected; state_unlock();
}
static void reload_config() { static uint64_t last=0; uint64_t now=xp_ptime(); if(now-last<1000000)return;last=now;xp::Config c;if(xp::load(xp::path,c)){g_config=c;__atomic_store_n(&g_diagnostic,c.diagnostic,__ATOMIC_RELAXED);}publish_state(); }

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
    if (n >= 6 && p[0] == 0x07 && p[3] == 2) {  // boton Xbox (guia) -> PS
        s_guide = (p[4] & 1) ? PS4_PS : 0;
        g_pad.buttons = s_main | s_guide;
        return;
    }
    if (n < 18 || p[0] != 0x20 || (p[1] & 0xC0) || p[3] < 14 || p[3] + 4 > n) return;  // 0x20 = informe de entrada

    if (!g_pad.connected) notify("Mando Xbox recibiendo entradas");
    g_pad.connected = 1;
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
    uint8_t l2 = (uint8_t)((lt > 1023 ? 1023 : lt) >> 2), r2 = (uint8_t)((rt > 1023 ? 1023 : rt) >> 2);
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
    static uint32_t last_buttons = 0;
    static unsigned button_logs = 0;
    if (g_config.diagnostic && g_pad.buttons != last_buttons && button_logs < 80) {
        last_buttons = g_pad.buttons;
        ++button_logs;
        logf_("pid=%d botones Xbox=%02X/%02X PS4=0x%08X", (int)getpid(), b0, b1, (unsigned)last_buttons);
    }
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
    *iface = 0; *inEp = 0; *outEp = 0;  // valores vistos en tu mando
    uint8_t cfg[256];
    memset(cfg, 0, sizeof(cfg));
    int len = p_ctrl(h, 0x80, 6, 0x0200, 0, cfg, 255, 1000);
    logf_("config descriptor: %d bytes", len);
    if (len > 9 && len <= 255) {
        uint8_t fi = 0xFF, fin = 0, fout = 0;
        bool match = false;
        int off = 0;
        while (off + 2 <= len) {
            uint8_t dl = cfg[off], dt = cfg[off + 1];
            if (dl < 2 || off + dl > len) break;
            if (dt == 4 && dl >= 9) {
                if (fi != 0xFF && fin && fout) break;
                fin = fout = 0; fi = 0xFF;
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
    if (!*inEp || !*outEp) { p_close(h); return false; }
    *hOut = h;
    return true;
}

static bool send_gip(void *h, uint8_t ep, const char *name, const uint8_t *packet, int len) {
    memcpy(g_tx, packet, len);
    g_tx[2] = g_seq++;
    int done = 0;
    int r = p_int(h, ep, g_tx, len, &done, 1000);
    logf_("%s -> r=0x%08X enviados=%d/%d", name, (unsigned)r, done, len);
    return r == 0 && done == len;
}

static bool send_initialization(void *h, uint8_t ep) {
    // Secuencia generica de xpad para mandos Xbox One y Series por USB.
    static const uint8_t power[] = {0x05, 0x20, 0x00, 0x01, 0x00};
    static const uint8_t led[] = {0x0A, 0x20, 0x00, 0x03, 0x00, 0x01, 0x14};
    static const uint8_t auth[] = {0x06, 0x20, 0x00, 0x02, 0x01, 0x00};
    bool ok = send_gip(h, ep, "power-on", power, sizeof(power));
    ok = send_gip(h, ep, "led-on", led, sizeof(led)) && ok;
    return send_gip(h, ep, "auth-done", auth, sizeof(auth)) && ok;
}

// ---------------- Hilo USB ----------------
static void *usb_thread(void *) {
    sceKernelSleep(3);
    while (!__atomic_load_n(&g_pad_discovery_done, __ATOMIC_ACQUIRE)) sceKernelUsleep(10000);
    logf_("usb_thread: inicio");
    int jr = xp_jailbreak(g_jb);
    logf_("jailbreak = %d", jr);
    if (!load_usbd()) { logf_("libSceUsbd no disponible: me rindo"); return 0; }
    void *ctx = 0;
    int ir = p_init(&ctx);
    logf_("sceUsbdInit = 0x%08X", (unsigned)ir);

    if(ir != 0 || jr != 0) return 0;
    int scans = 0;
    for (;;) {
        reload_config();
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
        if (!send_initialization(h, outEp)) { if(p_release)p_release(h,iface);p_close(h);sceKernelSleep(1);continue; }
        g_pad.connected = 0;
        notify("USB Xbox detectado (xbox_pad)");

        int errs = 0, npk = 0, poweron_extra = 0, resends = 0;
        int nreads = 0, ntimeouts = 0, nerrtotal = 0, fast_timeouts = 0, rx_logs = 0;
        uint8_t lastcmd = 0xFF;
        uint64_t t_start = xp_ptime();
        uint64_t t_status = t_start, t_poweron = t_start;

        for (;;) {
            int done = 0;
            reload_config();
            bool dbg = g_config.diagnostic && nreads < 25;
            if (dbg) logf_("leyendo #%d ...", nreads);
            uint64_t t0 = xp_ptime();
            int r = p_int(h, inEp, g_rx, 64, &done, 100);
            uint64_t t1 = xp_ptime();
            if (dbg) logf_("   r=0x%08X done=%d tardo=%u ms", (unsigned)r, done, (unsigned)((t1 - t0) / 1000));
            nreads++;

            if (r == 0 && done > 0 && done <= 64) {
                errs = 0;
                fast_timeouts = 0;
                if (g_config.diagnostic && rx_logs < 40 && (npk < 12 || g_rx[0] != lastcmd)) { log_hex("rx", g_rx, done); ++rx_logs; }
                npk++;
                lastcmd = g_rx[0];
                if (g_rx[0] == 0x02 && poweron_extra < 3) {  // anuncio: repetir encendido
                    poweron_extra++;
                    send_initialization(h, outEp);
                }
                if(done>=6 && g_rx[0]==7 && g_rx[1]==0x30 && g_rx[3]==2) {
                    uint8_t ack[]={1,0x20,g_rx[2],9,0,7,0x20,2,0,0,0,0,0};
                    int sent=0; int ar=p_int(h,outEp,ack,sizeof(ack),&sent,1000);
                    if(ar || sent!=(int)sizeof(ack)) logf_("ACK guia fallo r=%08X enviados=%d",ar,sent);
                }
                parse_gip(g_rx, done); publish_state();
            } else if (is_timeout(r)) {
                errs = 0;
                ntimeouts++;
                if (t1 >= t0 && t1 - t0 < 10000ULL) {
                    if (++fast_timeouts >= 20) {
                        logf_("timeouts inmediatos repetidos (0x%08X); reabriendo USB", (unsigned)r);
                        break;
                    }
                    sceKernelUsleep(10 * 1000);
                } else {
                    fast_timeouts = 0;
                }
            } else {
                fast_timeouts = 0;
                nerrtotal++;
                if (++errs == 1 && nerrtotal < 10) logf_("lectura r=0x%08X", (unsigned)r);
                if (errs > 20) break;
                sceKernelUsleep(5 * 1000);
            }

            // Reenviar la secuencia cada 2 s mientras no llegue ningun paquete
            if (!g_pad.connected && resends < 6 && (t1 - t_poweron) > 2000000ULL) {
                t_poweron = t1;
                resends++;
                logf_("sin paquetes aun: reenvio inicializacion (%d)", resends);
                send_initialization(h, outEp);
            }

            if (!g_pad.connected && t1-t_start>15000000ULL) break;
            // Resumen cada 5 s
            if ((t1 - t_status) > 5000000ULL) {
                t_status = t1;
                logf_("pid=%d estado: t=%us lecturas=%d timeouts=%d errores=%d paquetes=%d",
                      (int)getpid(), (unsigned)((t1 - t_start) / 1000000ULL), nreads, ntimeouts, nerrtotal, npk);
            }
        }

        logf_("mando desconectado o con error; reintento");
        notify("Mando Xbox desconectado (xbox_pad)");
        g_pad.connected = 0;
        g_pad.buttons = 0;
        g_pad.lx=g_pad.ly=g_pad.rx=g_pad.ry=128;g_pad.l2=g_pad.r2=0;publish_state();
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
typedef int (*fn_ReadStateTarget)(int32_t, void *, int);
typedef int (*fn_ReadTarget)(int32_t, void *, int32_t, int);

static Detour g_dReadState, g_dRead;
static fn_ReadState orig_ReadState;
static fn_Read orig_Read;
static fn_ReadStateTarget g_state_target;
static fn_ReadTarget g_read_target;
static fn_ReadStateTarget g_state_trampoline;
static fn_ReadTarget g_read_trampoline;
static Detour g_extStateDetour, g_extReadDetour, g_infoDetour, g_openDetour, g_handleDetour;
static fn_ReadState g_extStateOriginal;
static fn_Read g_extReadOriginal;
typedef int (*fn_Info)(int32_t, void *);
typedef int (*fn_Open)(int32_t, int32_t, int32_t, void *);
typedef int (*fn_Handle)(int32_t, uint32_t, uint32_t);
static fn_Info g_infoOriginal;
static fn_Open g_openOriginal;
static fn_Handle g_handleOriginal;
static uint32_t g_extStateCalls, g_extReadCalls, g_infoCalls, g_openCalls, g_handleCalls;
static int g_infoConnected = -1, g_infoResult, g_openResult, g_handleResult;
static uintptr_t g_hookedAddresses[7];
static unsigned g_hookedCount;
static uint32_t g_gameStateBaseline, g_gameReadBaseline, g_gameInjectBaseline;
static uint32_t g_gameExtStateBaseline, g_gameExtReadBaseline;

// El SDK puede devolver un stub sin comprobar el resultado de Mprotect.
// Comprobar las dos ramas escritas, no solo que el stub sea distinto de cero.
static bool verify_detour(const Detour *detour) {
    const uint8_t jump[] = {0xFF, 0x25, 0, 0, 0, 0};
    if (!detour->FunctionPtr || !detour->TrampolinePtr) return false;
    uintptr_t first = 0, second = 0;
    memcpy(&first, (const uint8_t *)detour->FunctionPtr + 6, sizeof(first));
    memcpy(&second, (const uint8_t *)detour->TrampolinePtr + 6, sizeof(second));
    return memcmp(detour->FunctionPtr, jump, sizeof(jump)) == 0 &&
           memcmp(detour->TrampolinePtr, jump, sizeof(jump)) == 0 &&
           first == (uintptr_t)detour->TrampolinePtr && second == (uintptr_t)detour->HookPtr;
}

static int resolve_pad_symbol(int module, const char *name, void **address) {
    *address = 0;
    int raw = sys_dynlib_dlsym(module, name, address);
    if (raw == 0 && *address) return 0;
    *address = 0;
    return sceKernelDlsym(module, name, address);
}

static bool has_pad_exports(int module) {
    void *state = 0, *read = 0;
    return resolve_pad_symbol(module, "scePadReadState", &state) == 0 && state &&
           resolve_pad_symbol(module, "scePadRead", &read) == 0 && read;
}

static int find_game_pad_module() {
    // Consultar lo que ya esta cargado antes de cargar otra copia del sistema.
    OrbisKernelModule modules[256] = {};
    size_t count = 0;
    int r = orbis_syscall(592, modules, (size_t)256, &count); // sys_dynlib_get_list
    logf_("pid=%d lista nativa: r=%d total=%u", (int)getpid(), r, (unsigned)count);
    int selected = -1;
    if (r == 0) {
        if (count > 256) count = 256;
        for (size_t i = 0; i < count; ++i) {
            OrbisKernelModuleInfo info = {};
            info.size = sizeof(info);
            int ir = sceKernelGetModuleInfo(modules[i], &info);
            info.name[sizeof(info.name) - 1] = 0;
            logf_("pid=%d nativo id=0x%X info=0x%08X nombre=%.100s", (int)getpid(),
                  modules[i], (unsigned)ir, ir == 0 ? info.name : "?");
            if (has_pad_exports(modules[i])) {
                logf_("pid=%d candidato Pad precargado=0x%X", (int)getpid(), modules[i]);
                if (selected < 0) selected = (int)modules[i];
            }
        }
    }
    if (selected >= 0) return selected;

    // Algunas listas ocultan modulos del sistema. Consultar handles mediante
    // la API de simbolos no lee ni escribe direcciones de memoria arbitrarias.
    for (unsigned bank = 0; bank < 2; ++bank) {
        unsigned base = bank ? 0x2000 : 0;
        for (unsigned i = 0; i < 0x400; ++i) {
            int handle = (int)(base + i);
            if (has_pad_exports(handle)) {
                logf_("pid=%d candidato Pad por handle=0x%X", (int)getpid(), handle);
                return handle;
            }
        }
    }

    // Mismo metodo que gamepad_helper oficial: ruta del sandbox + syscall.
    const char *word = sceKernelGetFsSandboxRandomWord();
    if (!word || !word[0]) { logf_("Pad: sandbox no disponible"); return -1; }
    char path[256];
    int n = snprintf(path, sizeof(path), "/%s/common/lib/libScePad.sprx", word);
    if (n < 0 || (size_t)n >= sizeof(path)) return -1;
    int handle = -1;
    r = sys_dynlib_load_prx(path, &handle);
    logf_("pid=%d Pad sandbox: r=%d handle=0x%X", (int)getpid(), r, (unsigned)handle);
    return r == 0 && handle >= 0 && has_pad_exports(handle) ? handle : -1;
}

// Detour copia las instrucciones a otra direccion. Rechazar cualquier
// prologo que no sea una secuencia conocida independiente de su ubicacion.
static bool safe_target_prologue(const uint8_t *code) {
    if (!code) return false;
    size_t n = 0;
    while (n < 14) {
        hde64s ins;
        unsigned len = hde64_disasm(code + n, &ins);
        if (!len || len > 15 || n + len > 32 || (ins.flags & (F_ERROR | F_RELATIVE)) || ins.p_67) return false;
        if ((ins.flags & F_MODRM) && ins.modrm_mod == 0 && ins.modrm_rm == 5) return false;
        uint8_t op = ins.opcode;
        if (!((op >= 0x50 && op <= 0x57) || (op >= 0xB8 && op <= 0xBF) ||
              op == 0x89 || op == 0x8B || op == 0x8D || op == 0x81 || op == 0x83 ||
              op == 0x31 || op == 0x33 || op == 0x85 || op == 0x90)) return false;
        n += len;
    }
    return true;
}

static void remember_hook(const void *addr) {
    if (addr && g_hookedCount < sizeof(g_hookedAddresses) / sizeof(g_hookedAddresses[0]))
        g_hookedAddresses[g_hookedCount++] = (uintptr_t)addr;
}

static bool overlaps_hook(const void *addr) {
    uintptr_t p = (uintptr_t)addr;
    for (unsigned i = 0; i < g_hookedCount; ++i) {
        uintptr_t q = g_hookedAddresses[i];
        if ((p >= q ? p - q : q - p) < 32) return true;
    }
    return false;
}

static void *try_extra_hook(int module, const char *name, Detour *detour, void *hook) {
    void *address = 0;
    int result = resolve_pad_symbol(module, name, &address);
    logf_("pid=%d prueba=%s dlsym=0x%08X addr=%p", (int)getpid(), name, (unsigned)result, address);
    if (result != 0 || !address) return 0;
    char tag[80];
    snprintf(tag, sizeof(tag), "pid=%d %s bytes:", (int)getpid(), name);
    log_hex(tag, (const uint8_t *)address, 32);
    if (overlaps_hook(address)) { logf_("pid=%d prueba=%s omitida: solapamiento", (int)getpid(), name); return 0; }
    if (!safe_target_prologue((const uint8_t *)address)) { logf_("pid=%d prueba=%s omitida: prologo no admitido", (int)getpid(), name); return 0; }
    Detour_Construct(detour, DetourMode_x64);
    void *original = Detour_DetourFunction(detour, (uint64_t)address, hook);
    if (original) remember_hook(address);
    logf_("pid=%d prueba=%s instalada=%d verificada=%d", (int)getpid(), name, original != 0, original && verify_detour(detour));
    return original;
}

// Las funciones observadas en 11.50 son: xor registro,registro; jmp rel32;
// y relleno NOP. El trampoline de Detour copia el salto sin reubicarlo.
// Solo aceptamos exactamente ese wrapper y llamamos su destino con el
// argumento adicional a cero, como hace el wrapper original.
static void *wrapper_target(const uint8_t *code, uint8_t zero_reg) {
    if (!code || code[0] != 0x31 || code[1] != zero_reg || code[2] != 0xE9) return 0;
    for (int i = 7; i < 14; ++i) if (code[i] != 0x90) return 0;
    int32_t displacement;
    memcpy(&displacement, code + 3, sizeof(displacement));
    uintptr_t target = (uintptr_t)(code + 7) + (intptr_t)displacement;
    if (!target || (target >= (uintptr_t)code && target < (uintptr_t)code + 14)) return 0;
    return (void *)target;
}

static int call_state_target(int32_t handle, void *data) { return g_state_target(handle, data, 0); }
static int call_read_target(int32_t handle, void *data, int32_t count) { return g_read_target(handle, data, count, 0); }

static void log_pad_modules() {
    // La API publica recibe el tamano en bytes (como plugin_loader oficial).
    // Reservar tambien espacio si un firmware interpreta ese valor como count.
    OrbisKernelModule modules[1024];
    size_t available = 0;
    int result = sceKernelGetModuleList(modules, 1024, &available);
    logf_("pid=%d modulos: r=0x%08X total=%u", (int)getpid(), (unsigned)result, (unsigned)available);
    if (result != 0) return;
    if (available > 1024) available = 1024;
    for (size_t i = 0; i < available; ++i) {
        OrbisKernelModuleInfo info;
        memset(&info, 0, sizeof(info));
        info.size = sizeof(info);
        int ir = sceKernelGetModuleInfo(modules[i], &info);
        if (ir != 0) { logf_("pid=%d modulo id=0x%X info=0x%08X", (int)getpid(), modules[i], (unsigned)ir); continue; }
        info.name[sizeof(info.name) - 1] = 0;
        logf_("pid=%d modulo id=0x%X nombre=%.120s", (int)getpid(), modules[i], info.name);
        if (!strstr(info.name, "Pad")) continue;
        void *read = 0, *state = 0;
        int rr = sceKernelDlsym(modules[i], "scePadRead", &read);
        int rs = sceKernelDlsym(modules[i], "scePadReadState", &state);
        logf_("pid=%d modulo=%s id=0x%X Read=%p(0x%08X) State=%p(0x%08X)",
              (int)getpid(), info.name, (unsigned)modules[i], read, (unsigned)rr, state, (unsigned)rs);
    }
}

static void inject(uint8_t *d, const xp::State &s) {
    uint32_t buttons; memcpy(&buttons,d,4); buttons |= s.buttons; memcpy(d,&buttons,4);
    for(int i=0;i<4;i++) if(s.axis[i]!=128) d[4+i]=s.axis[i];
    for(int i=0;i<2;i++) if(s.trigger[i]>d[8+i])d[8+i]=s.trigger[i];
}
static int post_ReadState(int32_t h, void *data, int r) {
    __atomic_fetch_add(&g_readstate_calls,1u,__ATOMIC_RELAXED);
    state_lock(); xp::State s=g_mapped; bool ready=g_ready;state_unlock();
    if(ready && r==0 && data && ((uint8_t*)data)[76]) { inject((uint8_t*)data,s);__atomic_fetch_add(&g_inject_calls,1u,__ATOMIC_RELAXED); }
    return r;
}
static int post_Read(int32_t h, void *data, int32_t count, int r) {
    __atomic_fetch_add(&g_read_calls,1u,__ATOMIC_RELAXED);
    state_lock();xp::State s=g_mapped;bool ready=g_ready;state_unlock();
    if(ready && data && r>0 && r<=count) for(int i=0;i<r;i++) {uint8_t *d=(uint8_t*)data+i*sizeof(OrbisPadData);if(d[76]){inject(d,s);__atomic_fetch_add(&g_inject_calls,1u,__ATOMIC_RELAXED);}}
    return r;
}

static int hook_ReadState(int32_t h, void *data) {
    if (!orig_ReadState) return -1;
    return post_ReadState(h, data, orig_ReadState(h, data));
}

static int hook_Read(int32_t h, void *data, int32_t count) {
    if (!orig_Read) return -1;
    return post_Read(h, data, count, orig_Read(h, data, count));
}

static int hook_StateTarget(int32_t h, void *data, int extra) {
    if (!g_state_trampoline) return -1;
    return post_ReadState(h, data, g_state_trampoline(h, data, extra));
}

static int hook_ReadTarget(int32_t h, void *data, int32_t count, int extra) {
    if (!g_read_trampoline) return -1;
    return post_Read(h, data, count, g_read_trampoline(h, data, count, extra));
}

static int hook_ExtState(int32_t h, void *data) {
    if (!g_extStateOriginal) return -1;
    __atomic_fetch_add(&g_extStateCalls, 1u, __ATOMIC_RELAXED);
    return post_ReadState(h, data, g_extStateOriginal(h, data));
}
static int hook_ExtRead(int32_t h, void *data, int32_t count) {
    if (!g_extReadOriginal) return -1;
    __atomic_fetch_add(&g_extReadCalls, 1u, __ATOMIC_RELAXED);
    return post_Read(h, data, count, g_extReadOriginal(h, data, count));
}
static int hook_Info(int32_t h, void *data) {
    if (!g_infoOriginal) return -1;
    int r = g_infoOriginal(h, data);
    __atomic_fetch_add(&g_infoCalls, 1u, __ATOMIC_RELAXED);
    __atomic_store_n(&g_infoResult, r, __ATOMIC_RELAXED);
    __atomic_store_n(&g_infoConnected, (r == 0 && data) ? ((uint8_t *)data)[12] : -1, __ATOMIC_RELAXED);
    return r;
}
static int hook_Open(int32_t user, int32_t type, int32_t index, void *param) {
    if (!g_openOriginal) return -1;
    int r = g_openOriginal(user, type, index, param);
    __atomic_fetch_add(&g_openCalls, 1u, __ATOMIC_RELAXED);
    __atomic_store_n(&g_openResult, r, __ATOMIC_RELAXED);
    return r;
}
static int hook_Handle(int32_t user, uint32_t type, uint32_t index) {
    if (!g_handleOriginal) return -1;
    int r = g_handleOriginal(user, type, index);
    __atomic_fetch_add(&g_handleCalls, 1u, __ATOMIC_RELAXED);
    __atomic_store_n(&g_handleResult, r, __ATOMIC_RELAXED);
    return r;
}

static void self_test_pad(int module) {
    const char *names[] = {"scePadReadState", "scePadRead", "scePadReadStateExt", "scePadReadExt"};
    for (unsigned i = 0; i < 4; ++i) {
        // No abrir usuarios ni mandar entradas: un handle invalido debe devolver
        // error. Solo comprobamos si el recorrido pasa por nuestro hook.
        bool installed = i == 0 ? (orig_ReadState || g_state_trampoline) :
                         i == 1 ? (orig_Read || g_read_trampoline) :
                         i == 2 ? g_extStateOriginal != 0 : g_extReadOriginal != 0;
        if (!installed) continue;
        void *address = 0;
        if (resolve_pad_symbol(module, names[i], &address) != 0 || !address) continue;
        OrbisPadData sample = {};
        uint32_t before = __atomic_load_n((i & 1) ? &g_read_calls : &g_readstate_calls, __ATOMIC_RELAXED);
        int r = (i & 1) ? ((fn_Read)address)(-1, &sample, 1) : ((fn_ReadState)address)(-1, &sample);
        uint32_t after = __atomic_load_n((i & 1) ? &g_read_calls : &g_readstate_calls, __ATOMIC_RELAXED);
        logf_("pid=%d autoprueba=%s resultado=0x%08X interceptada=%u", (int)getpid(), names[i], (unsigned)r, after - before);
    }
    // Los resumenes de juego excluyen TODAS las llamadas anteriores a este punto.
    g_gameStateBaseline = __atomic_load_n(&g_readstate_calls, __ATOMIC_RELAXED);
    g_gameReadBaseline = __atomic_load_n(&g_read_calls, __ATOMIC_RELAXED);
    g_gameInjectBaseline = __atomic_load_n(&g_inject_calls, __ATOMIC_RELAXED);
    g_gameExtStateBaseline = __atomic_load_n(&g_extStateCalls, __ATOMIC_RELAXED);
    g_gameExtReadBaseline = __atomic_load_n(&g_extReadCalls, __ATOMIC_RELAXED);
}

static void *hook_thread(void *) {
    log_pad_modules();
    int h = find_game_pad_module();
    logf_("pid=%d libScePad seleccionada = 0x%08X", (int)getpid(), (unsigned)h);
    if (h < 0) {
        logf_("Pad: no se encontro la biblioteca del juego; USB continuara como diagnostico");
        __atomic_store_n(&g_pad_discovery_done, 1, __ATOMIC_RELEASE);
        notify("xbox_pad: no se encontro libScePad del juego");
        return 0;
    }

    void *a1 = 0, *a2 = 0;
    resolve_pad_symbol(h, "scePadReadState", &a1);
    resolve_pad_symbol(h, "scePadRead", &a2);
    logf_("pid=%d scePadReadState=%p scePadRead=%p", (int)getpid(), a1, a2);
    char tag[64];
    if (a1) { snprintf(tag, sizeof(tag), "pid=%d State bytes:", (int)getpid()); log_hex(tag, (const uint8_t *)a1, 16); }
    if (a2) { snprintf(tag, sizeof(tag), "pid=%d Read bytes:", (int)getpid()); log_hex(tag, (const uint8_t *)a2, 16); }

    g_state_target = (fn_ReadStateTarget)wrapper_target((const uint8_t *)a1, 0xD2);
    g_read_target = (fn_ReadTarget)wrapper_target((const uint8_t *)a2, 0xC9);
    logf_("pid=%d destinos wrapper: State=%p Read=%p", (int)getpid(), (void *)g_state_target, (void *)g_read_target);
    if (g_state_target) { snprintf(tag, sizeof(tag), "pid=%d State destino:", (int)getpid()); log_hex(tag, (const uint8_t *)g_state_target, 32); }
    if (g_read_target) { snprintf(tag, sizeof(tag), "pid=%d Read destino:", (int)getpid()); log_hex(tag, (const uint8_t *)g_read_target, 32); }
    Detour_Construct(&g_dReadState, DetourMode_x64);
    Detour_Construct(&g_dRead, DetourMode_x64);
    if (g_state_target) {
        if (g_state_target != (fn_ReadStateTarget)g_read_target && safe_target_prologue((const uint8_t *)g_state_target))
            g_state_trampoline = (fn_ReadStateTarget)Detour_DetourFunction(&g_dReadState, (uint64_t)g_state_target, (void *)hook_StateTarget);
        if (!g_state_trampoline) {
            orig_ReadState = call_state_target;
            if (!Detour_DetourFunction(&g_dReadState, (uint64_t)a1, (void *)hook_ReadState)) orig_ReadState = 0;
        }
    }
    if (g_read_target) {
        if (g_state_target != (fn_ReadStateTarget)g_read_target && safe_target_prologue((const uint8_t *)g_read_target))
            g_read_trampoline = (fn_ReadTarget)Detour_DetourFunction(&g_dRead, (uint64_t)g_read_target, (void *)hook_ReadTarget);
        if (!g_read_trampoline) {
            orig_Read = call_read_target;
            if (!Detour_DetourFunction(&g_dRead, (uint64_t)a2, (void *)hook_Read)) orig_Read = 0;
        }
    }
    logf_("pid=%d hooks: State=%s Read=%s", (int)getpid(),
          g_state_trampoline ? "destino" : (orig_ReadState ? "wrapper" : "no"),
          g_read_trampoline ? "destino" : (orig_Read ? "wrapper" : "no"));
    logf_("pid=%d parches verificados: State=%d Read=%d", (int)getpid(), verify_detour(&g_dReadState), verify_detour(&g_dRead));
    remember_hook(g_state_trampoline ? (void *)g_state_target : (orig_ReadState ? a1 : 0));
    remember_hook(g_read_trampoline ? (void *)g_read_target : (orig_Read ? a2 : 0));
    g_extStateOriginal = (fn_ReadState)try_extra_hook(h, "scePadReadStateExt", &g_extStateDetour, (void *)hook_ExtState);
    g_extReadOriginal = (fn_Read)try_extra_hook(h, "scePadReadExt", &g_extReadDetour, (void *)hook_ExtRead);
    g_infoOriginal = (fn_Info)try_extra_hook(h, "scePadGetControllerInformation", &g_infoDetour, (void *)hook_Info);
    g_openOriginal = (fn_Open)try_extra_hook(h, "scePadOpen", &g_openDetour, (void *)hook_Open);
    g_handleOriginal = (fn_Handle)try_extra_hook(h, "scePadGetHandle", &g_handleDetour, (void *)hook_Handle);

    self_test_pad(h);
    __atomic_store_n(&g_pad_discovery_done, 1, __ATOMIC_RELEASE);
    if ((orig_ReadState || g_state_trampoline || g_extStateOriginal) && (orig_Read || g_read_trampoline || g_extReadOriginal)) notify("xbox_pad v2.11 diagnostico cargado");
    else notify("xbox_pad v2.11: hook no disponible");
    int summaries = 0;
    for (;;) {
        sceKernelSleep(5);
        state_lock(); int ready=g_ready; uint32_t mapped_buttons=g_mapped.buttons; state_unlock();
        ++summaries;
        if (!__atomic_load_n(&g_diagnostic,__ATOMIC_RELAXED) && summaries%6) continue;
        logf_("pid=%d juego desde autoprueba: ReadState=%u Read=%u inyecciones=%u USB=%d botones=0x%08X",
              (int)getpid(), __atomic_load_n(&g_readstate_calls, __ATOMIC_RELAXED) - g_gameStateBaseline,
              __atomic_load_n(&g_read_calls, __ATOMIC_RELAXED) - g_gameReadBaseline,
              __atomic_load_n(&g_inject_calls, __ATOMIC_RELAXED) - g_gameInjectBaseline, ready, (unsigned)mapped_buttons);
        logf_("pid=%d pruebas: StateExt=%u ReadExt=%u Info=%u Open=%u GetHandle=%u",
              (int)getpid(), __atomic_load_n(&g_extStateCalls, __ATOMIC_RELAXED) - g_gameExtStateBaseline, __atomic_load_n(&g_extReadCalls, __ATOMIC_RELAXED) - g_gameExtReadBaseline,
              __atomic_load_n(&g_infoCalls, __ATOMIC_RELAXED), __atomic_load_n(&g_openCalls, __ATOMIC_RELAXED), __atomic_load_n(&g_handleCalls, __ATOMIC_RELAXED));
        if (__atomic_load_n(&g_infoCalls, __ATOMIC_RELAXED) || __atomic_load_n(&g_openCalls, __ATOMIC_RELAXED) || __atomic_load_n(&g_handleCalls, __ATOMIC_RELAXED))
            logf_("pid=%d originales: Info=0x%08X conectado=%d Open=0x%08X GetHandle=0x%08X",
                  (int)getpid(), (unsigned)__atomic_load_n(&g_infoResult, __ATOMIC_RELAXED), __atomic_load_n(&g_infoConnected, __ATOMIC_RELAXED),
                  (unsigned)__atomic_load_n(&g_openResult, __ATOMIC_RELAXED), (unsigned)__atomic_load_n(&g_handleResult, __ATOMIC_RELAXED));
        if (__atomic_load_n(&g_readstate_calls, __ATOMIC_RELAXED) || __atomic_load_n(&g_read_calls, __ATOMIC_RELAXED))
            logf_("pid=%d ultima lectura: handle=%d resultado=0x%08X count=%d conectado_original=%d",
                  (int)getpid(), __atomic_load_n(&g_last_handle, __ATOMIC_RELAXED),
                  (unsigned)__atomic_load_n(&g_last_result, __ATOMIC_RELAXED),
                  __atomic_load_n(&g_last_count, __ATOMIC_RELAXED), __atomic_load_n(&g_last_connected, __ATOMIC_RELAXED));
    }
    return 0;
}

extern "C" {

static int g_started = 0;
attr_module_hidden int module_start(size_t argc, const void *args) {
    int expected = 0;
    if (!__atomic_compare_exchange_n(&g_started, &expected, 1, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) return 0;
    logf_("xbox_pad v2.11 diagnostico cargado pid=%d", (int)getpid());
    struct proc_info info;
    memset(&info, 0, sizeof(info));
    int r = sys_sdk_proc_info(&info);
    logf_("pid=%d proceso: r=0x%08X titulo=%.16s nombre=%.40s", (int)getpid(), (unsigned)r,
          r == 0 ? info.titleid : "", r == 0 ? info.name : "");
    if(r != 0 || !xp::titleValid(info.titleid)) return 0;
    memcpy(g_title,info.titleid,9);g_title[9]=0;
    OrbisPthread t1, t2;
    int hr = scePthreadCreate(&t1, 0, hook_thread, 0, "xbox_hook");
    if (hr != 0) __atomic_store_n(&g_pad_discovery_done, 1, __ATOMIC_RELEASE);
    int ur = scePthreadCreate(&t2, 0, usb_thread, 0, "xbox_usb");
    logf_("pid=%d hilos: hook=0x%08X USB=0x%08X", (int)getpid(), (unsigned)hr, (unsigned)ur);
    return 0;
}

// plugin_loader de GoldHEN exige los dos exports aunque module_start ya haya
// arrancado los hilos. La guardia evita arrancar dos lectores del mismo USB.
attr_public int plugin_load(int argc, const char *argv[]) {
    logf_("pid=%d GoldHEN plugin_load", (int)getpid());
    return module_start((size_t)argc, argv);
}

// El codigo sigue ejecutandose en hilos y hooks; se descarga al cerrar el juego.
attr_public int plugin_unload(int argc, const char *argv[]) { return -1; }
attr_module_hidden int module_stop(size_t argc, const void *args) { return -1; }

}
