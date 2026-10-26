#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "platform.h"
#include "ws.h"
#include "net.h"
#include "util.h"
#include "version.h"

#define MAX_MESSAGE (1024 * 1024)

typedef struct msg {
    struct msg *next;
    size_t len;
    unsigned char opcode;
    char data[];
} msg_t;

static pthread_t thread;
static int thread_running;
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static int lock_ready;
static volatile int running;
static volatile ws_state_t state = WS_IDLE;
static char error[192];
static char url[256], origin[128];
static msg_t *out_head, *out_tail, *in_head, *in_tail;

static void push(msg_t **head, msg_t **tail, msg_t *m)
{
    m->next = NULL;
    pthread_mutex_lock(&lock);
    if (*tail) (*tail)->next = m;
    else *head = m;
    *tail = m;
    pthread_mutex_unlock(&lock);
}

static msg_t *pop(msg_t **head, msg_t **tail)
{
    pthread_mutex_lock(&lock);
    msg_t *m = *head;
    if (m) {
        *head = m->next;
        if (!*head) *tail = NULL;
    }
    pthread_mutex_unlock(&lock);
    return m;
}

static void drain(msg_t **head, msg_t **tail)
{
    msg_t *m;
    while ((m = pop(head, tail))) free(m);
}

static msg_t *msg_new(unsigned char opcode, const void *data, size_t len)
{
    msg_t *m = malloc(sizeof(msg_t) + len + 1);
    if (!m) return NULL;
    m->opcode = opcode;
    m->len = len;
    if (len) memcpy(m->data, data, len);
    m->data[len] = 0;
    return m;
}

static void set_error(const char *text)
{
    if (!error[0]) snprintf(error, sizeof error, "%s", text);
}

/* Client frames are always masked. */
static int send_frame(net_conn *c, unsigned char opcode, const char *data, size_t len)
{
    unsigned char head[14];
    size_t n = 0;
    head[n++] = 0x80 | opcode;
    if (len < 126) {
        head[n++] = 0x80 | len;
    } else if (len < 65536) {
        head[n++] = 0x80 | 126;
        head[n++] = len >> 8;
        head[n++] = len & 255;
    } else {
        head[n++] = 0x80 | 127;
        for (int i = 7; i >= 0; i--) head[n++] = i >= 4 ? 0 : (len >> (i * 8)) & 255;
    }
    unsigned char mask[4];
    for (int i = 0; i < 4; i++) mask[i] = rand() & 255;
    memcpy(head + n, mask, 4);
    n += 4;
    if (net_conn_write(c, head, n) < 0) return -1;
    unsigned char chunk[1024];
    for (size_t done = 0; done < len;) {
        size_t k = len - done < sizeof chunk ? len - done : sizeof chunk;
        for (size_t i = 0; i < k; i++) chunk[i] = data[done + i] ^ mask[(done + i) & 3];
        if (net_conn_write(c, chunk, k) < 0) return -1;
        done += k;
    }
    return 0;
}

static int handshake(net_conn *c, const char *host, const char *path, unsigned char *buf, size_t cap, size_t *len)
{
    unsigned char key_raw[16];
    for (int i = 0; i < 16; i++) key_raw[i] = rand() & 255;
    char *key = base64_encode(key_raw, 16);
    char req[768];
    int n = snprintf(req, sizeof req,
                     "GET %s HTTP/1.1\r\nHost: %s\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                     "Sec-WebSocket-Key: %s\r\nSec-WebSocket-Version: 13\r\nOrigin: %s\r\n"
                     "User-Agent: PictoChatSwitch/" APP_VERSION "\r\n\r\n",
                     path, host, key ? key : "", origin);
    free(key);
    if (net_conn_write(c, req, n) < 0) {
        set_error("The connection was lost while joining.");
        return -1;
    }
    *len = 0;
    for (;;) {
        if (*len >= cap - 1) {
            set_error("The chat server sent an unexpected answer.");
            return -1;
        }
        int r = net_conn_read(c, buf + *len, cap - 1 - *len);
        if (r <= 0) {
            set_error("The chat server closed the connection.");
            return -1;
        }
        *len += r;
        buf[*len] = 0;
        char *end = strstr((char *)buf, "\r\n\r\n");
        if (!end) continue;
        if (strncmp((char *)buf, "HTTP/1.1 101", 12) && strncmp((char *)buf, "HTTP/1.0 101", 12)) {
            int status = atoi((char *)buf + 9);
            snprintf(error, sizeof error, "The chat server refused the connection (HTTP %d).", status);
            return -1;
        }
        size_t used = end + 4 - (char *)buf;
        memmove(buf, buf + used, *len - used);
        *len -= used;
        return 0;
    }
}

static void *ws_thread(void *arg)
{
    (void)arg;
    char host[128];
    const char *path;
    net_conn *c = net_conn_open(url, host, sizeof host, &path, error, sizeof error);
    if (!c) {
        state = WS_CLOSED;
        return NULL;
    }

    size_t cap = 64 * 1024, len = 0;
    unsigned char *buf = malloc(cap);
    char *frag = NULL; /* a fragmented message being put together */
    size_t frag_len = 0;
    unsigned char frag_op = 0;
    if (!buf || handshake(c, host, path, buf, cap, &len) < 0) goto done;
    state = WS_OPEN;

    while (running) {
        msg_t *m;
        while ((m = pop(&out_head, &out_tail))) {
            int ret = send_frame(c, m->opcode, m->data, m->len);
            free(m);
            if (ret < 0) {
                set_error("The connection to PictoChat was lost.");
                goto done;
            }
        }

        /* parse every complete frame in the buffer */
        for (;;) {
            if (len < 2) break;
            unsigned char op = buf[0] & 0x0F;
            int fin = buf[0] & 0x80, masked = buf[1] & 0x80;
            size_t plen = buf[1] & 0x7F, hl = 2;
            if (plen == 126) {
                if (len < 4) break;
                plen = buf[2] << 8 | buf[3];
                hl = 4;
            } else if (plen == 127) {
                if (len < 10) break;
                plen = 0;
                for (int i = 2; i < 10; i++) plen = plen << 8 | buf[i];
                hl = 10;
            }
            if (masked) hl += 4;
            if (plen > MAX_MESSAGE) {
                set_error("PictoChat sent a message that was too big.");
                goto done;
            }
            if (len < hl + plen) {
                if (hl + plen > cap) {
                    unsigned char *grown = realloc(buf, hl + plen + 4096);
                    if (!grown) goto done;
                    buf = grown;
                    cap = hl + plen + 4096;
                }
                break;
            }
            unsigned char *payload = buf + hl;
            if (masked)
                for (size_t i = 0; i < plen; i++) payload[i] ^= buf[hl - 4 + (i & 3)];

            if (op == 0x8) { /* close */
                if (plen >= 2) {
                    int code = payload[0] << 8 | payload[1];
                    if (code != 1000) snprintf(error, sizeof error, "PictoChat closed the connection (%d).", code);
                }
                send_frame(c, 0x8, (const char *)payload, plen >= 2 ? 2 : 0);
                goto done;
            } else if (op == 0x9) { /* ping */
                send_frame(c, 0xA, (const char *)payload, plen);
            } else if (op == 0x1 || op == 0x2 || op == 0x0) {
                if (op != 0) {
                    free(frag);
                    frag = NULL;
                    frag_len = 0;
                    frag_op = op;
                }
                if (frag_len + plen > MAX_MESSAGE) goto done;
                char *grown = realloc(frag, frag_len + plen + 1);
                if (!grown) goto done;
                frag = grown;
                memcpy(frag + frag_len, payload, plen);
                frag_len += plen;
                if (fin) {
                    if (frag_op == 0x1 && (m = msg_new(0x1, frag, frag_len))) push(&in_head, &in_tail, m);
                    free(frag);
                    frag = NULL;
                    frag_len = 0;
                }
            }
            memmove(buf, buf + hl + plen, len - hl - plen);
            len -= hl + plen;
        }

        int w = net_conn_wait(c, 15);
        if (w < 0) {
            set_error("The connection to PictoChat was lost.");
            break;
        }
        if (w > 0) {
            if (len == cap) {
                unsigned char *grown = realloc(buf, cap * 2);
                if (!grown) break;
                buf = grown;
                cap *= 2;
            }
            int r = net_conn_read(c, buf + len, cap - len);
            if (r <= 0) {
                set_error(r == 0 ? "PictoChat closed the connection." : "The connection to PictoChat was lost.");
                break;
            }
            len += r;
        }
    }

done:
    free(frag);
    free(buf);
    net_conn_close(c);
    state = WS_CLOSED;
    return NULL;
}

void ws_open(const char *u, const char *o)
{
    ws_close();
    lock_ready = 1;
    snprintf(url, sizeof url, "%s", u);
    snprintf(origin, sizeof origin, "%s", o);
    error[0] = 0;
    srand(gettime());
    running = 1;
    state = WS_CONNECTING;
    thread_running = thread_start(&thread, ws_thread, 128 * 1024) == 0;
    if (!thread_running) {
        snprintf(error, sizeof error, "Could not start the network thread.");
        state = WS_CLOSED;
    }
}

void ws_close(void)
{
    running = 0;
    if (thread_running) {
        pthread_join(thread, NULL);
        thread_running = 0;
    }
    if (lock_ready) {
        drain(&out_head, &out_tail);
        drain(&in_head, &in_tail);
    }
    state = WS_IDLE;
}

ws_state_t ws_state(void) { return state; }

const char *ws_error(void) { return error[0] ? error : "The connection to PictoChat closed."; }

int ws_send(const char *text)
{
    if (state != WS_OPEN) return -1;
    msg_t *m = msg_new(0x1, text, strlen(text));
    if (!m) return -1;
    push(&out_head, &out_tail, m);
    return 0;
}

char *ws_recv(void)
{
    if (!lock_ready) return NULL;
    msg_t *m = pop(&in_head, &in_tail);
    if (!m) return NULL;
    char *text = malloc(m->len + 1);
    if (text) memcpy(text, m->data, m->len + 1);
    free(m);
    return text;
}
