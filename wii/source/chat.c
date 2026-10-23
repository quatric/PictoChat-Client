#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "chat.h"
#include "cJSON.h"
#include "ws.h"

static int opened;
static int closed_reported;
static u32 my_color;
static char my_name[32];

void chat_connect(const char *ws_url, const char *origin)
{
    opened = 0;
    closed_reported = 0;
    ws_open(ws_url, origin);
}

void chat_disconnect(void)
{
    ws_close();
    opened = 0;
}

int chat_connected(void) { return ws_state() == WS_OPEN; }

static void send_json(cJSON *obj)
{
    char *text = cJSON_PrintUnformatted(obj);
    if (text) ws_send(text);
    free(text);
    cJSON_Delete(obj);
}

static cJSON *player_json(void)
{
    cJSON *p = cJSON_CreateObject();
    cJSON_AddStringToObject(p, "name", my_name);
    cJSON_AddNumberToObject(p, "color", my_color);
    return p;
}

void chat_verify(const char *name, u32 color, const char *token)
{
    snprintf(my_name, sizeof my_name, "%s", name);
    my_color = color;
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "type", "cl_verifyName");
    cJSON_AddItemToObject(o, "player", player_json());
    cJSON_AddStringToObject(o, "token", token && *token ? token : "none");
    send_json(o);
}

void chat_join(const char *room_id)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "type", "cl_joinRoom");
    cJSON_AddItemToObject(o, "player", player_json());
    cJSON_AddStringToObject(o, "id", room_id);
    send_json(o);
}

void chat_leave(void)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "type", "cl_leaveRoom");
    send_json(o);
}

void chat_send(const pc_msg *m)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "type", "cl_sendMessage");
    cJSON *msg = cJSON_AddObjectToObject(o, "message");
    cJSON_AddItemToObject(msg, "player", player_json());
    cJSON *drawing = cJSON_AddArrayToObject(msg, "drawing");
    for (int i = 0; i < m->nops; i++) {
        cJSON *op = cJSON_CreateObject();
        cJSON_AddNumberToObject(op, "x", m->ops[i].x);
        cJSON_AddNumberToObject(op, "y", m->ops[i].y);
        cJSON_AddNumberToObject(op, "type", m->ops[i].type);
        cJSON_AddItemToArray(drawing, op);
    }
    cJSON *tbs = cJSON_AddArrayToObject(msg, "textboxes");
    for (int i = 0; i < m->ntbs; i++) {
        if (!m->tbs[i].text || !m->tbs[i].text[0]) continue;
        cJSON *tb = cJSON_CreateObject();
        cJSON_AddStringToObject(tb, "text", m->tbs[i].text);
        cJSON_AddNumberToObject(tb, "x", m->tbs[i].x);
        cJSON_AddNumberToObject(tb, "y", m->tbs[i].y);
        cJSON_AddItemToArray(tbs, tb);
    }
    cJSON_AddNumberToObject(msg, "lines", m->lines);
    send_json(o);
}

/* ── Messages ────────────────────────────────────────────────────────────── */

void pc_msg_free(pc_msg *m)
{
    for (int i = 0; i < m->ntbs; i++) free(m->tbs[i].text);
    free(m->tbs);
    free(m->ops);
    free(m->pixels);
    memset(m, 0, sizeof *m);
}

int pc_msg_valid(const pc_msg *m)
{
    int text = 0;
    if (m->pixels) return 1;
    for (int i = 0; i < m->ntbs; i++)
        for (const char *c = m->tbs[i].text; c && *c; c++)
            if (!isspace((unsigned char)*c)) text = 1;
    if (m->nops == 2 && (m->ops[1].type == OP_ERASER || m->ops[1].type == OP_RAINBOW)) return text;
    if (m->nops > 1) return 1;
    return text;
}

void pc_ops_cleanup(pc_op *ops, int *count)
{
    /* runs of size changes (3/4) or ink changes (5/6/7) keep only their last entry,
     * and an entry that repeats the current state is dropped */
    int n = *count, w = 0;
    int prev_ink = OP_PEN, prev_size = -1;
    for (int i = 0; i < n; i++) {
        int t = ops[i].type;
        int size = t == OP_BIG || t == OP_SMALL, ink = t == OP_PEN || t == OP_ERASER || t == OP_RAINBOW;
        if (size && i + 1 < n && (ops[i + 1].type == OP_BIG || ops[i + 1].type == OP_SMALL)) continue;
        if (ink && i + 1 < n && (ops[i + 1].type == OP_PEN || ops[i + 1].type == OP_ERASER || ops[i + 1].type == OP_RAINBOW))
            continue;
        if (t == prev_ink || t == prev_size) continue;
        if (size) prev_size = t;
        else if (ink) prev_ink = t;
        ops[w++] = ops[i];
    }
    *count = w;
}

static double num(cJSON *o, const char *key, double fallback)
{
    cJSON *v = cJSON_GetObjectItemCaseSensitive(o, key);
    return cJSON_IsNumber(v) ? v->valuedouble : fallback;
}

static void read_player(cJSON *p, char *name, size_t name_len, u32 *color)
{
    cJSON *n = cJSON_GetObjectItemCaseSensitive(p, "name");
    snprintf(name, name_len, "%s", cJSON_IsString(n) ? n->valuestring : "");
    if (color) *color = (u32)num(p, "color", 0x289acb) & 0xFFFFFF;
}

static void read_message(cJSON *m, pc_msg *out)
{
    memset(out, 0, sizeof *out);
    read_player(cJSON_GetObjectItemCaseSensitive(m, "player"), out->name, sizeof out->name, &out->color);
    int lines = (int)num(m, "lines", 1);
    out->lines = lines < 1 ? 1 : lines > 5 ? 5 : lines;

    cJSON *drawing = cJSON_GetObjectItemCaseSensitive(m, "drawing");
    int n = cJSON_GetArraySize(drawing);
    if (n > 20000) n = 20000;
    out->ops = n ? malloc(n * sizeof(pc_op)) : NULL;
    cJSON *it;
    int i = 0;
    cJSON_ArrayForEach(it, drawing) {
        if (i >= n || !out->ops) break;
        if (!cJSON_IsObject(it)) continue;
        out->ops[i].x = num(it, "x", 22);
        out->ops[i].y = num(it, "y", 208);
        out->ops[i].type = (int)num(it, "type", -1);
        i++;
    }
    out->nops = i;

    cJSON *tbs = cJSON_GetObjectItemCaseSensitive(m, "textboxes");
    n = cJSON_GetArraySize(tbs);
    if (n > 256) n = 256;
    out->tbs = n ? calloc(n, sizeof(pc_textbox)) : NULL;
    i = 0;
    cJSON_ArrayForEach(it, tbs) {
        if (i >= n || !out->tbs) break;
        cJSON *text = cJSON_GetObjectItemCaseSensitive(it, "text");
        if (!cJSON_IsObject(it)) continue;
        out->tbs[i].x = num(it, "x", 13);
        out->tbs[i].y = num(it, "y", 198);
        out->tbs[i].text = strdup(cJSON_IsString(text) ? text->valuestring : "");
        i++;
    }
    out->ntbs = i;
}

/* ── Incoming ────────────────────────────────────────────────────────────── */

int chat_poll(chat_event *ev)
{
    memset(ev, 0, sizeof *ev);
    ws_state_t st = ws_state();
    if (st == WS_OPEN && !opened) {
        opened = 1;
        ws_send("handshake");
        ev->type = EV_OPEN;
        return 1;
    }
    for (;;) {
        char *text = ws_recv();
        if (!text) break;
        if (!strcmp(text, "ping")) {
            ws_send("pong");
            free(text);
            continue;
        }
        cJSON *o = cJSON_Parse(text);
        free(text);
        if (!o) continue;
        cJSON *type = cJSON_GetObjectItemCaseSensitive(o, "type");
        const char *t = cJSON_IsString(type) ? type->valuestring : "";
        int got = 1;
        if (!strcmp(t, "sv_nameVerified")) {
            ev->type = EV_NAME_VERIFIED;
            read_player(cJSON_GetObjectItemCaseSensitive(o, "player"), ev->name, sizeof ev->name, &ev->color);
            snprintf(my_name, sizeof my_name, "%s", ev->name);
            my_color = ev->color;
        } else if (!strcmp(t, "sv_roomIds")) {
            ev->type = EV_ROOMS;
            cJSON *count = cJSON_GetObjectItemCaseSensitive(o, "count"), *c;
            int i = 0;
            cJSON_ArrayForEach(c, count) if (i < 4) ev->counts[i++] = c->valueint;
        } else if (!strcmp(t, "sv_roomData")) {
            ev->type = EV_ROOM_DATA;
            cJSON *id = cJSON_GetObjectItemCaseSensitive(o, "id");
            snprintf(ev->room, sizeof ev->room, "%s", cJSON_IsString(id) ? id->valuestring : "room_a");
            ev->private_room = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(o, "private"));
        } else if (!strcmp(t, "sv_playerJoined") || !strcmp(t, "sv_playerLeft")) {
            ev->type = t[9] == 'J' ? EV_PLAYER_JOINED : EV_PLAYER_LEFT;
            read_player(cJSON_GetObjectItemCaseSensitive(o, "player"), ev->name, sizeof ev->name, &ev->color);
        } else if (!strcmp(t, "sv_receivedMessage")) {
            ev->type = EV_MESSAGE;
            read_message(cJSON_GetObjectItemCaseSensitive(o, "message"), &ev->msg);
            if (!pc_msg_valid(&ev->msg)) {
                pc_msg_free(&ev->msg);
                got = 0;
            }
        } else if (!strcmp(t, "sv_leaveRoom")) {
            ev->type = EV_LEFT_ROOM;
        } else {
            got = 0; /* sv_error and anything new */
        }
        cJSON_Delete(o);
        if (got) return 1;
    }
    if (st == WS_CLOSED && !closed_reported) {
        closed_reported = 1;
        ev->type = EV_CLOSED;
        snprintf(ev->text, sizeof ev->text, "%s", ws_error());
        return 1;
    }
    return 0;
}
