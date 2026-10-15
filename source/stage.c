#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "stage.h"

static u32 *stage_px;
static u32 *saved_canvas;
static int saved_w, saved_h;
static int out_w, out_h;

#define R(c) (((c) >> 16) & 0xFF)
#define G(c) (((c) >> 8) & 0xFF)
#define B(c) ((c) & 0xFF)

static int scale_mode = -1;
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

/* Each DS screen goes on its own LCD: 1.25x fills the bottom screen exactly
 * and the height of the top one; SCALE_PIXEL shows the DS pixels 1:1. */
void stage_set_scale(int mode)
{
    if (mode == scale_mode) return;
    scale_mode = mode;
    out_w = mode == SCALE_PIXEL ? STAGE_W : 320;
    out_h = mode == SCALE_PIXEL ? 192 : SCREEN_H;
}

int stage_scale(void) { return scale_mode; }

void stage_init(void)
{
    stage_px = malloc(STAGE_W * STAGE_H * 4);
    memset(stage_px, 0, STAGE_W * STAGE_H * 4);
    stage_set_scale(SCALE_FIT);
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

void stage_rect(int screen, int *x, int *y, int *w, int *h)
{
    *x = ((screen == SCREEN_TOP ? TOP_W : BOTTOM_W) - out_w) / 2;
    *y = (SCREEN_H - out_h) / 2;
    *w = out_w;
    *h = out_h;
}

void stage_present(int allow_dark) { gfx_stage(stage_px, allow_dark && stage_dark_on); }

int stage_from_touch(int tx, int ty, float *sx, float *sy)
{
    int ox, oy, ow, oh;
    stage_rect(SCREEN_BOTTOM, &ox, &oy, &ow, &oh);
    *sx = (tx - ox + 0.5f) * STAGE_W / ow;
    *sy = 192 + (ty - oy + 0.5f) * 192 / oh;
    return *sx >= 0 && *sy >= 192 && *sx < STAGE_W && *sy < STAGE_H;
}

/* ── Sprites ─────────────────────────────────────────────────────────────── */

static inline u32 tint_px(u32 c, u32 tint)
{
    if (tint == 0xFFFFFF) return c & 0xFFFFFF;
    return ((R(c) * R(tint) / 255) << 16) | ((G(c) * G(tint) / 255) << 8) | (B(c) * B(tint) / 255);
}

/* Which rows of a sprite are fully opaque: those are copied, not blended */
static const u8 *row_kinds(image_t *img)
{
    if (!img->row_kind && (img->row_kind = malloc(img->h))) {
        for (int y = 0; y < img->h; y++) {
            const u32 *p = img->px + y * img->w;
            int opaque = 1, clear = 1;
            for (int x = 0; x < img->w; x++) {
                u32 a = p[x] >> 24;
                if (a != 255) opaque = 0;
                if (a) clear = 0;
            }
            img->row_kind[y] = clear ? 0 : opaque ? 1 : 2;
        }
    }
    return img->row_kind;
}

static const u32 *flipped_pixels(image_t *img)
{
    if (!img->flipped && (img->flipped = malloc((size_t)img->w * img->h * 4)))
        for (int i = 0; i < img->w * img->h; i++) img->flipped[i] = (img->px[i] & 0xFF000000) | hue_flip(img->px[i]);
    return img->flipped ? img->flipped : img->px;
}

void stage_blit_region(const image_t *cimg, int sx, int sy, int w, int h, int x, int y, u32 tint, int alpha)
{
    image_t *img = (image_t *)cimg; /* the caches below are filled in on first use */
    if (!img || alpha <= 0) return;
    /* clip to the canvas and the image */
    if (sx < 0) w += sx, x -= sx, sx = 0;
    if (sy < 0) h += sy, y -= sy, sy = 0;
    if (x < 0) w += x, sx -= x, x = 0;
    if (y < 0) h += y, sy -= y, y = 0;
    if (sx + w > img->w) w = img->w - sx;
    if (sy + h > img->h) h = img->h - sy;
    if (x + w > SW) w = SW - x;
    if (y + h > SH) h = SH - y;
    if (w <= 0 || h <= 0) return;
    /* the hue flip comes before the tint, as PIXI does it */
    const u32 *px = flip && stage_dark_on ? flipped_pixels(img) : img->px;
    const u8 *kind = alpha >= 255 && tint == 0xFFFFFF ? row_kinds(img) : NULL;
    for (int j = 0; j < h; j++) {
        const u32 *src = px + (sy + j) * img->w + sx;
        u32 *row = canvas + (y + j) * SW + x;
        if (kind) {
            int k = kind[sy + j];
            if (k == 0) continue;
            if (k == 1) {
                memcpy(row, src, w * 4);
                continue;
            }
            for (int i = 0; i < w; i++) {
                u32 c = src[i], a = c >> 24;
                if (a == 255) row[i] = c;
                else if (a) row[i] = gfx_mix(row[i], c & 0xFFFFFF, a);
            }
            continue;
        }
        for (int i = 0; i < w; i++) {
            u32 c = src[i];
            int a = (c >> 24) * alpha / 255;
            if (!a) continue;
            u32 p = tint_px(c, tint);
            row[i] = a >= 255 ? p : gfx_mix(row[i], p, a);
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
    const u32 *px = flip && stage_dark_on ? flipped_pixels((image_t *)img) : img->px;
    for (int j = 0; j < img->h; j++) {
        int yy = y + j;
        if (yy < 0 || yy >= SH) continue;
        const u32 *src = px + j * img->w;
        u32 *row = canvas + yy * SW;
        for (int i = 0; i < img->w; i++) {
            int xx = x + i;
            if (xx < 0 || xx >= SW) continue;
            u32 c = src[i];
            int a = (c >> 24) * alpha / 255;
            if (!a) continue;
            u32 s = tint_px(c, tint), d = row[xx];
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
