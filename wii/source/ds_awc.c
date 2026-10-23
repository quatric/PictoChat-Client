/* The parent side of AWC for DS PictoChat. Ported from PiictoChat by
 * Abdelali221 (github.com/abdelali221/PiictoChat); the protocol is the
 * AWC/CHT layer of DS PictoChat as NitroSDK's WM ports carry it.
 *
 * A DS child only counts itself as a member (and only enables SEND) once a
 * CMD_INFO listing the room has reached it. After that, every message is
 * arbitrated by the parent: the child asks for the transport on the
 * negotiation port, the parent allows it, re-broadcasts each chunk the child
 * sends on the transport port, and the child closes the transfer with
 * DONE_TRANSPORT once its own last chunk came back.
 *
 * Every port packet inside an MP frame is laid out as
 *     u16 header   (DS order: low byte = length in halfwords,
 *                   high byte = 0x10 | port)
 *     data
 *     u16 seq      (ports 8-15 only, the sequenced ports)
 *     u16 bitmap   (parent only: the children the packet is meant for)
 * The seq word is a 15 bit counter plus WM_HEADER_SEQ_RETRY (0x8000) on a
 * repeated copy of the same packet. */
#include <stdio.h>
#include <string.h>
#include <gccore.h>
#include <ogc/lwp_watchdog.h>
#include "ds_awc.h"

#define CONNECTION_CMD_SIZE 8
#define MACLIST_SIZE        ((AWC_MAX_AID + 1) * 6)
#define BROADCAST_CMD_SIZE  20
#define TRANSPORT_CMD_SIZE  12

#define PKT_MAX             0xC0   /* parent MP payload size advertised in the beacon */
#define PKT_REPEAT          3      /* copies of each port packet, same seq */
#define RELAY_REPEAT        1      /* copies of a relayed body chunk: the sender repeats it if it misses one */
#define QUEUE_LEN           64
#define REQ_FIFO_LEN        16
#define KEEPALIVE_MS        50
#define MSG_MAX             (16 * 1024)
#define SEQ_RETRY           0x8000
#define SEQ_MARGIN          512    /* packets we may have sent after the last save */
#define SEQ_FIRST_RUN       0x1800

#define ECHO_RESEND_MS      150    /* resend the last relayed chunk until its sender says DONE */
#define ECHO_RESEND_MAX     10
#define ALLOW_RESEND_MS     100    /* a granted child starts sending within ~20 ms */
#define ALLOW_RESEND_MAX    8
#define GRANT_STALL_MS      3000
#define GRANT_RETRIES       3

#define SEND_CHUNK          160    /* AWC_BROADCAST_SEND_SIZE */
#define OWN_PASSES          4      /* nothing acks a parent's chunks: send the body several times */
#define OWN_GAP_MS          10     /* a DS Lite loses chunks sent back to back */
#define OWN_QUEUE           4      /* own messages waiting */

enum { CMD_GET_TRANSPORT, CMD_ALLOW_TRANSPORT, CMD_SET_DATA, CMD_DONE_TRANSPORT, CMD_INFO, CMD_KEEP_ALIVE, CMD_MACADDRLIST };

typedef struct {
    u8 port;
    u8 len;
    u16 bitmap;
    u16 seq;
    u8 repeat;
    u8 data[PKT_MAX];
} port_packet;

static mutex_t mtx = LWP_MUTEX_NULL;
static awc_callbacks cbs;

static u8 mac_list[AWC_MAX_AID + 1][6];  /* AWCMacAddr layout, index 0 = us */
static u16 child_bitmap;
static u8 child_count;

static port_packet queue[QUEUE_LEN];
static u32 queue_rd, queue_wr;
static port_packet current;
static int current_left;
static u16 port_seq[16];
static u16 peer_seq[16];                 /* highest seq a child used on each port */
static u32 last_rx_seq[AWC_MAX_AID + 1][16];
static u64 last_keepalive;
static u32 rnd = 0x1234567;

/* transport arbitration */
static u8 req_fifo[REQ_FIFO_LEN][BROADCAST_CMD_SIZE];
static u32 req_rd, req_wr;
static int forwarding;
static u16 trans_aid;
static u32 trans_size;
static s32 recv_offset;
static u8 msgbuf[MSG_MAX];
static u64 last_data_time;
static u8 grant_retries[AWC_MAX_AID + 1];

static u8 allow_cmd[BROADCAST_CMD_SIZE]; /* the ALLOW we last sent, until the child's data starts */
static u32 allow_tries;
static u64 allow_time;

static u8 echo_buf[PKT_MAX];             /* the last chunk of a transfer, until DONE */
static u32 echo_len, echo_tries;
static u64 echo_time;

static u8 last_relay[PKT_MAX];           /* echoed again when the sender repeats it */
static u32 last_relay_len;
static u64 last_relay_time;

/* our own membership: a host is a chat member too */
static u8 login_allow[BROADCAST_CMD_SIZE];
static u8 own_info[CHT_USERINFO_SIZE];
static u8 cht_version = 3;
static int own_info_pending;

/* our own messages */
static u8 own_queue[OWN_QUEUE][CHT_PIC_MAX];
static u32 own_queue_size[OWN_QUEUE];
static u32 own_rd, own_wr;
static int own_sending;
static u32 own_step;
static u64 own_last_chunk;

static inline void put16(u8 *p, u16 v) { p[0] = v; p[1] = v >> 8; }
static inline void put32(u8 *p, u32 v) { p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; }
static inline u16 get16(const u8 *p) { return p[0] | (p[1] << 8); }
static inline u32 get32(const u8 *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24); }
static inline u32 ms_since(u64 t) { return ticks_to_millisecs(gettime() - t); }

static u32 next_rnd(void)
{
    rnd = rnd * 1592653589UL + 453816691UL;
    return rnd;
}

static void set_mac(u16 aid, const u8 *mac)
{
    if (!mac) {
        memset(mac_list[aid], 0, 6);
        return;
    }
    for (int i = 0; i < 3; i++) {
        mac_list[aid][i * 2] = mac[i * 2 + 1];
        mac_list[aid][i * 2 + 1] = mac[i * 2];
    }
}

/* ── Packet queue ────────────────────────────────────────────────────────── */

static void enqueue_n(u8 port, const u8 *data, u32 len, u8 repeat)
{
    if (len > PKT_MAX || queue_wr - queue_rd >= QUEUE_LEN) return;
    port_packet *p = &queue[queue_wr % QUEUE_LEN];
    p->port = port;
    p->len = len;
    p->bitmap = 0xFFFF;
    p->repeat = repeat;
    /* A DS keeps one counter per port for both directions and drops anything
     * that doesn't move past what it last saw, so move past the children too. */
    u16 *seq = &port_seq[port & 0xF];
    if ((u16)(peer_seq[port & 0xF] - *seq) < 0x4000) *seq = peer_seq[port & 0xF];
    p->seq = ++*seq & 0x7FFF;
    memcpy(p->data, data, len);
    queue_wr++;
}

static void enqueue(u8 port, const u8 *data, u32 len)
{
    enqueue_n(port, data, len, PKT_REPEAT);
}

/* CMD_INFO also serves as the keepalive: a DS only becomes a member once one
 * gets through, and the few sent when it connects can all be missed while it
 * syncs its port 12 counter. */
static void send_connection_cmd(u16 cmd_id)
{
    u8 buf[CONNECTION_CMD_SIZE + MACLIST_SIZE];
    put16(buf, cmd_id);
    put16(buf + 2, sizeof buf);
    put32(buf + 4, next_rnd());
    memcpy(buf + CONNECTION_CMD_SIZE, mac_list, MACLIST_SIZE);
    enqueue(AWC_PORT_SESSION, buf, sizeof buf);
}

/* The first packet a child sees on a sequenced port only syncs its counter,
 * so each new child gets a throwaway packet the DS ignores on both. */
static void prime_seq_ports(void)
{
    u8 buf[BROADCAST_CMD_SIZE] = { 0 };
    put16(buf, CMD_MACADDRLIST + 1);
    put16(buf + 2, BROADCAST_CMD_SIZE);
    enqueue(AWC_PORT_NEGOTIATION, buf, BROADCAST_CMD_SIZE);
    memset(buf, 0, sizeof buf);
    put16(buf, CMD_SET_DATA);
    put16(buf + 2, TRANSPORT_CMD_SIZE);
    enqueue(AWC_PORT_TRANSPORT, buf, TRANSPORT_CMD_SIZE);
}

/* ── Our own USERINFO ────────────────────────────────────────────────────── */

void awc_set_profile(const u16 *name, const u16 *comment, u8 color)
{
    LWP_MutexLock(mtx);
    memset(own_info, 0, sizeof own_info);
    for (int i = 0; i < 10; i++) put16(own_info + 8 + i * 2, name[i]);
    for (int i = 0; i < 26; i++) put16(own_info + 28 + i * 2, comment[i]);
    put16(own_info + 80, color & 0xF);
    LWP_MutexUnlock(mtx);
}

/* An ALLOW announcing a transfer of ours (aid 0), built like a child's request */
static void announce_own(u32 size)
{
    u8 cmd[BROADCAST_CMD_SIZE] = { 0 };
    if (get16(login_allow + 2) == BROADCAST_CMD_SIZE) memcpy(cmd, login_allow, sizeof cmd);
    put16(cmd, CMD_ALLOW_TRANSPORT);
    put16(cmd + 2, BROADCAST_CMD_SIZE);
    cmd[4] = 0;
    put16(cmd + 6, 0xFFFF);
    put32(cmd + 8, size);
    cmd[12] = 1;
    put32(cmd + 16, next_rnd());
    enqueue(AWC_PORT_NEGOTIATION, cmd, BROADCAST_CMD_SIZE);
}

/* Answers a newcomer's LOGIN (and its USERINFO, in case the first answer
 * got lost in between) the way every member does. Children don't DONE a
 * parent's transfer. */
static void send_own_userinfo(void)
{
    own_info[0] = cht_version;
    own_info[1] = CHT_USERINFO;
    memcpy(own_info + 2, mac_list[0], 6);
    announce_own(CHT_USERINFO_SIZE);
    u8 pkt[TRANSPORT_CMD_SIZE + CHT_USERINFO_SIZE] = { 0 };
    put16(pkt, CMD_SET_DATA);
    put16(pkt + 2, sizeof pkt);
    pkt[6] = CHT_USERINFO_SIZE;
    pkt[7] = 1;
    memcpy(pkt + TRANSPORT_CMD_SIZE, own_info, CHT_USERINFO_SIZE);
    enqueue(AWC_PORT_TRANSPORT, pkt, sizeof pkt);
    own_info_pending = 0;
}

/* ── Our own messages ────────────────────────────────────────────────────── */

int awc_send_picture(const u8 *tiles, int rows)
{
    static const u8 fixed[14] = { 0x00, 0x05, 0x00, 0x00, 0x00, 0x00, 0x03, 0x06, 0x08, 0x0d, 0x08, 0x0d, 0x12, 0x1b };
    if (rows < 8 || rows > 80) return -1;
    u32 size = CHT_PIC_HEADER + (u32)rows / 8 * 1024;
    LWP_MutexLock(mtx);
    if (own_wr - own_rd >= OWN_QUEUE) {
        LWP_MutexUnlock(mtx);
        return -1;
    }
    u8 *m = own_queue[own_wr % OWN_QUEUE];
    memset(m, 0, CHT_PIC_HEADER);
    m[0] = cht_version;
    m[1] = CHT_PICTURE;
    memcpy(m + 2, mac_list[0], 6);
    memcpy(m + 8, fixed, sizeof fixed);
    memcpy(m + CHT_PIC_HEADER, tiles, size - CHT_PIC_HEADER);
    own_queue_size[own_wr % OWN_QUEUE] = size;
    own_wr++;
    LWP_MutexUnlock(mtx);
    return 0;
}

static void start_own_message(void)
{
    /* a child drops pictures from senders it doesn't list as members */
    send_own_userinfo();
    announce_own(own_queue_size[own_rd % OWN_QUEUE]);
    announce_own(own_queue_size[own_rd % OWN_QUEUE]); /* a child that misses it drops the whole message */
    forwarding = 1;
    trans_aid = 0;
    recv_offset = 0;   /* no ALLOW resends for our own transfer */
    own_sending = 1;
    own_step = 0;
}

static void allow_next_transport(void);

/* One chunk at a time, paced. The body goes out OWN_PASSES times, each chunk
 * as a new packet (a child just rewrites the same offset), then DATA_END
 * twice: a child shows the message with whatever arrived when DATA_END does,
 * and ignores the second one. */
static void feed_own_message(void)
{
    if (!own_sending || queue_wr - queue_rd >= 1 || ms_since(own_last_chunk) < OWN_GAP_MS) return;
    own_last_chunk = gettime();
    const u8 *m = own_queue[own_rd % OWN_QUEUE];
    u32 size = own_queue_size[own_rd % OWN_QUEUE];
    u32 body = (size - 1) / SEND_CHUNK;
    u32 step = own_step++;
    u32 offset = step < body * OWN_PASSES ? (step % body) * SEND_CHUNK : body * SEND_CHUNK;
    u32 n = size - offset > SEND_CHUNK ? SEND_CHUNK : size - offset;
    int last = offset + n == size;
    u8 pkt[TRANSPORT_CMD_SIZE + SEND_CHUNK] = { 0 };
    put16(pkt, CMD_SET_DATA);
    put16(pkt + 2, TRANSPORT_CMD_SIZE + n);
    pkt[6] = n;
    pkt[7] = last;
    put32(pkt + 8, offset);
    memcpy(pkt + TRANSPORT_CMD_SIZE, m + offset, n);
    enqueue_n(AWC_PORT_TRANSPORT, pkt, TRANSPORT_CMD_SIZE + n, last ? PKT_REPEAT : 2);
    if (step == body * OWN_PASSES + 1) {
        own_sending = 0;
        own_rd++;
        forwarding = 0;
        trans_aid = 0xFFFF;
        recv_offset = -1;
        allow_next_transport();
    }
}

/* ── Arbitration ─────────────────────────────────────────────────────────── */

static void allow_next_transport(void)
{
    while (!forwarding && req_rd != req_wr) {
        u8 *cmd = req_fifo[req_rd % REQ_FIFO_LEN];
        req_rd++;
        u8 aid = cmd[4];
        if (aid > AWC_MAX_AID || !(child_bitmap & (1 << aid))) continue;
        forwarding = 1;
        trans_aid = aid;
        trans_size = get32(cmd + 8);
        recv_offset = -1;
        last_relay_len = 0;
        put16(cmd, CMD_ALLOW_TRANSPORT);
        enqueue(AWC_PORT_NEGOTIATION, cmd, BROADCAST_CMD_SIZE);
        memcpy(allow_cmd, cmd, BROADCAST_CMD_SIZE);
        if (trans_size == CHT_USERINFO_SIZE) memcpy(login_allow, cmd, BROADCAST_CMD_SIZE);
        allow_tries = 0;
        allow_time = gettime();
    }
}

/* Takes the transport back from a child that isn't using it. One that never
 * started (it missed every ALLOW) still waits for one, so its request goes to
 * the back of the queue and gets a fresh ALLOW later. */
static void drop_grant(int requeue)
{
    u16 aid = trans_aid;
    forwarding = 0;
    trans_aid = 0xFFFF;
    recv_offset = -1;
    echo_len = 0;
    if (requeue && aid <= AWC_MAX_AID && grant_retries[aid]++ < GRANT_RETRIES && req_wr - req_rd < REQ_FIFO_LEN) {
        put16(allow_cmd, CMD_GET_TRANSPORT);
        memcpy(req_fifo[req_wr % REQ_FIFO_LEN], allow_cmd, BROADCAST_CMD_SIZE);
        req_wr++;
    }
    allow_next_transport();
    if (own_info_pending && !forwarding) send_own_userinfo();
}

static void on_session(const u8 *data)
{
    if (get16(data) == CMD_MACADDRLIST) send_connection_cmd(CMD_INFO);
}

static void on_negotiation(u16 aid, const u8 *data, u32 len)
{
    if (len < BROADCAST_CMD_SIZE) return;
    switch (get16(data)) {
    case CMD_GET_TRANSPORT:
        if (req_wr - req_rd >= REQ_FIFO_LEN) break;
        memcpy(req_fifo[req_wr % REQ_FIFO_LEN], data, BROADCAST_CMD_SIZE);
        req_wr++;
        allow_next_transport();
        break;
    case CMD_DONE_TRANSPORT:
        if (forwarding && trans_aid == aid) {
            if (data[12] && trans_size >= 8 && trans_size <= MSG_MAX) {
                u8 cmd = msgbuf[1];
                if (cmd == CHT_LOGIN) cht_version = msgbuf[0];
                /* introduce ourselves after the newcomer's LOGIN, and again once
                 * its own USERINFO is through: one squeezed between can get lost */
                if (trans_size == CHT_USERINFO_SIZE && (cmd == CHT_LOGIN || cmd == CHT_USERINFO)) own_info_pending = 1;
                if (cbs.message) cbs.message(aid, msgbuf, trans_size);
            }
            forwarding = 0;
            trans_aid = 0xFFFF;
            recv_offset = -1;
        }
        echo_len = 0;
        allow_next_transport();
        if (own_info_pending && !forwarding) send_own_userinfo();
        break;
    }
}

static void enqueue_relay(const u8 *data, u32 len, u8 repeat)
{
    enqueue_n(AWC_PORT_TRANSPORT, data, len, repeat);
}

static void on_transport(u16 aid, const u8 *data, u32 len)
{
    if (len < TRANSPORT_CMD_SIZE || !forwarding || aid != trans_aid) return;
    u8 chunk = data[6], state = data[7];
    u32 offset = get32(data + 8);
    if ((s32)offset == recv_offset) return;
    recv_offset = offset;
    last_data_time = gettime();
    grant_retries[aid] = 0;
    if (offset + chunk <= MSG_MAX && (u32)(TRANSPORT_CMD_SIZE + chunk) <= len)
        memcpy(msgbuf + offset, data + TRANSPORT_CMD_SIZE, chunk);
    /* re-broadcast the chunk untouched: the sender waits for its own DATA_END */
    u32 n = TRANSPORT_CMD_SIZE + chunk;
    if (n > PKT_MAX) return;
    enqueue_relay(data, n, state ? PKT_REPEAT : RELAY_REPEAT);
    memcpy(last_relay, data, n);
    last_relay_len = n;
    last_relay_time = gettime();
    if (state) {
        echo_len = n;
        memcpy(echo_buf, data, n);
        echo_tries = 0;
        echo_time = gettime();
    }
}

/* ── Children ────────────────────────────────────────────────────────────── */

void awc_on_connect(u16 aid, const u8 *mac)
{
    if (aid == 0 || aid > AWC_MAX_AID) return;
    LWP_MutexLock(mtx);
    if (!(child_bitmap & (1 << aid))) child_count++;
    child_bitmap |= 1 << aid;
    set_mac(aid, mac);
    memset(last_rx_seq[aid], 0xFF, sizeof last_rx_seq[aid]);
    grant_retries[aid] = 0;
    prime_seq_ports();
    send_connection_cmd(CMD_INFO);
    LWP_MutexUnlock(mtx);
}

void awc_on_disconnect(u16 aid)
{
    if (aid == 0 || aid > AWC_MAX_AID) return;
    LWP_MutexLock(mtx);
    int was = child_bitmap & (1 << aid);
    if (was) child_count--;
    child_bitmap &= ~(1 << aid);
    if (forwarding && trans_aid == aid) {
        forwarding = 0;
        trans_aid = 0xFFFF;
        recv_offset = -1;
        allow_next_transport();
    }
    if (child_bitmap) send_connection_cmd(CMD_INFO);
    if (was && cbs.left) cbs.left(aid);
    set_mac(aid, NULL);
    LWP_MutexUnlock(mtx);
}

/* One child's reply. 'frame' is laid out as if this record were the first:
 * record header (length, rate/rssi, aid, noResponse) at 0xA, the aid at 0xE,
 * the WM header at 0x12, data from 0x14. */
static void on_child_record(const u8 *frame, s32 len)
{
    if (len < 0x14) return;
    u16 aid = (frame[0xE] << 8) | frame[0xF];
    u8 lenhw = frame[0x12];
    u8 port = frame[0x13] & 0xF;
    u32 dlen = lenhw * 2;
    if (!lenhw || aid == 0 || aid > AWC_MAX_AID || 0x14 + dlen > (u32)len) return;
    const u8 *data = frame + 0x14;
    LWP_MutexLock(mtx);
    if (port >= 8) {
        u16 seq = (0x14 + dlen + 2 <= (u32)len) ? get16(data + dlen) : 0;
        if (last_rx_seq[aid][port] == seq) {
            /* the DS repeats a packet until it's acked; a sender repeating a
             * chunk missed our echo, so echo it again */
            if (port == AWC_PORT_TRANSPORT && forwarding && trans_aid == aid && last_relay_len &&
                dlen >= TRANSPORT_CMD_SIZE && get32(data + 8) == get32(last_relay + 8) &&
                ms_since(last_relay_time) >= 10) {
                enqueue_relay(last_relay, last_relay_len, RELAY_REPEAT);
                last_relay_time = gettime();
            }
            LWP_MutexUnlock(mtx);
            return;
        }
        last_rx_seq[aid][port] = seq;
        if ((u16)((seq & 0x7FFF) - peer_seq[port]) < 0x4000) peer_seq[port] = seq & 0x7FFF;
    }
    switch (port) {
    case AWC_PORT_SESSION: on_session(data); break;
    case AWC_PORT_NEGOTIATION: on_negotiation(aid, data, dlen); break;
    case AWC_PORT_TRANSPORT: on_transport(aid, data, dlen); break;
    }
    LWP_MutexUnlock(mtx);
}

/* WD hands over all replies to one MP frame together: a 10-byte header (bitmap
 * of children that didn't answer, timestamp, record count, record stride =
 * reply slot + 10, ?) and then one fixed-size record per child. */
void awc_on_frame(const u8 *frame, s32 len)
{
    if (len < 0x14) return;
    u16 count = (frame[4] << 8) | frame[5];
    u16 stride = (frame[6] << 8) | frame[7];
    if (count <= 1 || stride < 0xA) {
        on_child_record(frame, len);
        return;
    }
    for (u32 k = 0; k < count && (s32)(k * stride + 0x14) <= len; k++)
        on_child_record(frame + k * stride, len - k * stride);
}

/* ── MP frames ───────────────────────────────────────────────────────────── */

static u32 build_port_frame(u8 *buf, const port_packet *p, u16 bitmap, int retry)
{
    u8 lenhw = (p->len + 1) / 2;
    buf[0] = 0x10 | p->port;
    buf[1] = lenhw;
    memcpy(buf + 2, p->data, p->len);
    if (p->len & 1) buf[2 + p->len] = 0;
    u32 off = 2 + lenhw * 2;
    if (p->port >= 8) {
        put16(buf + off, (p->seq & 0x7FFF) | (retry ? SEQ_RETRY : 0));
        off += 2;
    }
    put16(buf + off, bitmap);
    return off + 2;
}

u32 awc_next_frame(u8 *buf, u16 *pollbitmap)
{
    LWP_MutexLock(mtx);
    u16 bitmap = child_bitmap;
    *pollbitmap = bitmap;
    if (!bitmap) {
        LWP_MutexUnlock(mtx);
        return 0;
    }

    /* a child that missed its ALLOW waits for one forever: send it again as a
     * new packet until the data starts, then take the grant back */
    if (forwarding && recv_offset == -1 && ms_since(allow_time) >= ALLOW_RESEND_MS) {
        if (allow_tries++ < ALLOW_RESEND_MAX) enqueue(AWC_PORT_NEGOTIATION, allow_cmd, BROADCAST_CMD_SIZE);
        else drop_grant(1);
        allow_time = gettime();
    }
    /* a transfer that stops partway would hold the room forever */
    if (forwarding && trans_aid != 0 && recv_offset != -1 && ms_since(last_data_time) >= GRANT_STALL_MS)
        drop_grant(0);
    if (echo_len && forwarding && ms_since(echo_time) >= ECHO_RESEND_MS) {
        if (echo_tries++ < ECHO_RESEND_MAX) enqueue(AWC_PORT_TRANSPORT, echo_buf, echo_len);
        else echo_len = 0;
        echo_time = gettime();
    }

    if (!own_sending && own_rd != own_wr && !forwarding && req_rd == req_wr && !own_info_pending) start_own_message();
    feed_own_message();

    if (!current_left) {
        if (queue_rd == queue_wr && ms_since(last_keepalive) >= KEEPALIVE_MS) {
            send_connection_cmd(CMD_INFO);
            last_keepalive = gettime();
        }
        if (queue_rd != queue_wr) {
            current = queue[queue_rd % QUEUE_LEN];
            queue_rd++;
            current_left = current.repeat;
        }
    }

    u32 len;
    if (current_left) {
        int retry = current_left < current.repeat;
        current_left--;
        len = build_port_frame(buf, &current, current.bitmap & bitmap, retry);
    } else {
        /* nothing to say, just poll the children so they can talk */
        memset(buf, 0, 4);
        len = 4;
    }
    LWP_MutexUnlock(mtx);
    return len;
}

/* ── Setup ───────────────────────────────────────────────────────────────── */

void awc_init(const u8 *own_mac, const awc_callbacks *cb)
{
    if (mtx == LWP_MUTEX_NULL) LWP_MutexInit(&mtx, false);
    cbs = *cb;
    memset(mac_list, 0, sizeof mac_list);
    set_mac(0, own_mac);
    child_bitmap = 0;
    child_count = 0;
    queue_rd = queue_wr = 0;
    current_left = 0;
    memset(last_rx_seq, 0xFF, sizeof last_rx_seq);
    memset(peer_seq, 0, sizeof peer_seq);
    memset(login_allow, 0, sizeof login_allow);
    echo_len = 0;
    req_rd = req_wr = 0;
    forwarding = 0;
    trans_aid = 0xFFFF;
    recv_offset = -1;
    own_rd = own_wr = 0;
    own_sending = 0;
    own_info_pending = 0;
    cht_version = 3;
    rnd ^= gettick();
    last_keepalive = gettime();
}

void awc_child_mac(u16 aid, u8 *mac_out)
{
    LWP_MutexLock(mtx);
    memcpy(mac_out, mac_list[aid <= AWC_MAX_AID ? aid : 0], 6);
    LWP_MutexUnlock(mtx);
}

void awc_own_mac(u8 *mac_out)
{
    awc_child_mac(0, mac_out);
}

u8 awc_child_count(void)
{
    return child_count;
}

void awc_load_seq(const char *path)
{
    memset(port_seq, 0, sizeof port_seq);
    u16 saved[16];
    FILE *f = fopen(path, "rb");
    if (f && fread(saved, sizeof saved, 1, f) == 1) {
        for (int i = 8; i < 16; i++) port_seq[i] = (saved[i] + SEQ_MARGIN) & 0x7FFF;
    } else {
        /* a DS still in the room may remember an earlier session's counters */
        for (int i = 8; i < 16; i++) port_seq[i] = SEQ_FIRST_RUN;
    }
    if (f) fclose(f);
}

void awc_save_seq(const char *path)
{
    u16 saved[16];
    LWP_MutexLock(mtx);
    memcpy(saved, port_seq, sizeof saved);
    LWP_MutexUnlock(mtx);
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fwrite(saved, sizeof saved, 1, f);
    fclose(f);
}
