#pragma once

/* A WebSocket client (RFC 6455) for ws:// and wss:// URLs. One connection at
 * a time; a background thread does all the network I/O, so the calls below
 * never block the frame loop. */

typedef enum { WS_IDLE, WS_CONNECTING, WS_OPEN, WS_CLOSED } ws_state_t;

void ws_open(const char *url, const char *origin);
void ws_close(void);
ws_state_t ws_state(void);
const char *ws_error(void);       /* why the connection closed, for the user */
int ws_send(const char *text);    /* queue a text frame; -1 when not open */
char *ws_recv(void);              /* next text message (malloc'd), or NULL */
