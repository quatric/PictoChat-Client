#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "msglog.h"
#include "dsmsg.h"
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
    dst->pixels = src->pixels ? malloc(DSMSG_W * src->pixel_rows) : NULL;
    if (dst->pixels) memcpy(dst->pixels, src->pixels, DSMSG_W * src->pixel_rows);
    else dst->pixel_rows = 0;
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
                stage_flip(0);   /* pict.chat makes these with new PIXI.Sprite, so no hue flip */
                stage_blit(img[it->image], BOX_X, y, 0xFFFFFF, 255);
                stage_flip(1);
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
            u32 color = mode == 1 ? 0xFBFBFB : mode == 2 ? dk(rainbow(deg)) : 0x000000;
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

/* pict.chat's box images carry an 89x19 name tab in their top-left corner;
 * a DS's is fixed at about a quarter of the box wide and 15 rows tall, and
 * the big one covered the top of DS pictures. These piece the images back
 * together with the tab's right edge at TAB_W and its bottom at TAB_H.
 * The tab of a one-line box runs its whole height, so that one only gets
 * narrower. 'edge' is how many rows the bottom corner takes in this layer:
 * 4 with the outline, 3 for the fill, which stops above it. */
#define SRC_TAB_W 88
#define SRC_TAB_H 18
#define TAB_W 58
#define TAB_H 14

static void tab_part(const image_t *im, int sx, int sy, int w, int h, int x, int y, u32 tint, int alpha, int multiply)
{
    if (w <= 0 || h <= 0) return;
    if (multiply) stage_blit_multiply_region(im, sx, sy, w, h, x, y, tint, alpha);
    else stage_blit_region(im, sx, sy, w, h, x, y, tint, alpha);
}

void box_blit(const image_t *im, int x, int y, u32 tint, int alpha, int multiply, int edge)
{
    if (!im) return;
    const int E = SRC_TAB_W, B = SRC_TAB_H, W = TAB_W;
    int short_box = im->h < B + 8;
    int H = short_box ? B : TAB_H, rows = short_box ? im->h : B + 1;
    /* the rest of the box as it is */
    tab_part(im, 0, rows, im->w, im->h - rows, x, y + rows, tint, alpha, multiply);
    tab_part(im, E + 1, 0, im->w - E - 1, rows, x + E + 1, y, tint, alpha, multiply);
    /* the box's top edge where the tab was, from just right of it */
    tab_part(im, E + 1, 0, E - W, rows, x + W + 1, y, tint, alpha, multiply);
    if (short_box) {
        tab_part(im, 0, 0, W - 3, rows, x, y, tint, alpha, multiply);
        tab_part(im, E - 3, 0, 4, rows, x + W - 3, y, tint, alpha, multiply);
        return;
    }
    /* the tab: its body, right edge, bottom edge and bottom-right corner */
    tab_part(im, 0, 0, W - 3, H - 3, x, y, tint, alpha, multiply);
    tab_part(im, E - 3, 0, 4, H - 3, x + W - 3, y, tint, alpha, multiply);
    tab_part(im, 0, B - 3, W - 3, edge, x, y + H - 3, tint, alpha, multiply);
    tab_part(im, E - 3, B - 3, 4, edge, x + W - 3, y + H - 3, tint, alpha, multiply);
    /* under it, the box's left side, from rows ending on a ruled line where
     * the box has one (the compose box rules row 18 and every 16th after) */
    int src = im->h > 40 ? 35 - (B - H) : B + 1;
    tab_part(im, 0, src, W + 1, B - H, x, y + H + 1, tint, alpha, multiply);
}

void box_name(int x, int y, u32 color, const char *name)
{
    gfx_clip(x + 2, y, TAB_W - 3, TAB_H);
    ds_text(x + 6, y + 2, color, name);
    gfx_noclip();
}

void msg_draw(const pc_msg *m, int x, int y)
{
    int lines = m->lines;
    stage_flip(0);
    stage_blit(img[IMG_BOX_BG1 + lines - 1], x, y, 0xFFFFFF, 255);
    box_blit(img[IMG_BOX_LINES1 + lines - 1], x, y, dk(brighten(m->color, 75)), 255, 0, 3);
    box_blit(img[IMG_BOX_OUTLINE1 + lines - 1], x, y, dk(m->color), 255, 0, 4);
    stage_flip(1);
    box_name(x, y, dk(m->color), m->name);

    gfx_clip(x, y, 234, msg_height(m));
    msg_draw_content(m, x - BOX_X, y - INPUT_Y);
    gfx_noclip();
}

/* A DS picture: image x is screen x, row 0 is stage row 208 (a pixel under
 * the compose box's top edge). Drawn as runs of one colour. */
static void pixels_draw(const pc_msg *m, int ox, int oy)
{
    for (int r = 0; r < m->pixel_rows; r++) {
        const u8 *row = m->pixels + r * DSMSG_W;
        for (int c = 0; c < DSMSG_W;) {
            int v = row[c], n = 1;
            while (c + n < DSMSG_W && row[c + n] == v) n++;
            if (v) gfx_fill(c + ox, INPUT_Y + 1 + r + oy, n, 1, v == 1 ? 0x000000 : dk(dsmsg_color(v)));
            c += n;
        }
    }
}

void msg_draw_content(const pc_msg *m, int ox, int oy)
{
    /* contents use pict.chat's stage coordinates, relative to the compose box */
    if (m->pixels) pixels_draw(m, ox, oy);
    ops_draw(m->ops, m->nops, ox, oy, m->lines);
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
}
