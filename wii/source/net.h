#pragma once
#include <stddef.h>

typedef struct {
    int status;            /* HTTP status, or 0 when the request failed */
    char *body;            /* NUL-terminated, malloc'd */
    size_t len;
    char error[192];       /* why it failed, for the user */
} http_response;

/* Bring up Wi-Fi/LAN. Returns 0 on success; safe to call again. */
int net_start(char *ip, size_t ip_len);
/* GET or POST to http:// or https:// URLs. *progress (0..100) tracks the upload. */
int http_request(const char *method, const char *url, const char *content_type, const char *body,
                 size_t body_len, http_response *res, volatile int *progress);
void http_response_free(http_response *res);

/* A persistent connection to http(s):// or ws(s):// URLs, for the WebSocket client.
 * host_out receives the host name and *path points into url. */
typedef struct net_conn net_conn;
net_conn *net_conn_open(const char *url, char *host_out, size_t host_len, const char **path, char *err, size_t err_len);
int net_conn_write(net_conn *c, const void *data, size_t len);  /* 0, or -1 when the connection broke */
int net_conn_read(net_conn *c, void *buf, size_t len);          /* bytes, 0 when closed, <0 on errors */
int net_conn_wait(net_conn *c, int timeout_ms);                 /* 1 when there is something to read */
void net_conn_close(net_conn *c);
