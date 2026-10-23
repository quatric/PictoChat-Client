#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "compose.h"
#include "assets.h"
#include "font.h"
#include "msglog.h"
#include "sound.h"
#include "stage.h"
#include "keys.h"

#define MAX_TB 64
#define TB_BYTES 1024
#define MAX_OPS 20000

typedef struct {
    float x, y;
    char text[TB_BYTES];
} tb_t;

typedef struct {
    int x, y, w, h;
} rect_t;

enum { T_SEND, T_RENEW, T_CLEAR, T_BIG, T_SMALL, T_PEN, T_ERASER, T_UP, T_DOWN, T_EXIT,
       T_KB0, T_KB3, T_KB4, T_KB5, T_KB6, T_COUNT };

static const rect_t TOOLS[T_COUNT] = {
    { 225, 296, 31, 30 }, { 225, 327, 31, 23 }, { 225, 351, 31, 24 },
    { 2, 263, 14, 14 }, { 2, 278, 14, 14 }, { 2, 230, 14, 13 }, { 2, 244, 14, 13 },
    { 2, 194, 14, 14 }, { 2, 209, 14, 14 }, { 245, 193, 10, 10 },
    { 2, 299, 14, 14 }, { 2, 316, 14, 14 }, { 2, 333, 14, 14 }, { 2, 350, 14, 14 }, { 2, 367, 14, 14 },
};

static rect_t normal_keys[50], other_keys[59];

static char name[32];
static u32 color;
static tb_t tbs[MAX_TB];
static int ntb, selected;
static pc_op *ops;
static int nops, ops_cap;
static int kb_mode, jp_mode;
static int big_pen = 1, eraser, rainbow_pen, rainbow_deg;
static int drawing, draw_sound_cooldown;
static float last_x, last_y;

/* what the pointer went down on */
enum { HIT_NONE, HIT_KEY, HIT_TOOL, HIT_BOX };
static int hit, hit_index, dragging;
static float drag_x, drag_y;
static char drag_text[8];
static int scroll_down_timer;
static int frame;

static pc_msg last_sent;

/* ── Layout ──────────────────────────────────────────────────────────────── */

static void build_keys(void)
{
    int k = 0;
    for (int i = 0; i < 12; i++) normal_keys[k++] = (rect_t){ 26 + 16 * i, 297, 15, 14 };
    for (int i = 0; i < 10; i++) normal_keys[k++] = (rect_t){ 35 + 16 * i, 312, 15, 15 };
    normal_keys[k++] = (rect_t){ 195, 312, 25, 15 };
    for (int i = 0; i < 10; i++) normal_keys[k++] = (rect_t){ 26 + 16 * i, 328, 15, 15 };
    normal_keys[k++] = (rect_t){ 186, 328, 34, 15 };
    normal_keys[k++] = (rect_t){ 24, 344, 25, 15 };
    for (int i = 0; i < 10; i++) normal_keys[k++] = (rect_t){ 50 + 16 * i, 344, 15, 15 };
    normal_keys[k++] = (rect_t){ 58, 360, 15, 14 };
    normal_keys[k++] = (rect_t){ 74, 360, 15, 14 };
    normal_keys[k++] = (rect_t){ 90, 360, 79, 14 };
    normal_keys[k++] = (rect_t){ 170, 360, 15, 14 };
    normal_keys[k++] = (rect_t){ 186, 360, 15, 14 };

    k = 0;
    other_keys[k++] = (rect_t){ 202, 360, 18, 15 };
    other_keys[k++] = (rect_t){ 202, 328, 18, 31 };
    other_keys[k++] = (rect_t){ 202, 312, 18, 15 };
    for (int i = 0; i < 5; i++) other_keys[k++] = (rect_t){ 24, 296 + 16 * i, 17, 15 };
    for (int i = 0; i < 50; i++) other_keys[k++] = (rect_t){ 42 + 16 * (i % 10), 296 + 16 * (i / 10), 15, 15 };
    other_keys[k++] = (rect_t){ 202, 296, 18, 15 };
}

static int inside(const rect_t *r, float x, float y)
{
    return x >= r->x && y >= r->y && x < r->x + r->w && y < r->y + r->h;
}

/* The key labels of the current keyboard, and how many keys it has */
static const char *const *layout(int *count)
{
    switch (kb_mode) {
    case 1: *count = 50; return K_SHIFT;
    case 2: *count = 50; return K_CAPS;
    case 3: *count = 55; return K_ACCENTS;
    case 4: *count = 59; return jp_mode ? K_JAPANESE2 : K_JAPANESE;
    case 5: *count = 58; return K_SYMBOLS;
    case 6: *count = 58; return K_EMOJIS;
    default: *count = 50; return K_NORMAL;
    }
}

static const rect_t *key_rect(int i) { return kb_mode < 3 ? &normal_keys[i] : &other_keys[i]; }

static int key_at(float x, float y)
{
    int n;
    layout(&n);
    for (int i = 0; i < n; i++)
        if (inside(key_rect(i), x, y)) return i;
    return -1;
}

static int tool_at(float x, float y)
{
    for (int i = 0; i < T_COUNT; i++)
        if (inside(&TOOLS[i], x, y)) return i;
    return -1;
}

/* ── Drawing operations ──────────────────────────────────────────────────── */

static void push_op(float x, float y, int type)
{
    if (nops >= MAX_OPS) return;
    if (nops == ops_cap) {
        int cap = ops_cap ? ops_cap * 2 : 256;
        pc_op *grown = realloc(ops, cap * sizeof(pc_op));
        if (!grown) return;
        ops = grown;
        ops_cap = cap;
    }
    ops[nops++] = (pc_op){ x, y, type };
}

static void state_ops(void)
{
    push_op(0, 0, big_pen ? OP_BIG : OP_SMALL);
    if (eraser) push_op(0, 0, OP_ERASER);
    else if (rainbow_pen) push_op(0, 0, OP_RAINBOW);
}

/* ── Text ────────────────────────────────────────────────────────────────── */

static void initial_textboxes(void)
{
    static const int Y[5] = { 211, 227, 243, 259, 275 };
    ntb = 5;
    for (int i = 0; i < 5; i++) {
        tbs[i].x = i ? 27 : 113;
        tbs[i].y = Y[i];
        tbs[i].text[0] = 0;
    }
    selected = 0;
}

static int utf8_last(const char *s)
{
    int len = strlen(s);
    return utf8_prev(s, len);
}

static int add_textbox(float x, float y, const char *text)
{
    if (ntb >= MAX_TB) return -1;
    tbs[ntb].x = x;
    tbs[ntb].y = y;
    snprintf(tbs[ntb].text, TB_BYTES, "%s", text);
    return ntb++;
}

/* pict.chat's addCharacterDirect for printable keys */
static void add_char(const char *ch)
{
    if (selected >= ntb) {
        initial_textboxes();
    }
    tb_t *t = &tbs[selected];
    size_t len = strlen(t->text), clen = strlen(ch);
    if (len + clen >= TB_BYTES) return;
    memcpy(t->text + len, ch, clen + 1);
    if (ds_width(t->text) > 225 - (t->x - 27)) {
        t->text[len] = 0;
        if (selected < 4) {
            selected++;
            add_char(ch);
        } else if (selected > 4 && t->y + 16 < 278) {
            int i = add_textbox(27, t->y + 16, ch);
            if (i >= 0) selected = i;
        }
    }
}

void compose_type(const char *utf8)
{
    if (!strcmp(utf8, "\n")) {
        compose_enter();
        return;
    }
    add_char(utf8);
}

void compose_enter(void)
{
    if (selected < 4) {
        selected++;
    } else if (selected > 4 && selected < ntb && tbs[selected].y + 16 < 278) {
        int i = add_textbox(27, tbs[selected].y + 16, "");
        if (i >= 0) selected = i;
    }
}

/* Removes the last character and returns it (empty when there was none) */
static void backspace(char *removed)
{
    removed[0] = 0;
    if (selected >= ntb) return;
    char *text = tbs[selected].text;
    if (text[0]) {
        int at = utf8_last(text);
        snprintf(removed, 8, "%s", text + at);
        text[at] = 0;
    } else {
        if (selected > 4) {
            memmove(&tbs[selected], &tbs[selected + 1], (ntb - selected - 1) * sizeof(tb_t));
            ntb--;
        }
        if (selected > 0) selected--;
    }
}

void compose_backspace(void)
{
    char removed[8];
    backspace(removed);
}

/* The Japanese keyboard's voiced/semi-voiced/small modifiers */
static void jp_modify(int key)
{
    int amt = key - 4;
    if (amt > 2) amt = -1;
    char last[8];
    backspace(last);
    if (!last[0]) return;
    const char *p = last;
    u32 cp = utf8_next(&p);
    const char *set = JP_MOD[key - 5];
    int in_set = 0, next_in_set = 0;
    for (const char *s = set; *s;) {
        u32 c = utf8_next(&s);
        if (c == cp) in_set = 1;
        if (c == cp + amt) next_in_set = 1;
    }
    char out[8];
    out[utf8_encode(in_set ? cp - amt : next_in_set ? cp + amt : cp, out)] = 0;
    add_char(out);
}

static void press_key(int i)
{
    int n;
    const char *const *keys = layout(&n);
    const char *k = keys[i];
    if (kb_mode == 4 && i > 2 && i < 5) {
        jp_mode = i - 3;
    } else if (kb_mode == 4 && i > 4 && i < 8) {
        jp_modify(i);
    } else if (!strcmp(k, "BACKSPACE")) {
        compose_backspace();
    } else if (!strcmp(k, "SHIFT")) {
        if (kb_mode < 3) kb_mode = kb_mode == 1 ? 0 : 1;
    } else if (!strcmp(k, "CAPS")) {
        if (kb_mode < 3) kb_mode = kb_mode == 2 ? 0 : 2;
    } else if (!strcmp(k, "ENTER")) {
        compose_enter();
    } else if (k[0]) {
        add_char(k);
    }
    if (kb_mode == 1 && i != 34) kb_mode = 0;
    sound_play(SND_KEY_UP);
}

static int draggable(int i)
{
    int n;
    const char *k = layout(&n)[i];
    if (!k[0] || !strcmp(k, "BACKSPACE") || !strcmp(k, "CAPS") || !strcmp(k, "ENTER") || !strcmp(k, "SHIFT")) return 0;
    return !(kb_mode == 4 && i >= 3 && i <= 7);
}

/* ── Stage ───────────────────────────────────────────────────────────────── */

void compose_reset(const char *n, u32 c)
{
    snprintf(name, sizeof name, "%s", n);
    color = c;
    if (!normal_keys[0].w) build_keys();
    kb_mode = 0;
    jp_mode = 0;
    hit = HIT_NONE;
    dragging = 0;
    drawing = 0;
    compose_clear();
}

void compose_clear(void)
{
    nops = 0;
    state_ops();
    initial_textboxes();
}

static int is_empty(void)
{
    for (int i = 0; i < ntb; i++)
        if (tbs[i].text[0]) return 0;
    pc_ops_cleanup(ops, &nops);
    if (nops == 2 && (ops[1].type == OP_ERASER || ops[1].type == OP_RAINBOW)) return 1;
    return nops <= 1;
}

int compose_send(void)
{
    pc_ops_cleanup(ops, &nops);
    pc_msg m = { .color = color, .lines = 1 };
    snprintf(m.name, sizeof m.name, "%s", name);
    float lowest = 0;
    for (int i = 0; i < nops; i++) {
        if (ops[i].x < 0) ops[i].x = 0;
        if (ops[i].y < 0) ops[i].y = 0;
        if (ops[i].y > lowest) lowest = ops[i].y;
    }
    m.ops = ops;
    m.nops = nops;
    pc_textbox list[MAX_TB];
    for (int i = 0; i < ntb; i++) {
        if (!tbs[i].text[0]) continue;
        list[m.ntbs++] = (pc_textbox){ tbs[i].x, tbs[i].y, tbs[i].text };
        if (tbs[i].y + 12 > lowest) lowest = tbs[i].y + 12;
    }
    m.tbs = list;
    if (lowest > 226) m.lines = 2;
    if (lowest > 240) m.lines = 3;
    if (lowest > 256) m.lines = 4;
    if (lowest > 274) m.lines = 5;
    if (!pc_msg_valid(&m)) {
        sound_play(SND_ERROR);
        return COMPOSE_NOTHING;
    }
    sound_play(SND_SEND);

    pc_msg_free(&last_sent);
    last_sent = m;
    last_sent.ops = malloc(nops * sizeof(pc_op));
    if (last_sent.ops) memcpy(last_sent.ops, ops, nops * sizeof(pc_op));
    else last_sent.nops = 0;
    last_sent.tbs = m.ntbs ? calloc(m.ntbs, sizeof(pc_textbox)) : NULL;
    if (!last_sent.tbs) last_sent.ntbs = 0;
    for (int i = 0; i < last_sent.ntbs; i++) {
        last_sent.tbs[i] = list[i];
        last_sent.tbs[i].text = strdup(list[i].text);
    }
    compose_clear();
    return COMPOSE_SENT;
}

const pc_msg *compose_last_sent(void) { return &last_sent; }

void compose_renew(const pc_msg *from)
{
    if (!from || (from->nops <= 1 && !from->ntbs)) {
        sound_play(SND_ERROR);
        return;
    }
    sound_play(SND_RETRIEVE);
    for (int i = 0; i < from->nops; i++) push_op(from->ops[i].x, from->ops[i].y, from->ops[i].type);
    for (int i = 0; i < from->ntbs; i++) add_textbox(from->tbs[i].x, from->tbs[i].y, from->tbs[i].text);
    selected = 0;
    push_op(0, 0, big_pen ? OP_BIG : OP_SMALL);
    push_op(0, 0, eraser ? OP_ERASER : rainbow_pen ? OP_RAINBOW : OP_PEN);
    pc_ops_cleanup(ops, &nops);
}

int keyboard_key_at(float x, float y)
{
    if (!normal_keys[0].w) build_keys();
    for (int i = 0; i < 50; i++)
        if (inside(&normal_keys[i], x, y)) return i;
    return -1;
}

const char *keyboard_label(int mode, int key)
{
    if (key < 0 || key >= 50) return "";
    return mode == 1 ? K_SHIFT[key] : mode == 2 ? K_CAPS[key] : K_NORMAL[key];
}

void compose_next_keyboard(void)
{
    sound_play(SND_SCROLL);
    if (kb_mode < 3) kb_mode = 3;
    else if (kb_mode == 4 && jp_mode == 0) jp_mode = 1;
    else kb_mode = (kb_mode + 1) % 7;
}

static void set_pen_size(int big)
{
    big_pen = big;
    sound_play(big ? SND_BIG_PEN : SND_SMALL_PEN);
    push_op(0, 0, big ? OP_BIG : OP_SMALL);
}

static void pen_mode(void)
{
    if (!eraser) rainbow_pen = !rainbow_pen;
    eraser = 0;
    sound_play(SND_PEN);
    push_op(0, 0, rainbow_pen ? OP_RAINBOW : OP_PEN);
}

static void eraser_mode(void)
{
    eraser = 1;
    rainbow_pen = 0;
    sound_play(SND_ERASER);
    push_op(0, 0, OP_ERASER);
}

void compose_toggle_pen_size(void) { set_pen_size(!big_pen); }

void compose_toggle_pen_mode(void)
{
    if (eraser || !rainbow_pen) pen_mode();
    else eraser_mode();
}

static int tool(int t)
{
    switch (t) {
    case T_SEND: return compose_send();
    case T_RENEW: return COMPOSE_RENEW;
    case T_CLEAR:
        sound_play(is_empty() ? SND_ERROR : SND_CLEAR);
        compose_clear();
        break;
    case T_BIG: set_pen_size(1); break;
    case T_SMALL: set_pen_size(0); break;
    case T_PEN: pen_mode(); break;
    case T_ERASER: eraser_mode(); break;
    case T_UP:
        sound_play(SND_SCROLL);
        return COMPOSE_SCROLL_UP;
    case T_DOWN:
        sound_play(SND_SCROLL);
        if (scroll_down_timer > 0) return COMPOSE_SCROLL_END;
        scroll_down_timer = 12;
        return COMPOSE_SCROLL_DOWN;
    case T_EXIT: return COMPOSE_EXIT;
    default:
        sound_play(SND_SCROLL);
        kb_mode = t == T_KB0 ? 0 : t == T_KB3 ? 3 : t == T_KB4 ? 4 : t == T_KB5 ? 5 : 6;
        break;
    }
    return COMPOSE_NOTHING;
}

static int in_draw_area(float x, float y)
{
    return x > 22 && x < 22 + 232 && y > 208 && y < 208 + 83 && !(x <= 110 && y <= 226);
}

int compose_touch(int phase, float x, float y)
{
    int result = COMPOSE_NOTHING;
    if (phase == TOUCH_DOWN) {
        int i;
        hit = HIT_NONE;
        if ((i = tool_at(x, y)) >= 0) {
            hit = HIT_TOOL;
            hit_index = i;
        } else if ((i = key_at(x, y)) >= 0) {
            hit = HIT_KEY;
            hit_index = i;
            dragging = 0;
            sound_play(SND_KEY_DOWN);
        } else if (x >= 21 && y >= 207 && x < 21 + 234 && y < 207 + 85) {
            hit = HIT_BOX;
            if (in_draw_area(x, y)) {
                drawing = 1;
                push_op(x, y, OP_MOVE);
                last_x = x;
                last_y = y;
                sound_play(SND_DRAW_DOWN);
            }
        }
    } else if (phase == TOUCH_HELD) {
        if (hit == HIT_KEY && !dragging && !inside(key_rect(hit_index), x, y) && draggable(hit_index)) {
            int n;
            dragging = 1;
            snprintf(drag_text, sizeof drag_text, "%s", layout(&n)[hit_index]);
        }
        if (dragging) {
            drag_x = x;
            drag_y = y;
        }
        if (drawing) {
            if (in_draw_area(x, y)) {
                if (nops && ops[nops - 1].type == OP_PEN_UP) {
                    push_op(x, y, OP_MOVE);
                    last_x = x;
                    last_y = y;
                }
                float dx = x - last_x, dy = y - last_y;
                if (dx * dx + dy * dy >= 4) {
                    push_op(x, y, OP_LINE);
                    last_x = x;
                    last_y = y;
                    if (draw_sound_cooldown <= 0) {
                        sound_play(SND_DRAW);
                        draw_sound_cooldown = 5;
                    }
                }
            } else if (nops && ops[nops - 1].type != OP_PEN_UP) {
                push_op(0, 0, OP_PEN_UP);
            }
        }
    } else { /* TOUCH_UP */
        if (hit == HIT_TOOL && inside(&TOOLS[hit_index], x, y)) {
            result = tool(hit_index);
        } else if (hit == HIT_KEY) {
            if (inside(key_rect(hit_index), x, y)) {
                press_key(hit_index);
            } else if (dragging && x > 23 + 10 && x < 21 + 245 - 10 && y > 208 + 10 && y < 208 + 70 + 10 &&
                       !(x <= 110 + 10 && y <= 226 + 10)) {
                int i = add_textbox(x - 10, y - 10, drag_text);
                if (i >= 0) selected = i;
                sound_play(SND_DROP_LETTER);
            }
        }
        if (drawing) {
            push_op(0, 0, OP_PEN_UP);
            drawing = 0;
        }
        hit = HIT_NONE;
        dragging = 0;
    }
    return result;
}

void compose_update(void)
{
    frame++;
    if (scroll_down_timer > 0) scroll_down_timer--;
    if (draw_sound_cooldown > 0) draw_sound_cooldown--;
    if (rainbow_pen && frame % 6 == 0) rainbow_deg = (rainbow_deg + 3) % 360;
}

static void highlight(const rect_t *r, u32 c, int alpha)
{
    gfx_fill_alpha(r->x, r->y, r->w, r->h, c, alpha);
}

void compose_draw(int alpha)
{
    u32 tint = dk(color);
    stage_blit(img[IMG_DRAWUI], 0, 192, 0xFFFFFF, alpha);
    static const int OVERLAY[7] = { -1, IMG_SHIFT, IMG_CAPS, IMG_ACCENTS, IMG_JAPANESE, IMG_SYMBOLS, IMG_EMOJIS };
    int overlay = kb_mode == 4 && jp_mode ? IMG_JAPANESE2 : OVERLAY[kb_mode];
    if (overlay >= 0) stage_blit(img[overlay], 0, 192, 0xFFFFFF, alpha);

    stage_blit(img[IMG_BOX_BG5], 21, 207, 0xFFFFFF, alpha);
    box_blit(img[IMG_BOX_OUTLINE5], 21, 207, tint, alpha, 0, 4);
    if (alpha >= 255) {
        gfx_clip(21, 207, 234, 85);
        ops_draw(ops, nops, 0, 0, 5);
        gfx_noclip();
    }
    /* in dark mode pict.chat's lines come out grey: the multiplied sprite loses its tint */
    box_blit(img[IMG_BOX_LINES], 21, 207, stage_dark() ? 0x303030 : brighten(color, 75), alpha, 1, 3);
    if (alpha < 255) return;

    box_name(21, 207, tint, name);
    for (int i = 0; i < ntb; i++) ds_text((int)tbs[i].x, (int)tbs[i].y, 0x000000, tbs[i].text);
    if (selected < ntb && (frame / 30) % 2 == 0) {
        tb_t *t = &tbs[selected];
        gfx_fill((int)t->x + ds_width(t->text), (int)t->y + 1, 1, 10, tint);
    }

    u32 mode_tint = rainbow_pen ? dk(rainbow(rainbow_deg)) : tint;
    gfx_fill_alpha(2, big_pen ? 263 : 278, 14, 14, tint, 128);
    gfx_fill_alpha(2, eraser ? 244 : 230, 14, 13, mode_tint, 128);
    gfx_fill_alpha(2, 299 + 17 * (kb_mode > 2 ? kb_mode - 2 : 0), 14, 14, tint, 128);

    if (hit == HIT_TOOL) highlight(&TOOLS[hit_index], tint, 102);
    else if (hit == HIT_KEY && !dragging) highlight(key_rect(hit_index), tint, 102);
    if (dragging) ds_text((int)drag_x - 10, (int)drag_y - 10, 0x000000, drag_text);
}
