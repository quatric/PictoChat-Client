#pragma once
#include <gctypes.h>

/* PictoChat Online's protocol (github.com/ayunami2000/ayunpictojava): JSON
 * text frames over a WebSocket, plus "ping"/"pong" keep-alives. */

/* Drawing operations, as in pict.chat's main.js */
enum {
    OP_LINE = 0,      /* line to x,y */
    OP_PEN_UP = 1,    /* stroke ended */
    OP_MOVE = 2,      /* stroke starts at x,y */
    OP_BIG = 3,       /* 2 px pen */
    OP_SMALL = 4,     /* 1 px pen */
    OP_PEN = 5,       /* black ink */
    OP_ERASER = 6,
    OP_RAINBOW = 7,
};

typedef struct {
    float x, y;
    int type;
} pc_op;

typedef struct {
    float x, y;
    char *text;
} pc_textbox;

typedef struct {
    char name[32];
    u32 color;
    int lines;            /* 1..5: how tall the message box is */
    pc_op *ops;
    int nops;
    pc_textbox *tbs;
    int ntbs;
} pc_msg;

typedef enum {
    EV_OPEN,           /* connected: send the captcha token now */
    EV_NAME_VERIFIED,  /* name, color as the server cleaned them up */
    EV_ROOMS,          /* counts[0..3] */
    EV_ROOM_DATA,      /* room ("room_a".."room_e"), private_room */
    EV_PLAYER_JOINED,  /* name */
    EV_PLAYER_LEFT,    /* name */
    EV_MESSAGE,        /* msg */
    EV_LEFT_ROOM,
    EV_CLOSED,         /* text: why */
} chat_event_type;

typedef struct {
    chat_event_type type;
    char name[32];
    u32 color;
    int counts[4];
    char room[16];
    int private_room;
    pc_msg msg;
    char text[192];
} chat_event;

void chat_connect(const char *ws_url, const char *origin);
void chat_disconnect(void);
int chat_connected(void);
int chat_poll(chat_event *ev);      /* 1 when *ev was filled; free ev->msg with pc_msg_free */
void chat_verify(const char *name, u32 color, const char *token);
void chat_join(const char *room_id); /* "room_a".."room_d", "room_e" (new private room) or a 6-letter code */
void chat_leave(void);
void chat_send(const pc_msg *msg);

void pc_msg_free(pc_msg *msg);
/* 1 when the message has something to show (pict.chat's isMessageValid) */
int pc_msg_valid(const pc_msg *msg);
/* Drop redundant pen/size changes, like pict.chat's cleanupDrawing */
void pc_ops_cleanup(pc_op *ops, int *n);
