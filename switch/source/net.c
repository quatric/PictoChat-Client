#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <errno.h>
#include <unistd.h>
#include <pthread.h>
#include <poll.h>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "platform.h"
#include <mbedtls/ssl.h>
#include <mbedtls/entropy.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/x509_crt.h>
#include <mbedtls/error.h>
#include <mbedtls/platform_time.h>
#include "net.h"
#include "certs.h"
#include "version.h"

#define TIMEOUT_MS 25000
#define ERR_SEND_FAILED -0x004E
#define ERR_RECV_FAILED -0x004C

static int net_ready;
static int tls_ready;
static mbedtls_entropy_context entropy;
static mbedtls_ctr_drbg_context drbg;
static mbedtls_x509_crt cas;
static mbedtls_ssl_config conf;
static pthread_mutex_t tls_lock = PTHREAD_MUTEX_INITIALIZER;

mbedtls_ms_time_t mbedtls_ms_time(void)
{
    return (mbedtls_ms_time_t)ticks_to_millisecs(gettime());
}

int net_start(char *ip, size_t ip_len)
{
    if (!net_ready) {
        static int services_ready;
        if (!services_ready) {
            if (R_FAILED(nifmInitialize(NifmServiceType_User))) return -1;
            if (R_FAILED(socketInitializeDefault())) {
                nifmExit();
                return -1;
            }
            services_ready = 1;
        }
        NifmInternetConnectionType type;
        u32 strength;
        NifmInternetConnectionStatus status;
        if (R_FAILED(nifmGetInternetConnectionStatus(&type, &strength, &status)) ||
            status != NifmInternetConnectionStatus_Connected)
            return -1;
        u32 addr = 0;
        if (ip && R_SUCCEEDED(nifmGetCurrentIpAddress(&addr)))
            snprintf(ip, ip_len, "%u.%u.%u.%u", (unsigned)addr & 255, (unsigned)(addr >> 8) & 255,
                     (unsigned)(addr >> 16) & 255, (unsigned)(addr >> 24));
        net_ready = 1;
    }
    return 0;
}

/* ── Entropy ─────────────────────────────────────────────────────────────── */
/* The system's random number generator, through libnx. */
static int timing_entropy(void *data, unsigned char *out, size_t len, size_t *olen)
{
    (void)data;
    randomGet(out, len);
    *olen = len;
    return 0;
}

static int tls_init(char *err, size_t err_len)
{
    if (tls_ready) return 0;
    mbedtls_entropy_init(&entropy);
    mbedtls_ctr_drbg_init(&drbg);
    mbedtls_x509_crt_init(&cas);
    mbedtls_ssl_config_init(&conf);
    mbedtls_entropy_add_source(&entropy, timing_entropy, NULL, 32, MBEDTLS_ENTROPY_SOURCE_STRONG);

    char personal[96];
    snprintf(personal, sizeof personal, "PictoChat-Switch %p %llu %lld", (void *)personal, (unsigned long long)gettime(),
             (long long)time(NULL));
    int ret = mbedtls_ctr_drbg_seed(&drbg, mbedtls_entropy_func, &entropy, (const unsigned char *)personal,
                                    strlen(personal));
    if (!ret) ret = mbedtls_x509_crt_parse(&cas, (const unsigned char *)PICTOCARD_CA_PEM, sizeof PICTOCARD_CA_PEM);
    if (ret < 0) {
        snprintf(err, err_len, "Could not start secure connections (%d).", ret);
        return -1;
    }
    mbedtls_ssl_config_defaults(&conf, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT);
    mbedtls_ssl_conf_authmode(&conf, MBEDTLS_SSL_VERIFY_REQUIRED);
    mbedtls_ssl_conf_ca_chain(&conf, &cas, NULL);
    mbedtls_ssl_conf_rng(&conf, mbedtls_ctr_drbg_random, &drbg);
    tls_ready = 1;
    return 0;
}

/* ── Sockets with timeouts ───────────────────────────────────────────────── */

static int wait_fd(int fd, int events)
{
    struct pollfd p = { .fd = fd, .events = events, .revents = 0 };
    int ret = poll(&p, 1, TIMEOUT_MS);
    if (ret == 0) return MBEDTLS_ERR_SSL_TIMEOUT;
    return ret < 0 ? ret : 0;
}

static int sock_send(void *ctx, const unsigned char *buf, size_t len)
{
    int fd = (int)(intptr_t)ctx;
    if (wait_fd(fd, POLLOUT) < 0) return MBEDTLS_ERR_SSL_TIMEOUT;
    int ret = send(fd, buf, len > 16384 ? 16384 : len, 0);
    if (ret < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return MBEDTLS_ERR_SSL_WANT_WRITE;
    return ret < 0 ? ERR_SEND_FAILED : ret;
}

static int sock_recv(void *ctx, unsigned char *buf, size_t len)
{
    int fd = (int)(intptr_t)ctx;
    int w = wait_fd(fd, POLLIN);
    if (w < 0) return MBEDTLS_ERR_SSL_TIMEOUT;
    int ret = recv(fd, buf, len, 0);
    if (ret < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return MBEDTLS_ERR_SSL_WANT_READ;
    return ret < 0 ? ERR_RECV_FAILED : ret;
}

struct net_conn {
    int fd;
    int tls;
    mbedtls_ssl_context ssl;
};
typedef struct net_conn conn_t;

static int conn_write(conn_t *c, const void *data, size_t len)
{
    const unsigned char *p = data;
    while (len) {
        int ret = c->tls ? mbedtls_ssl_write(&c->ssl, p, len) : sock_send((void *)(intptr_t)c->fd, p, len);
        if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE) continue;
        if (ret <= 0) return -1;
        p += ret;
        len -= ret;
    }
    return 0;
}

static int conn_read(conn_t *c, void *buf, size_t len)
{
    for (;;) {
        int ret = c->tls ? mbedtls_ssl_read(&c->ssl, buf, len) : sock_recv((void *)(intptr_t)c->fd, buf, len);
        if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE) continue;
        if (ret == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY || ret == 0) return 0;
        return ret;
    }
}

/* ── HTTP ────────────────────────────────────────────────────────────────── */

static int parse_url(const char *url, int *tls, char *host, size_t host_len, int *port, const char **path)
{
    const char *p;
    if (!strncmp(url, "https://", 8)) { *tls = 1; *port = 443; p = url + 8; }
    else if (!strncmp(url, "http://", 7)) { *tls = 0; *port = 80; p = url + 7; }
    else if (!strncmp(url, "wss://", 6)) { *tls = 1; *port = 443; p = url + 6; }
    else if (!strncmp(url, "ws://", 5)) { *tls = 0; *port = 80; p = url + 5; }
    else return -1;
    const char *end = p + strcspn(p, ":/");
    if ((size_t)(end - p) >= host_len || end == p) return -1;
    memcpy(host, p, end - p);
    host[end - p] = 0;
    if (*end == ':') {
        *port = atoi(end + 1);
        end += strcspn(end, "/");
    }
    *path = *end ? end : "/";
    return 0;
}

static void fail(http_response *res, const char *msg, int code)
{
    if (code) snprintf(res->error, sizeof res->error, "%s (%d)", msg, code);
    else snprintf(res->error, sizeof res->error, "%s", msg);
}

static int dechunk(char *body, size_t len, size_t *out_len)
{
    size_t r = 0, w = 0;
    while (r < len) {
        char *line_end = strstr(body + r, "\r\n");
        if (!line_end) return -1;
        size_t size = strtoul(body + r, NULL, 16);
        r = line_end - body + 2;
        if (size == 0) break;
        if (r + size > len) return -1;
        memmove(body + w, body + r, size);
        w += size;
        r += size + 2;
    }
    body[w] = 0;
    *out_len = w;
    return 0;
}

/* Resolve, connect and (for https/wss) finish the TLS handshake. */
static int conn_open(conn_t *c, int tls, const char *host, int port, char *err, size_t err_len)
{
    memset(c, 0, sizeof *c);
    c->fd = -1;
    c->tls = tls;
    if (!net_ready) {
        snprintf(err, err_len, "The Switch is not connected to the Internet.");
        return -1;
    }
    if (tls) {
        pthread_mutex_lock(&tls_lock);
        int ret = tls_init(err, err_len);
        pthread_mutex_unlock(&tls_lock);
        if (ret < 0) return -1;
    }

    /* getaddrinfo rather than gethostbyname: the latter also reads nsysnet's h_errno */
    struct addrinfo hints, *ai = NULL;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, NULL, &hints, &ai) != 0 || !ai) {
        snprintf(err, err_len, "Could not find %s. Check the Internet connection.", host);
        return -1;
    }
    struct sockaddr_in sa;
    memcpy(&sa, ai->ai_addr, sizeof sa);
    freeaddrinfo(ai);
    sa.sin_port = htons(port);

    c->fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (c->fd < 0) {
        snprintf(err, err_len, "Could not open a network connection (%d).", -errno);
        return -1;
    }
    int ret = connect(c->fd, (struct sockaddr *)&sa, sizeof sa);
    if (ret < 0) {
        ret = -errno;
        close(c->fd);
        c->fd = -1;
        snprintf(err, err_len, "Could not connect to %s (%d).", host, ret);
        return -1;
    }
    if (tls) {
        mbedtls_ssl_init(&c->ssl);
        if ((ret = mbedtls_ssl_setup(&c->ssl, &conf)) || (ret = mbedtls_ssl_set_hostname(&c->ssl, host))) {
            snprintf(err, err_len, "Could not start a secure connection (%d).", ret);
            goto fail;
        }
        mbedtls_ssl_set_bio(&c->ssl, (void *)(intptr_t)c->fd, sock_send, sock_recv, NULL);
        while ((ret = mbedtls_ssl_handshake(&c->ssl)) != 0) {
            if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE) continue;
            if (ret == MBEDTLS_ERR_X509_CERT_VERIFY_FAILED)
                snprintf(err, err_len, "The server's certificate could not be verified.");
            else
                snprintf(err, err_len, "The secure connection failed (%d).", ret);
            goto fail;
        }
    }
    return 0;

fail:
    mbedtls_ssl_free(&c->ssl);
    close(c->fd);
    c->fd = -1;
    return -1;
}

static void conn_close(conn_t *c)
{
    if (c->fd < 0) return;
    if (c->tls) {
        mbedtls_ssl_close_notify(&c->ssl);
        mbedtls_ssl_free(&c->ssl);
    }
    close(c->fd);
    c->fd = -1;
}

int http_request(const char *method, const char *url, const char *content_type, const char *body,
                 size_t body_len, http_response *res, volatile int *progress)
{
    memset(res, 0, sizeof *res);
    int tls, port;
    char host[128];
    const char *path;
    if (parse_url(url, &tls, host, sizeof host, &port, &path) < 0) {
        fail(res, "The server address is not valid.", 0);
        return -1;
    }
    conn_t c;
    if (conn_open(&c, tls, host, port, res->error, sizeof res->error) < 0) return -1;

    int result = -1;
    char *buf = NULL;
    char head[640];
    int n = snprintf(head, sizeof head,
                     "%s %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: PictoChatSwitch/" APP_VERSION
                     "\r\nAccept: application/json\r\nConnection: close\r\n",
                     method, path, host);
    if (body) n += snprintf(head + n, sizeof head - n, "Content-Type: %s\r\nContent-Length: %u\r\n",
                            content_type ? content_type : "application/json", (unsigned)body_len);
    n += snprintf(head + n, sizeof head - n, "\r\n");
    if (conn_write(&c, head, n) < 0) {
        fail(res, "The connection was lost while sending.", 0);
        goto done;
    }
    for (size_t sent = 0; body && sent < body_len;) {
        size_t chunk = body_len - sent > 8192 ? 8192 : body_len - sent;
        if (conn_write(&c, body + sent, chunk) < 0) {
            fail(res, "The connection was lost while sending.", 0);
            goto done;
        }
        sent += chunk;
        if (progress) *progress = (int)(sent * 100 / body_len);
    }

    size_t cap = 16384, len = 0;
    buf = malloc(cap + 1);
    for (;;) {
        if (len + 4096 > cap) {
            if (cap >= 8 * 1024 * 1024) break;
            cap *= 2;
            char *grown = realloc(buf, cap + 1);
            if (!grown) break;
            buf = grown;
        }
        int r = conn_read(&c, buf + len, cap - len);
        if (r <= 0) {
            if (r < 0 && !len) {
                fail(res, r == MBEDTLS_ERR_SSL_TIMEOUT ? "The server took too long to answer." : "The connection was lost.", r);
                goto done;
            }
            break;
        }
        len += r;
    }
    buf[len] = 0;

    char *sep = strstr(buf, "\r\n\r\n");
    if (strncmp(buf, "HTTP/1.", 7) || !sep) {
        fail(res, "The server sent an unexpected answer.", 0);
        goto done;
    }
    res->status = atoi(buf + 9);
    *sep = 0;
    int chunked = 0;
    for (char *line = strstr(buf, "\r\n"); line; line = strstr(line + 2, "\r\n")) {
        if (!strncasecmp(line + 2, "Transfer-Encoding:", 18) && strstr(line + 20, "chunked")) chunked = 1;
    }
    size_t body_start = sep + 4 - buf;
    res->len = len - body_start;
    memmove(buf, buf + body_start, res->len + 1);
    if (chunked && dechunk(buf, res->len, &res->len) < 0) {
        fail(res, "The server's answer was cut off.", 0);
        res->status = 0;
        goto done;
    }
    res->body = buf;
    buf = NULL;
    result = 0;

done:
    conn_close(&c);
    free(buf);
    return result;
}

void http_response_free(http_response *res)
{
    free(res->body);
    res->body = NULL;
}

/* ── Persistent connections (WebSocket) ──────────────────────────────────── */

net_conn *net_conn_open(const char *url, char *host_out, size_t host_len, const char **path, char *err, size_t err_len)
{
    int tls, port;
    if (parse_url(url, &tls, host_out, host_len, &port, path) < 0) {
        snprintf(err, err_len, "The chat server address is not valid.");
        return NULL;
    }
    conn_t *c = malloc(sizeof *c);
    if (!c) return NULL;
    if (conn_open(c, tls, host_out, port, err, err_len) < 0) {
        free(c);
        return NULL;
    }
    return c;
}

int net_conn_write(net_conn *c, const void *data, size_t len) { return conn_write(c, data, len); }

int net_conn_read(net_conn *c, void *buf, size_t len) { return conn_read(c, buf, len); }

int net_conn_wait(net_conn *c, int timeout_ms)
{
    if (c->tls && mbedtls_ssl_get_bytes_avail(&c->ssl) > 0) return 1;
    struct pollfd p = { .fd = c->fd, .events = POLLIN, .revents = 0 };
    int ret = poll(&p, 1, timeout_ms);
    if (ret < 0) return ret;
    return ret > 0 && (p.revents & (POLLIN | POLLHUP | POLLERR)) ? 1 : 0;
}

void net_conn_close(net_conn *c)
{
    if (!c) return;
    conn_close(c);
    free(c);
}
