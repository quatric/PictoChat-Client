#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "msglog.h"
#include "assets.h"
#include "font.h"
#include "stage.h"

#define MAX_ITEMS 120
#define GAP 2
#define BOX_X 21          /* where message boxes sit, on both screens */
#define INPUT_Y 207       /* the compose box's top on the bottom screen */
#define VIEW_BOTTOM 191
#define SCROLL_SPEED 4

typedef struct {
    int image;            /* a banner image, or -1 for a message */
    char name[32];
    int text_x;
    pc_msg msg;
    int h;
} item_t;

static item_t items[MAX_ITEMS];
static int count;
static int pos;           /* the item the view rests on */
static float offset;      /* y of the list's top, animated towards target */
static int follow = 1;    /* keep the newest item in view */

static int item_top(int index)
{
    int y = 0;
    for (int i = 0; i < index; i++) y += items[i].h + GAP;
    return y;
}

static float target_offset(void)
{
    if (!count) return 0;
    return VIEW_BOTTOM - item_top(pos) - items[pos].h;
}

static void drop_oldest(void)
{
    if (items[0].image < 0) pc_msg_free(&items[0].msg);
    offset += items[0].h + GAP;
    memmove(items, items + 1, (count - 1) * sizeof(item_t));
    count--;
    if (pos > 0) pos--;
}

static item_t *add(void)
{
    if (count == MAX_ITEMS) drop_oldest();
    item_t *it = &items[count++];
    memset(it, 0, sizeof *it);
    if (count == 1) offset = VIEW_BOTTOM + 4;
    if (follow) pos = count - 1;
    return it;
}

void log_reset(void)
{
    for (int i = 0; i < count; i++)
        if (items[i].image < 0) pc_msg_free(&items[i].msg);
    count = 0;
    pos = 0;
    offset = 0;
    follow = 1;
}

void log_add_image(int image)
{
    item_t *it = add();
    it->image = image;
    it->h = img[image] ? img[image]->h : 22;
}

void log_add_banner(int image, const char *name, int text_x)
{
    log_add_image(image);
    item_t *it = &items[count - 1];
    snprintf(it->name, sizeof it->name, "%s", name);
    it->text_x = text_x;
}

static void copy_msg(pc_msg *dst, const pc_msg *src)
{
    *dst = *src;
    dst->ops = src->nops ? malloc(src->nops * sizeof(pc_op)) : NULL;
    if (dst->ops) memcpy(dst->ops, src->ops, src->nops * sizeof(pc_op));
    else dst->nops = 0;
    dst->tbs = src->ntbs ? calloc(src->ntbs, sizeof(pc_textbox)) : NULL;
    dst->ntbs = dst->tbs ? src->ntbs : 0;
    for (int i = 0; i < dst->ntbs; i++) {
        dst->tbs[i] = src->tbs[i];
        dst->tbs[i].text = strdup(src->tbs[i].text ? src->tbs[i].text : "");
    }
    pc_ops_cleanup(dst->ops, &dst->nops);
}

void autotest_log(const char *fmt, ...);
void log_add_message(const pc_msg *msg)
{
#ifdef AUTOTEST
    autotest_log("log msg %s ops %d tbs %d lines %d", msg->name, msg->nops, msg->ntbs, msg->lines);
#endif
    item_t *it = add();
    it->image = -1;
    copy_msg(&it->msg, msg);
    it->h = msg_height(&it->msg);
}

void log_scroll(int delta)
{
    if (!count) return;
    pos += delta;
    if (pos < 0) pos = 0;
    if (pos > count - 1) pos = count - 1;
    follow = pos == count - 1;
}

void log_scroll_end(void)
{
    pos = count ? count - 1 : 0;
    follow = 1;
}

const pc_msg *log_current(void)
{
    if (!count || items[pos].image >= 0) return NULL;
    return &items[pos].msg;
}

void log_update(void)
{
    float target = target_offset();
    if (offset < target) {
        offset += SCROLL_SPEED;
        if (offset > target) offset = target;
    } else if (offset > target) {
        offset -= SCROLL_SPEED;
        if (offset < target) offset = target;
    }
}

void log_draw(void)
{
    gfx_clip(0, 0, STAGE_W, 192);
    int y = (int)offset;
    for (int i = 0; i < count; i++) {
        item_t *it = &items[i];
        if (y + it->h >= 0 && y < 192) {
            if (it->image >= 0) {
                stage_blit(img[it->image], BOX_X, y, 0xFFFFFF, 255);
                if (it->name[0]) ds_text(BOX_X + it->text_x, y + 4, 0xD3CBC3, it->name);
            } else {
                msg_draw(&it->msg, BOX_X, y);
            }
        }
        y += it->h + GAP;
    }
    gfx_noclip();
}

/* ── Message boxes ───────────────────────────────────────────────────────── */

int msg_height(const pc_msg *m)
{
    image_t *bg = img[IMG_BOX_BG1 + m->lines - 1];
    return bg ? bg->h : 22 + (m->lines - 1) * 16;
}

static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }

void ops_draw(const pc_op *ops, int n, int ox, int oy, int clip_lines)
{
    (void)clip_lines;
    int width = 2, mode = 0, deg = 0, have = 0; /* mode: 0 pen, 1 eraser, 2 rainbow */
    float px = 0, py = 0;
    for (int i = 0; i < n; i++) {
        float x = clampf(ops[i].x, 22, 254), y = clampf(ops[i].y, 208, 291);
        if (x <= 110 && y <= 226) {
            x = 110;
            y = 226;
        }
        switch (ops[i].type) {
        case OP_LINE: {
            u32 color = mode == 1 ? 0xFBFBFB : mode == 2 ? rainbow(deg) : 0x000000;
            if (mode == 2) deg = (deg + 12) % 360;
            int w = width + (mode == 1 && width == 2);
            if (have) stage_line(px + ox, py + oy, x + ox, y + oy, w, color);
            px = x;
            py = y;
            have = 1;
            break;
        }
        case OP_PEN_UP:
        case OP_MOVE:
            px = x;
            py = y;
            have = 1;
            break;
        case OP_BIG: width = 2; break;
        case OP_SMALL: width = 1; break;
        case OP_PEN: mode = 0; break;
        case OP_ERASER: mode = 1; break;
        case OP_RAINBOW: mode = 2; break;
        }
    }
}

void msg_draw(const pc_msg *m, int x, int y)
{
    int lines = m->lines;
    stage_blit(img[IMG_BOX_BG1 + lines - 1], x, y, 0xFFFFFF, 255);
    stage_blit(img[IMG_BOX_LINES1 + lines - 1], x, y, brighten(m->color, 75), 255);
    stage_blit(img[IMG_BOX_OUTLINE1 + lines - 1], x, y, m->color, 255);
    ds_text(x + 6, y + 4, m->color, m->name);

    /* contents use pict.chat's stage coordinates, relative to the compose box */
    int ox = x - BOX_X, oy = y - INPUT_Y;
    gfx_clip(x, y, 234, msg_height(m));
    ops_draw(m->ops, m->nops, ox, oy, lines);
    for (int i = 0; i < m->ntbs && i < 256; i++) {
        float tx = clampf(m->tbs[i].x, 13, 256), ty = clampf(m->tbs[i].y, 204, 274);
        if (tx <= 110 && ty <= 226) {
            tx = 110;
            ty = 226;
        }
        const char *t = m->tbs[i].text ? m->tbs[i].text : "";
        int len = strlen(t);
        if (len > 1020) len = 1020;
        ds_text_n((int)tx + ox, (int)ty + oy, 0x000000, t, len);
    }
    gfx_noclip();
}
