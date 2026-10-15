#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <malloc.h>
#include "stage.h"

static u32 *stage_px;
static u32 *saved_canvas;
static int saved_w, saved_h;
static u32 *pad_canvas;              /* the GamePad's own picture, when it differs from the TV's */
static u32 *sel_canvas;
static int sel_w, sel_h, selected = SCREEN_TV;

#define R(c) (((c) >> 16) & 0xFF)
#define G(c) (((c) >> 8) & 0xFF)
#define B(c) ((c) & 0xFF)

/* Box-filter weights for scaling n source pixels to m output pixels: each output
 * pixel covers at most two source pixels, so only the seams get blended and the
 * pixel art stays sharp. */
static void scale_table(int n, int m, u16 *index, u8 *weight)
{
    float step = (float)n / m;
    for (int d = 0; d < m; d++) {
        float s0 = d * step, s1 = s0 + step;
        int i = (int)s0;
        if (i >= n - 1) {
            index[d] = n - 2;
            weight[d] = 255;
            continue;
        }
        index[d] = i;
        weight[d] = s1 <= i + 1 ? 0 : (u8)((s1 - (i + 1)) / step * 255);
    }
}

static int view;
static layout_t layouts[2];
int stage_dark_on;
static int flip;

void stage_set_dark(int on) { stage_dark_on = on; }
int stage_dark(void) { return stage_dark_on; }
void stage_flip(int on) { flip = on; }

/* PIXI's ColorMatrixFilter.hue(180): every channel becomes 2/3 of the sum minus itself */
static inline u32 hue_flip(u32 c)
{
    int r = R(c), g = G(c), b = B(c), s = (r + g + b) * 2 / 3;
    r = s - r;
    g = s - g;
    b = s - b;
    r = r < 0 ? 0 : r > 255 ? 255 : r;
    g = g < 0 ? 0 : g > 255 ? 255 : g;
    b = b < 0 ? 0 : b > 255 ? 255 : b;
    return r << 16 | g << 8 | b;
}

static const char *const VIEW_NAMES[VIEW_COUNT] = {
    "Both screens", "Bottom screen", "Top screen", "Side by side", "Large bottom screen",
    "Large top screen", "TV and GamePad", "Upright GamePad", "Upright GamePad (flipped)",
};

static const char *const VIEW_KEYS[VIEW_COUNT] = {
    "both", "bottom", "top", "side", "big-bottom", "big-top", "split", "upright", "upright-flipped",
};

const char *stage_view_name(int v) { return v >= 0 && v < VIEW_COUNT ? VIEW_NAMES[v] : ""; }
const char *stage_view_key(int v) { return v >= 0 && v < VIEW_COUNT ? VIEW_KEYS[v] : VIEW_KEYS[0]; }

int stage_view_from_key(const char *key)
{
    for (int i = 0; i < VIEW_COUNT; i++)
        if (!strcmp(key, VIEW_KEYS[i])) return i;
    return VIEW_BOTH;
}

static void pane(layout_t *l, int sy, int sh, int x, int y, int w, int h)
{
    pane_t *p = &l->pane[l->npanes++];
    p->sy = sy;
    p->sh = sh;
    p->x = x;
    p->y = y;
    p->w = w;
    p->h = h;
}

static void hud(layout_t *l, int x, int y, int w, int h)
{
    l->hud_x = x;
    l->hud_y = y;
    l->hud_w = w;
    l->hud_h = h;
}

/* One layout on a 852x480 picture (or 480x852 upright) */
static void make_layout(layout_t *l, int v)
{
    memset(l, 0, sizeof *l);
    l->w = 852;
    l->h = 480;
    switch (v) {
    default:
    case VIEW_BOTH: /* 1.25x, filling the height */
        pane(l, 0, STAGE_H, (852 - 320) / 2, 0, 320, 480);
        l->sides = 1;
        break;
    case VIEW_TOP:
    case VIEW_BOTTOM: /* 2x, centred */
        pane(l, v == VIEW_BOTTOM ? 192 : 0, 192, (852 - 512) / 2, 48, 512, 384);
        l->sides = 1;
        break;
    case VIEW_SIDE: /* 1.625x each, the name and controls underneath */
        pane(l, 0, 192, 10, 12, 416, 312);
        pane(l, 192, 192, 426, 12, 416, 312);
        hud(l, 10, 336, 832, 132);
        break;
    case VIEW_BIG_BOTTOM: /* the small top screen on the left, the name and controls under it */
        pane(l, 192, 192, 312, 48, 512, 384);
        pane(l, 0, 192, 28, 48, 256, 192);
        hud(l, 28, 252, 256, 180);
        break;
    case VIEW_BIG_TOP:
        pane(l, 0, 192, 28, 48, 512, 384);
        pane(l, 192, 192, 568, 48, 256, 192);
        hud(l, 568, 252, 256, 180);
        break;
    case VIEW_TALL:
    case VIEW_TALL_FLIP: /* 1.875x across the upright GamePad, the name and controls underneath */
        l->w = 480;
        l->h = 852;
        l->rot = v == VIEW_TALL ? 90 : 270;
        pane(l, 0, STAGE_H, 0, 0, 480, 720);
        hud(l, 6, 726, 468, 120);
        break;
    }
}

void stage_set_view(int v)
{
    view = v;
    switch (v) {
    case VIEW_SPLIT:
        make_layout(&layouts[SCREEN_TV], VIEW_TOP);
        make_layout(&layouts[SCREEN_GAMEPAD], VIEW_BOTTOM);
        break;
    case VIEW_TALL:
    case VIEW_TALL_FLIP: /* the TV keeps both screens */
        make_layout(&layouts[SCREEN_TV], VIEW_BOTH);
        make_layout(&layouts[SCREEN_GAMEPAD], v);
        break;
    default:
        make_layout(&layouts[SCREEN_TV], v);
        layouts[SCREEN_GAMEPAD] = layouts[SCREEN_TV];
        break;
    }
}

int stage_view(void) { return view; }
const layout_t *stage_layout(int screen) { return &layouts[screen == SCREEN_GAMEPAD]; }
int stage_mirrored(void) { return view != VIEW_SPLIT && view != VIEW_TALL && view != VIEW_TALL_FLIP; }
int stage_gamepad_turn(void) { return layouts[SCREEN_GAMEPAD].rot; }
u32 *stage_canvas(int screen) { return screen == SCREEN_GAMEPAD ? pad_canvas : canvas; }

void stage_init(void)
{
    stage_px = memalign(32, STAGE_W * STAGE_H * 4);
    memset(stage_px, 0, STAGE_W * STAGE_H * 4);
    pad_canvas = memalign(32, SW * SH * 4);
    memset(pad_canvas, 0, SW * SH * 4);
    stage_set_view(VIEW_BOTH);
}

void stage_select(int screen)
{
    selected = screen == SCREEN_GAMEPAD;
    sel_canvas = canvas;
    sel_w = SW;
    sel_h = SH;
    if (selected) {
        canvas = pad_canvas;
        SW = layouts[selected].w;
        SH = layouts[selected].h;
    }
    gfx_noclip();
}

void stage_unselect(void)
{
    canvas = sel_canvas;
    SW = sel_w;
    SH = sel_h;
    selected = SCREEN_TV;
    gfx_noclip();
}

void stage_begin(void)
{
    saved_canvas = canvas;
    saved_w = SW;
    saved_h = SH;
    canvas = stage_px;
    SW = STAGE_W;
    SH = STAGE_H;
    flip = 1;
    gfx_noclip();
}

void stage_end(void)
{
    canvas = saved_canvas;
    SW = saved_w;
    SH = saved_h;
    gfx_noclip();
}

void stage_present(void)
{
    static u16 *col_index, *row_index;
    static u8 *col_weight, *row_weight;
    static int tw, th, tsh;
    const pane_t *p = &layouts[selected].pane[0];
    if (tw != p->w || th != p->h || tsh != p->sh) {
        free(col_index);
        free(col_weight);
        free(row_index);
        free(row_weight);
        col_index = malloc(p->w * sizeof(u16));
        col_weight = malloc(p->w);
        row_index = malloc(p->h * sizeof(u16));
        row_weight = malloc(p->h);
        scale_table(STAGE_W, p->w, col_index, col_weight);
        scale_table(p->sh, p->h, row_index, row_weight);
        tw = p->w;
        th = p->h;
        tsh = p->sh;
    }
    for (int y = 0; y < p->h; y++) {
        int ry = row_index[y], wy = row_weight[y];
        const u32 *a = stage_px + (p->sy + ry) * STAGE_W, *b = a + STAGE_W;
        u32 *dst = canvas + (p->y + y) * SW + p->x;
        for (int x = 0; x < p->w; x++) {
            int cx = col_index[x], wx = col_weight[x];
            u32 top = wx ? gfx_mix(a[cx], a[cx + 1], wx) : a[cx];
            if (wy) {
                u32 bottom = wx ? gfx_mix(b[cx], b[cx + 1], wx) : b[cx];
                top = gfx_mix(top, bottom, wy);
            }
            dst[x] = stage_dark_on ? top ^ 0xFFFFFF : top;
        }
    }
}

const u32 *stage_pixels(void) { return stage_px; }

void stage_rect(int *x, int *y, int *w, int *h)
{
    const pane_t *p = &layouts[selected].pane[0];
    *x = p->x;
    *y = p->y;
    *w = p->w;
    *h = p->h;
}

int stage_from_screen(int screen, int px, int py, int want, float *sx, float *sy)
{
    const layout_t *l = stage_layout(screen);
    /* the screen is 852x480; an upright picture is turned onto it */
    float x = px + 0.5f, y = py + 0.5f, lx = x, ly = y;
    if (l->rot == 90) {
        lx = y;
        ly = l->h - x;
    } else if (l->rot == 270) {
        lx = l->w - y;
        ly = x;
    }
    int hit = -1;
    for (int i = 0; i < l->npanes && hit < 0; i++) {
        const pane_t *p = &l->pane[i];
        if (lx >= p->x && ly >= p->y && lx < p->x + p->w && ly < p->y + p->h) hit = i;
    }
    int use = want >= 0 && want < l->npanes ? want : hit;
    if (use < 0) {
        *sx = *sy = -1000;
        return -1;
    }
    const pane_t *p = &l->pane[use];
    *sx = (lx - p->x) * STAGE_W / p->w;
    *sy = p->sy + (ly - p->y) * p->sh / p->h;
    return hit;
}

/* ── Sprites ─────────────────────────────────────────────────────────────── */

static inline u32 tint_px(u32 c, u32 tint)
{
    if (tint == 0xFFFFFF) return c & 0xFFFFFF;
    return ((R(c) * R(tint) / 255) << 16) | ((G(c) * G(tint) / 255) << 8) | (B(c) * B(tint) / 255);
}

void stage_blit_region(const image_t *img, int sx, int sy, int w, int h, int x, int y, u32 tint, int alpha)
{
    if (!img || alpha <= 0) return;
    for (int j = 0; j < h; j++) {
        int yy = y + j, iy = sy + j;
        if (yy < 0 || yy >= SH || iy < 0 || iy >= img->h) continue;
        const u32 *src = img->px + iy * img->w;
        u32 *row = canvas + yy * SW;
        for (int i = 0; i < w; i++) {
            int xx = x + i, ix = sx + i;
            if (xx < 0 || xx >= SW || ix < 0 || ix >= img->w) continue;
            u32 c = src[ix];
            int a = (c >> 24) * alpha / 255;
            if (!a) continue;
            /* the hue flip comes before the tint, as PIXI does it */
            u32 p = tint_px(flip && stage_dark_on ? hue_flip(c) : c, tint);
            row[xx] = a >= 255 ? p : gfx_mix(row[xx], p, a);
        }
    }
}

void stage_blit(const image_t *img, int x, int y, u32 tint, int alpha)
{
    if (img) stage_blit_region(img, 0, 0, img->w, img->h, x, y, tint, alpha);
}

void stage_blit_multiply(const image_t *img, int x, int y, u32 tint, int alpha)
{
    if (!img || alpha <= 0) return;
    for (int j = 0; j < img->h; j++) {
        int yy = y + j;
        if (yy < 0 || yy >= SH) continue;
        const u32 *src = img->px + j * img->w;
        u32 *row = canvas + yy * SW;
        for (int i = 0; i < img->w; i++) {
            int xx = x + i;
            if (xx < 0 || xx >= SW) continue;
            u32 c = src[i];
            int a = (c >> 24) * alpha / 255;
            if (!a) continue;
            u32 s = tint_px(flip && stage_dark_on ? hue_flip(c) : c, tint), d = row[xx];
            u32 m = ((R(s) * R(d) / 255) << 16) | ((G(s) * G(d) / 255) << 8) | (B(s) * B(d) / 255);
            row[xx] = gfx_mix(d, m, a);
        }
    }
}

/* ── Strokes ─────────────────────────────────────────────────────────────── */

static void dot(int x, int y, int width, u32 color)
{
    /* a square pen centred on the path, like PIXI's line style at this size */
    int x0 = x - width / 2, y0 = y - width / 2;
    gfx_fill(x0, y0, width, width, color);
}

void stage_line(float x0, float y0, float x1, float y1, int width, u32 color)
{
    float dx = x1 - x0, dy = y1 - y0;
    int steps = (int)ceilf(fmaxf(fabsf(dx), fabsf(dy)));
    if (steps < 1) steps = 1;
    for (int i = 0; i <= steps; i++) {
        float t = (float)i / steps;
        dot((int)floorf(x0 + dx * t), (int)floorf(y0 + dy * t), width, color);
    }
}

u32 brighten(u32 c, int percent)
{
    int r = R(c), g = G(c), b = B(c);
    r += (255 - r) * percent / 100;
    g += (255 - g) * percent / 100;
    b += (255 - b) * percent / 100;
    return r << 16 | g << 8 | b;
}

u32 rainbow(int h)
{
    /* hsl2rgb2dec(h, 1, 0.5) from pict.chat */
    float l = 0.5f, a = 0.5f;
    u32 out = 0;
    const int ns[3] = { 0, 8, 4 };
    for (int c = 0; c < 3; c++) {
        float k = fmodf(ns[c] + h / 30.0f, 12.0f);
        float v = fmaxf(fminf(fminf(k - 3, 9 - k), 1), -1);
        int ch = (int)(255 * (l - a * v));
        out |= (u32)(ch < 0 ? 0 : ch > 255 ? 255 : ch) << (16 - 8 * c);
    }
    return out;
}
