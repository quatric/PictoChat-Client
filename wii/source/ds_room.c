/* Hosting a DS PictoChat room on WD, the Wii's DS wireless driver. The WD
 * bring-up, beacon and threads follow PiictoChat by Abdelali221
 * (github.com/abdelali221/PiictoChat); ds_awc.c runs the room itself. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <gccore.h>
#include <ogc/lwp_watchdog.h>
#include "ds_room.h"
#include "ds_awc.h"
#include "dsmsg.h"
#include "settings.h"
#include "wd.h"

#define STACKSIZE 0x8000
#define SEND_INTERVAL_US 4000
/* Bytes 6-7 of the MP send config: each child's reply slot per MP cycle.
 * An AWC child sends up to 192 bytes. */
#define CHILD_REPLY_SIZE 0xC8
#define EVENT_QUEUE 32

/* The DS colours in the order of PictoChat's profile, as pict.chat's swatches */
static const u32 DS_COLORS[16] = {
    0x61829a, 0xba4900, 0xfb0018, 0xfb8afb, 0xfb9200, 0xf3e300, 0xaafb00, 0x00fb00,
    0x00a238, 0x49db8a, 0x30baf3, 0x0059f3, 0x000092, 0x8a00d3, 0xd300eb, 0xfb0092,
};

typedef struct {
    u32 header;
    u32 id;
    u16 timer;
    u16 unk0;
    u32 id_2;
    u16 status;
    u16 random_id;
    u8 current_room;
    u8 users_connected;
    u16 unk1;
} ds_beacon;

typedef struct {
    int joined;
    char name[32];
    u32 color;
} member_t;

static ds_beacon beacon __attribute__((aligned(32)));
static u8 send_conf[0x10] __attribute__((aligned(32)));
static u8 send_buf[0x210] __attribute__((aligned(32)));
static u8 recv_buf[0x2000] __attribute__((aligned(32)));

static u8 beacon_stack[STACKSIZE], recv_stack[STACKSIZE], notify_stack[STACKSIZE], send_stack[STACKSIZE];
static lwp_t beacon_thread, recv_thread, notify_thread, send_thread;
static volatile int running;
static volatile int threads_alive;  /* the receive and notification threads can outlive a stop */
static int active, used;
static s32 lock_id = -1;
static int room_index;

static member_t members[AWC_MAX_AID + 1];
static mutex_t ev_lock = LWP_MUTEX_NULL;
static chat_event events[EVENT_QUEUE];
static int ev_rd, ev_wr;

static inline u16 get16(const u8 *p) { return p[0] | (p[1] << 8); }

static void seq_path(char *out, int len)
{
    snprintf(out, len, "%s/ds.seq", app_dir);
}

/* ── Events, made on the WD threads and taken by the UI ─────────────────── */

static chat_event *ev_begin(chat_event_type type)
{
    LWP_MutexLock(ev_lock);
    if (ev_wr - ev_rd >= EVENT_QUEUE) {
        LWP_MutexUnlock(ev_lock);
        return NULL;
    }
    chat_event *ev = &events[ev_wr % EVENT_QUEUE];
    memset(ev, 0, sizeof *ev);
    ev->type = type;
    return ev;  /* locked until ev_end */
}

static void ev_end(void)
{
    ev_wr++;
    LWP_MutexUnlock(ev_lock);
}

static void ev_simple(chat_event_type type, const char *name, u32 color, const char *text)
{
    chat_event *ev = ev_begin(type);
    if (!ev) return;
    if (name) snprintf(ev->name, sizeof ev->name, "%s", name);
    ev->color = color;
    if (text) snprintf(ev->text, sizeof ev->text, "%s", text);
    ev_end();
}

int ds_room_poll(chat_event *ev)
{
    memset(ev, 0, sizeof *ev);
    if (ev_lock == LWP_MUTEX_NULL) return 0;
    LWP_MutexLock(ev_lock);
    int got = ev_rd != ev_wr;
    if (got) *ev = events[ev_rd++ % EVENT_QUEUE];
    LWP_MutexUnlock(ev_lock);
    return got;
}

/* ── Room members (these run inside ds_awc, which holds its lock) ──────── */

static void read_profile(member_t *m, const u8 *msg)
{
    int n = 0;
    for (int i = 0; i < 10; i++) {
        u16 c = get16(msg + 8 + i * 2);
        if (!c) break;
        m->name[n++] = c >= 0x20 && c < 0x7F ? c : '?';
    }
    m->name[n] = 0;
    m->color = DS_COLORS[get16(msg + 80) & 0xF];
}

static void on_message(u16 aid, const u8 *msg, u32 size)
{
    if (aid > AWC_MAX_AID) return;
    member_t *m = &members[aid];
    u8 cmd = msg[1];
    if ((cmd == CHT_LOGIN || cmd == CHT_USERINFO) && size >= CHT_USERINFO_SIZE) {
        read_profile(m, msg);
        if (!m->joined) {
            m->joined = 1;
            ev_simple(EV_PLAYER_JOINED, m->name, m->color, NULL);
        }
        return;
    }
    if (cmd != CHT_PICTURE || size <= CHT_PIC_HEADER) return;
    int rows = (size - CHT_PIC_HEADER) / 1024 * 8;
    if (rows < 8) return;
    if (rows > DSMSG_MAX_ROWS) rows = DSMSG_MAX_ROWS;
    u8 *pixels = calloc(DSMSG_W, rows < 16 ? 16 : rows);
    if (!pixels) return;
    dsmsg_tiles_to_pixels(msg + CHT_PIC_HEADER, rows, pixels);
    chat_event *ev = ev_begin(EV_MESSAGE);
    if (!ev) {
        free(pixels);
        return;
    }
    snprintf(ev->msg.name, sizeof ev->msg.name, "%s", m->name[0] ? m->name : "DS");
    ev->msg.color = m->joined ? m->color : DS_COLORS[0];
    ev->msg.lines = (rows + 15) / 16;
    ev->msg.pixels = pixels;
    ev->msg.pixel_rows = rows;
    ev_end();
}

static void on_left(u16 aid)
{
    if (aid > AWC_MAX_AID) return;
    member_t *m = &members[aid];
    if (m->joined) ev_simple(EV_PLAYER_LEFT, m->name, m->color, NULL);
    memset(m, 0, sizeof *m);
}

/* ── WD threads ──────────────────────────────────────────────────────────── */

static void mp_send(u8 *buf, u32 len, u8 *conf)
{
    WD_ChangeVTSF((u16)(ticks_to_microsecs(gettick()) / 64));
    WD_MpSendFrame(buf, len, conf, 0x10);
}

static void *beacon_main(void *arg)
{
    WD_ChangeBeacon((u16)(ticks_to_microsecs(gettick()) / 64), (u8 *)&beacon, 0x20);
    WD_SetLinkState(1);
    for (int i = 0; i < 500 && WD_GetLinkState() == 0; i++) usleep(1000);
    while (running) {
        /* a DS picks a pseudo parent (nobody here yet) or a real one from this */
        u8 children = awc_child_count();
        beacon.users_connected = 1 + children;
        beacon.status = children ? AWC_PARENTINFO_PARENT : AWC_PARENTINFO_PSEUDO;
        WD_ChangeBeacon((u16)(ticks_to_microsecs(gettick()) / 64), (u8 *)&beacon, 0x20);
        usleep(200 * 1000);
    }
    WD_SetLinkState(0);
    for (int i = 0; i < 500 && WD_GetLinkState() == 1; i++) usleep(1000);
    return arg;
}

static void *recv_main(void *arg)
{
    while (running) {
        s32 len = WD_ReceiveFrame(recv_buf, sizeof recv_buf);
        if (!running) break;
        if (len < 0) {
            usleep(1000);
            continue;
        }
        awc_on_frame(recv_buf, len);
    }
    threads_alive--;
    return arg;
}

static void *notify_main(void *arg)
{
    u8 note[0x94] __attribute__((aligned(32)));
    while (running) {
        memset(note, 0, sizeof note);
        s32 ret = WD_RecvNotification(note, sizeof note);
        if (!running) break;
        if (ret != 0) {
            usleep(1000);
            continue;
        }
        s32 type;
        memcpy(&type, note, 4);
        u16 aid = note[0xE] << 8 | note[0xF];
        if (type == 0 && aid) awc_on_connect(aid, note + 8);
        else if (type == 1 && aid) awc_on_disconnect(aid);
    }
    threads_alive--;
    return arg;
}

static void *send_main(void *arg)
{
    memset(send_conf, 0, sizeof send_conf);
    send_conf[6] = CHILD_REPLY_SIZE >> 8;
    send_conf[7] = CHILD_REPLY_SIZE & 0xFF;
    while (running) {
        u16 poll = 0;
        u32 len = awc_next_frame(send_buf, &poll);
        if (len) {
            send_conf[8] = poll >> 8;
            send_conf[9] = poll & 0xFF;
            mp_send(send_buf, len, send_conf);
        }
        usleep(SEND_INTERVAL_US);
    }
    return arg;
}

/* ── Starting and stopping ───────────────────────────────────────────────── */

static void utf16(u16 *out, int n, const char *s)
{
    memset(out, 0, n * sizeof *out);
    for (int i = 0; i < n && s && s[i]; i++) out[i] = (u8)s[i];
}

int ds_room_start(int room, const char *name, int color_index, char *why, int why_len)
{
    static const u8 channels[4] = { 1, 7, 13, 7 };
    if (active) return 0;
    if (threads_alive) {
        snprintf(why, why_len, "The last DS room is still closing. Please restart PictoChat.");
        return -1;
    }
    if (ev_lock == LWP_MUTEX_NULL) LWP_MutexInit(&ev_lock, false);
    ev_rd = ev_wr = 0;
    memset(members, 0, sizeof members);
    used = 1;

    /* KD would keep the chip busy with WiiConnect24 */
    u32 out = 0;
    s32 kd = IOS_Open("/dev/net/kd/request", 0);
    if (kd >= 0) {
        IOS_Ioctl(kd, 1, NULL, 0, &out, 4);
        IOS_Close(kd);
    }
    /* the network (a cancelled update check, say) can hold the chip for a moment */
    lock_id = NCD_LockWirelessDriver();
    for (int i = 0; i < 50 && lock_id < 0; i++) {
        usleep(100 * 1000);
        lock_id = NCD_LockWirelessDriver();
    }
    if (lock_id < 0) {
        snprintf(why, why_len, "The Wii's wireless chip is busy (%d).", (int)lock_id);
        return -1;
    }
    s32 ret = WD_Init(DSCommunications);
    if (ret < 0) {
        NCD_UnlockWirelessDriver(lock_id);
        snprintf(why, why_len, "The DS wireless mode didn't start (%d).", (int)ret);
        return -1;
    }

    WDInfo info __attribute__((aligned(32)));
    memset(&info, 0, sizeof info);
    WD_GetInfo(&info);
    awc_callbacks cb = { on_message, on_left };
    awc_init(info.MAC, &cb);
    u16 uname[10], comment[26];
    utf16(uname, 10, name);
    utf16(comment, 26, "");
    awc_set_profile(uname, comment, color_index & 0xF);
    char path[160];
    seq_path(path, sizeof path);
    awc_load_seq(path);

    WD_Config conf __attribute__((aligned(32)));
    memset(&conf, 0, sizeof conf);
    conf.mpParent.connectionTimeout = 4;
    conf.mpParent.beaconPeriod = 0xC8;
    conf.mpParent.maxNodes = 0xF;
    conf.mpParent.channel = channels[room & 3];
    u64 mask __attribute__((aligned(32))) = 0x0003007F00000000ULL;
    WD_SetConfig(&conf, mask);

    room_index = room & 3;
    memset(&beacon, 0, sizeof beacon);
    beacon.header = 0x01000108;
    beacon.id_2 = 0xc000c000;
    beacon.unk0 = 0x0801;
    beacon.status = AWC_PARENTINFO_PSEUDO;
    beacon.current_room = room_index;
    beacon.users_connected = 1;
    beacon.unk1 = 0x0400;
    u16 r = gettick();
    beacon.random_id = (r >> 8) | (r & 0xFF);

    running = 1;
    threads_alive = 2;
    LWP_CreateThread(&beacon_thread, beacon_main, NULL, beacon_stack, STACKSIZE, 0x40);
    LWP_CreateThread(&notify_thread, notify_main, NULL, notify_stack, STACKSIZE, 0x50);
    LWP_CreateThread(&recv_thread, recv_main, NULL, recv_stack, STACKSIZE, 0x50);
    LWP_CreateThread(&send_thread, send_main, NULL, send_stack, STACKSIZE, 0x50);
    active = 1;

    chat_event *ev = ev_begin(EV_ROOM_DATA);
    if (ev) {
        snprintf(ev->room, sizeof ev->room, "room_%c", 'a' + room_index);
        ev_end();
    }
    return 0;
}

void ds_room_stop(void)
{
    if (!active) return;
    active = 0;
    char path[160];
    seq_path(path, sizeof path);
    awc_save_seq(path);
    running = 0;
    LWP_JoinThread(send_thread, NULL);

    /* wake the receive thread by sending something to ourselves */
    memset(send_conf, 0, sizeof send_conf);
    send_conf[7] = 0xC;
    memset(send_buf, 0, 4);
    mp_send(send_buf, 4, send_conf);

    LWP_JoinThread(beacon_thread, NULL);
    usleep(100 * 1000);
    /* the receive and notification threads may still sit in an ioctl:
     * closing WD releases them */
    WD_Deinit();
    NCD_UnlockWirelessDriver(lock_id);
    lock_id = -1;

    /* whatever the UI didn't take yet */
    chat_event ev;
    while (ds_room_poll(&ev)) pc_msg_free(&ev.msg);
}

int ds_room_active(void) { return active; }
int ds_room_used(void) { return used; }

void ds_room_send(const pc_msg *msg)
{
    static u8 pixels[DSMSG_W * DSMSG_MAX_ROWS];
    static u8 tiles[DSMSG_W * DSMSG_MAX_ROWS / 2];
    if (!active) return;
    /* Rendered onto a blank canvas, then cut below the last row with
     * anything on it, rounded up to a 16-row line as a DS sends its own */
    memset(pixels, 0, sizeof pixels);
    dsmsg_render(msg, pixels);
    int rows = DSMSG_MAX_ROWS;
    while (rows > 0) {
        const u8 *row = pixels + (rows - 1) * DSMSG_W;
        int c = 0;
        while (c < DSMSG_W && !row[c]) c++;
        if (c < DSMSG_W) break;
        rows--;
    }
    rows = (rows + 15) / 16 * 16;
    if (rows < 16) rows = 16;
    dsmsg_pixels_to_tiles(pixels, rows, tiles);
    awc_send_picture(tiles, rows);
}
