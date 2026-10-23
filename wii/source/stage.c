#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <malloc.h>
#include "stage.h"

static u32 *stage_px;
static u32 *saved_canvas;
static int saved_w, saved_h;
static int out_x, out_y, out_w, out_h;
static u16 *col_index, *row_index;
static u8 *col_weight, *row_weight;

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

static int view, src_y, src_h;
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

/* Both screens at 1.25x filling the height, or one screen at 2x. In 4:3 a zoomed
 * screen sits at the top so the HUD fits underneath; in 16:9 it is centred. */
void stage_set_view(int v)
{
    view = v;
    src_y = v == VIEW_BOTTOM ? 192 : 0;
    src_h = v == VIEW_BOTH ? STAGE_H : 192;
    if (v == VIEW_BOTH) {
        out_h = SH;
        out_w = STAGE_W * SH / STAGE_H;
        out_y = 0;
    } else {
        out_w = STAGE_W * 2;
        out_h = 192 * 2;
        out_y = SW >= 800 ? (SH - out_h) / 2 : 0;
    }
    out_x = (SW - out_w) / 2;
    free(col_index);
    free(col_weight);
    free(row_index);
    free(row_weight);
    col_index = malloc(out_w * sizeof(u16));
    col_weight = malloc(out_w);
    row_index = malloc(out_h * sizeof(u16));
    row_weight = malloc(out_h);
    scale_table(STAGE_W, out_w, col_index, col_weight);
    scale_table(src_h, out_h, row_index, row_weight);
}

int stage_view(void) { return view; }

void stage_init(void)
{
    stage_px = memalign(32, STAGE_W * STAGE_H * 4);
    memset(stage_px, 0, STAGE_W * STAGE_H * 4);
    stage_set_view(VIEW_BOTH);
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
    for (int y = 0; y < out_h; y++) {
        int ry = row_index[y], wy = row_weight[y];
        const u32 *a = stage_px + (src_y + ry) * STAGE_W, *b = a + STAGE_W;
        u32 *dst = canvas + (out_y + y) * SW + out_x;
        for (int x = 0; x < out_w; x++) {
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

/* the rows the current view shows */
const u32 *stage_pixels(int *rows) { *rows = src_h; return stage_px + src_y * STAGE_W; }

void stage_rect(int *x, int *y, int *w, int *h)
{
    *x = out_x;
    *y = out_y;
    *w = out_w;
    *h = out_h;
}

int stage_from_canvas(int px, int py, float *sx, float *sy)
{
    *sx = (px - out_x + 0.5f) * STAGE_W / out_w;
    *sy = src_y + (py - out_y + 0.5f) * src_h / out_h;
    return *sx >= 0 && *sy >= src_y && *sx < STAGE_W && *sy < src_y + src_h;
}

void stage_to_canvas(float sx, float sy, int *px, int *py)
{
    *px = out_x + (int)(sx * out_w / STAGE_W);
    *py = out_y + (int)((sy - src_y) * out_h / src_h);
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
    if (img) stage_blit_multiply_region(img, 0, 0, img->w, img->h, x, y, tint, alpha);
}

void stage_blit_multiply_region(const image_t *img, int sx, int sy, int w, int h, int x, int y, u32 tint, int alpha)
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
