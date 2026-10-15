#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <malloc.h>
#include <SDL2/SDL.h>
#include "gfx.h"
#include "image.h"
#include "pointer_png.h"

/* The canvas is the Wii's 16:9 picture, 852x480: the GamePad shows it at its
 * own size (854x480) and the TV at 1280x720. One SDL window is copied to both. */
#define WIN_W 1280
#define WIN_H 720

int SW = 852, SH = 480;
u32 *canvas;

static SDL_Window *window;
static SDL_Renderer *renderer;
static int clip_x0, clip_y0, clip_x1, clip_y1;
static int canvas_dirty = 1;

/* The GPU draws the canvas, the layer on top of it and the pointer as textures;
 * it also does the scaling (layer zoom, TV and GamePad sizes). */
static SDL_Texture *canvas_tex;
static SDL_Texture *layer_tex;
static u32 *layer_px;
static int layer_w, layer_h, layer_on;
static float layer_x, layer_y, layer_dw, layer_dh;
static SDL_Texture *pointer_tex;
static int pointer_tw, pointer_th;

#define R(c) (((c) >> 16) & 0xFF)
#define G(c) (((c) >> 8) & 0xFF)
#define B(c) ((c) & 0xFF)

void gfx_init(void)
{
    window = SDL_CreateWindow("PictoChat", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, WIN_W, WIN_H, 0);
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    SDL_RenderSetScale(renderer, (float)WIN_W / SW, (float)WIN_H / SH);

    canvas = memalign(32, SW * SH * 4);
    memset(canvas, 0, SW * SH * 4);
    /* canvas pixels are 0x00RRGGBB; they are uploaded with an opaque alpha byte */
    canvas_tex = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, SW, SH);
    SDL_SetTextureBlendMode(canvas_tex, SDL_BLENDMODE_NONE);
    SDL_SetTextureScaleMode(canvas_tex, SDL_ScaleModeLinear);
    gfx_noclip();
}

void gfx_shutdown(void)
{
    if (pointer_tex) SDL_DestroyTexture(pointer_tex);
    if (layer_tex) SDL_DestroyTexture(layer_tex);
    if (canvas_tex) SDL_DestroyTexture(canvas_tex);
    pointer_tex = layer_tex = canvas_tex = NULL;
    free(layer_px);
    layer_px = NULL;
    if (renderer) SDL_DestroyRenderer(renderer);
    if (window) SDL_DestroyWindow(window);
    renderer = NULL;
    window = NULL;
}

int gfx_widescreen(void) { return 1; }

/* ── Cursor ──────────────────────────────────────────────────────────────── */
/* The Wii pointer hand (Player 1) from PrimmR's Wii Pointer Cursors, https://primmr.dev/projects/wii-pointer-cursors/ */
#define CURSOR_HOT_X 10
#define CURSOR_HOT_Y 1
#define UNDER_MAX 64
static image_t *pointer_img;
static int pointer_tried;

static void draw_texture(SDL_Texture *tex, float x, float y, float dw, float dh)
{
    SDL_FRect dst = { x, y, dw, dh };
    SDL_RenderCopyF(renderer, tex, NULL, &dst);
}

static void pointer_texture(void)
{
    if (!pointer_tried) {
        pointer_tried = 1;
        pointer_img = image_decode(pointer_png, pointer_png_size, 128);
        if (pointer_img && (pointer_img->w > UNDER_MAX || pointer_img->h > UNDER_MAX)) {
            image_free(pointer_img);
            pointer_img = NULL;
        }
    }
    if (pointer_tex || !pointer_img) return;
    pointer_tw = pointer_img->w;
    pointer_th = pointer_img->h;
    pointer_tex = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, pointer_tw, pointer_th);
    if (!pointer_tex) return;
    SDL_UpdateTexture(pointer_tex, NULL, pointer_img->px, pointer_tw * 4);
    SDL_SetTextureBlendMode(pointer_tex, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(pointer_tex, SDL_ScaleModeLinear);
}

void gfx_canvas_changed(void) { canvas_dirty = 1; }

void gfx_layer(const u32 *px, int w, int h, float x, float y, float dw, float dh, int invert)
{
    if (!px) {
        layer_on = 0;
        return;
    }
    if (!layer_tex || layer_w != w || layer_h != h) {
        if (layer_tex) SDL_DestroyTexture(layer_tex);
        free(layer_px);
        layer_tex = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, w * 2, h * 2);
        layer_px = malloc(w * 2 * h * 2 * 4);
        if (!layer_tex || !layer_px) {
            layer_on = 0;
            layer_w = layer_h = 0;
            return;
        }
        SDL_SetTextureBlendMode(layer_tex, SDL_BLENDMODE_NONE);
        SDL_SetTextureScaleMode(layer_tex, SDL_ScaleModeLinear);
        layer_w = w;
        layer_h = h;
    }
    /* doubled with nearest, then scaled by the GPU with linear filtering:
     * sharp pixels without the uneven widths of plain nearest scaling */
    u32 inv = invert ? 0xFFFFFF : 0;
    int dst_w = w * 2;
    for (int y = 0; y < h; y++) {
        const u32 *s = px + y * w;
        u32 *d0 = layer_px + y * 2 * dst_w, *d1 = d0 + dst_w;
        for (int x = 0; x < w; x++) {
            u32 c = (s[x] ^ inv) | 0xFF000000;
            d0[x * 2] = d0[x * 2 + 1] = d1[x * 2] = d1[x * 2 + 1] = c;
        }
    }
    SDL_UpdateTexture(layer_tex, NULL, layer_px, dst_w * 4);
    layer_on = 1;
    layer_x = x;
    layer_y = y;
    layer_dw = dw;
    layer_dh = dh;
}

#ifdef AUTOTEST
/* Screenshots: the canvas with the layer scaled in (nearest), as the TV shows it */
static void autotest_compose(void)
{
    extern int autotest_shot_pending(void);
    extern void autotest_save(const u32 *px, int w, int h);
    if (!autotest_shot_pending()) return;
    u32 *rgb = malloc(SW * SH * 4);
    if (!rgb) return;
    memcpy(rgb, canvas, SW * SH * 4);
    if (layer_on && layer_dw > 0 && layer_dh > 0) {
        int lw = layer_w * 2;
        for (int y = 0; y < (int)layer_dh; y++) {
            int yy = (int)layer_y + y;
            if (yy < 0 || yy >= SH) continue;
            const u32 *row = layer_px + (int)(y * layer_h * 2 / layer_dh) * lw;
            for (int x = 0; x < (int)layer_dw; x++) {
                int xx = (int)layer_x + x;
                if (xx >= 0 && xx < SW) rgb[yy * SW + xx] = row[(int)(x * lw / layer_dw)];
            }
        }
    }
    autotest_save(rgb, SW, SH);
    free(rgb);
}
#endif

void gfx_present(int cursor, int cx, int cy)
{
    if (canvas_dirty) {
        void *dst;
        int pitch;
        if (!SDL_LockTexture(canvas_tex, NULL, &dst, &pitch)) {
            for (int y = 0; y < SH; y++) {
                const u32 *s = canvas + y * SW;
                u32 *d = (u32 *)((u8 *)dst + y * pitch);
                for (int x = 0; x < SW; x++) d[x] = s[x] | 0xFF000000;
            }
            SDL_UnlockTexture(canvas_tex);
        }
        canvas_dirty = 0;
    }
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    draw_texture(canvas_tex, 0, 0, SW, SH);
    if (layer_on) draw_texture(layer_tex, layer_x, layer_y, layer_dw, layer_dh);
    if (cursor) {
        pointer_texture();
        if (pointer_tex) draw_texture(pointer_tex, cx - CURSOR_HOT_X, cy - CURSOR_HOT_Y, pointer_tw, pointer_th);
    }
#ifdef AUTOTEST
    autotest_compose();
#endif
    SDL_RenderPresent(renderer);
}

/* ── Primitives ──────────────────────────────────────────────────────────── */

/* Clip rectangles nest: each one is intersected with the one around it. */
static int clip_stack[16][4];
static int clip_depth;

void gfx_clip(int x, int y, int w, int h)
{
    if (clip_depth < 16) {
        clip_stack[clip_depth][0] = clip_x0;
        clip_stack[clip_depth][1] = clip_y0;
        clip_stack[clip_depth][2] = clip_x1;
        clip_stack[clip_depth][3] = clip_y1;
        clip_depth++;
    }
    if (x > clip_x0) clip_x0 = x;
    if (y > clip_y0) clip_y0 = y;
    if (x + w < clip_x1) clip_x1 = x + w;
    if (y + h < clip_y1) clip_y1 = y + h;
    if (clip_x1 < clip_x0) clip_x1 = clip_x0;
    if (clip_y1 < clip_y0) clip_y1 = clip_y0;
}

void gfx_noclip(void)
{
    if (clip_depth > 0) {
        clip_depth--;
        clip_x0 = clip_stack[clip_depth][0];
        clip_y0 = clip_stack[clip_depth][1];
        clip_x1 = clip_stack[clip_depth][2];
        clip_y1 = clip_stack[clip_depth][3];
    } else {
        clip_x0 = clip_y0 = 0;
        clip_x1 = SW;
        clip_y1 = SH;
    }
}

u32 gfx_mix(u32 a, u32 b, int alpha)
{
    if (alpha <= 0) return a;
    if (alpha >= 255) return b;
    /* (x * 255 + 128) / 255 without the divisions */
    u32 w = alpha + (alpha >> 7), iw = 256 - w;
    u32 rb = ((a & 0xFF00FF) * iw + (b & 0xFF00FF) * w) >> 8;
    u32 g = ((a & 0x00FF00) * iw + (b & 0x00FF00) * w) >> 8;
    return (rb & 0xFF00FF) | (g & 0x00FF00);
}

static inline void plot(int x, int y, u32 color, int alpha)
{
    if (x < clip_x0 || y < clip_y0 || x >= clip_x1 || y >= clip_y1 || alpha <= 0) return;
    u32 *p = &canvas[y * SW + x];
    *p = alpha >= 255 ? color : gfx_mix(*p, color, alpha);
}

void gfx_fill(int x, int y, int w, int h, u32 color)
{
    int x0 = x < clip_x0 ? clip_x0 : x, y0 = y < clip_y0 ? clip_y0 : y;
    int x1 = x + w > clip_x1 ? clip_x1 : x + w, y1 = y + h > clip_y1 ? clip_y1 : y + h;
    for (int j = y0; j < y1; j++) {
        u32 *p = &canvas[j * SW + x0];
        for (int i = x0; i < x1; i++) *p++ = color;
    }
}

void gfx_fill_alpha(int x, int y, int w, int h, u32 color, int alpha)
{
    int x0 = x < clip_x0 ? clip_x0 : x, y0 = y < clip_y0 ? clip_y0 : y;
    int x1 = x + w > clip_x1 ? clip_x1 : x + w, y1 = y + h > clip_y1 ? clip_y1 : y + h;
    for (int j = y0; j < y1; j++) {
        u32 *p = &canvas[j * SW + x0];
        for (int i = x0; i < x1; i++, p++) *p = gfx_mix(*p, color, alpha);
    }
}

static int coverage(float dx, float dy, float r)
{
    float d = sqrtf(dx * dx + dy * dy);
    float c = r - d + 0.5f;
    if (c <= 0) return 0;
    if (c >= 1) return 255;
    return (int)(c * 255);
}

void gfx_round(int x, int y, int w, int h, int r, u32 color)
{
    if (w <= 0 || h <= 0) return;
    if (r * 2 > w) r = w / 2;
    if (r * 2 > h) r = h / 2;
    if (r <= 0) {
        gfx_fill(x, y, w, h, color);
        return;
    }
    gfx_fill(x, y + r, w, h - 2 * r, color);
    for (int j = 0; j < r; j++) {
        float dy = r - (j + 0.5f);
        gfx_fill(x + r, y + j, w - 2 * r, 1, color);
        gfx_fill(x + r, y + h - 1 - j, w - 2 * r, 1, color);
        for (int i = 0; i < r; i++) {
            int a = coverage(r - (i + 0.5f), dy, r);
            plot(x + i, y + j, color, a);
            plot(x + w - 1 - i, y + j, color, a);
            plot(x + i, y + h - 1 - j, color, a);
            plot(x + w - 1 - i, y + h - 1 - j, color, a);
        }
    }
}

void gfx_frame(int x, int y, int w, int h, int r, u32 fill, u32 border, int t)
{
    gfx_round(x, y, w, h, r, border);
    gfx_round(x + t, y + t, w - 2 * t, h - 2 * t, r > t ? r - t : 0, fill);
}

void gfx_grid(int x, int y, int w, int h, int step, u32 line)
{
    for (int j = 0; j < h; j += step) gfx_fill(x, y + j, w, 1, line);
    for (int i = 0; i < w; i += step) gfx_fill(x + i, y, 1, h, line);
}

void gfx_circle(int cx, int cy, int r, u32 color)
{
    for (int j = -r - 1; j <= r; j++)
        for (int i = -r - 1; i <= r; i++) plot(cx + i, cy + j, color, coverage(i + 0.5f, j + 0.5f, r));
}

void gfx_ring(int cx, int cy, int r, int t, u32 color)
{
    for (int j = -r - 1; j <= r; j++)
        for (int i = -r - 1; i <= r; i++) {
            int outer = coverage(i + 0.5f, j + 0.5f, r);
            int inner = coverage(i + 0.5f, j + 0.5f, r - t);
            plot(cx + i, cy + j, color, outer - inner);
        }
}

void gfx_mask(const u8 *alpha, int w, int h, int x, int y, u32 color)
{
    for (int j = 0; j < h; j++) {
        int yy = y + j;
        if (yy < clip_y0 || yy >= clip_y1) continue;
        const u8 *a = alpha + j * w;
        u32 *row = canvas + yy * SW;
        for (int i = 0; i < w; i++) {
            int xx = x + i;
            if (!a[i] || xx < clip_x0 || xx >= clip_x1) continue;
            row[xx] = a[i] == 255 ? color : gfx_mix(row[xx], color, a[i]);
        }
    }
}

/* ── Images ──────────────────────────────────────────────────────────────── */

image_t *image_new(int w, int h)
{
    image_t *img = malloc(sizeof(image_t));
    if (!img) return NULL;
    img->w = w;
    img->h = h;
    img->px = malloc((size_t)w * h * 4);
    if (!img->px) {
        free(img);
        return NULL;
    }
    return img;
}

void image_free(image_t *img)
{
    if (!img) return;
    free(img->px);
    free(img);
}

void gfx_blit_alpha(const image_t *img, int x, int y, int alpha)
{
    if (!img) return;
    for (int j = 0; j < img->h; j++) {
        int yy = y + j;
        if (yy < clip_y0 || yy >= clip_y1) continue;
        const u32 *s = img->px + j * img->w;
        for (int i = 0; i < img->w; i++) {
            int xx = x + i;
            if (xx < clip_x0 || xx >= clip_x1) continue;
            u32 c = s[i];
            int a = ((c >> 24) * alpha) / 255;
            if (a) canvas[yy * SW + xx] = a >= 255 ? (c & 0xFFFFFF) : gfx_mix(canvas[yy * SW + xx], c & 0xFFFFFF, a);
        }
    }
}

void gfx_blit(const image_t *img, int x, int y) { gfx_blit_alpha(img, x, y, 255); }

void gfx_stretch(const image_t *img, int x, int y, int w, int h)
{
    if (!img || w <= 0 || h <= 0) return;
    for (int j = 0; j < h; j++) {
        int yy = y + j;
        if (yy < clip_y0 || yy >= clip_y1) continue;
        const u32 *s = img->px + (j * img->h / h) * img->w;
        for (int i = 0; i < w; i++) {
            int xx = x + i;
            if (xx < clip_x0 || xx >= clip_x1) continue;
            u32 c = s[i * img->w / w];
            int a = c >> 24;
            if (a) canvas[yy * SW + xx] = a == 255 ? (c & 0xFFFFFF) : gfx_mix(canvas[yy * SW + xx], c & 0xFFFFFF, a);
        }
    }
}

void gfx_tile(const image_t *img, int x, int y, int w, int h, int tile_w)
{
    if (!img) return;
    for (int i = 0; i < w; i += tile_w) {
        int cw = w - i < tile_w ? w - i : tile_w;
        int ox = clip_x1;
        if (x + i + cw < clip_x1) clip_x1 = x + i + cw;
        gfx_stretch(img, x + i, y, tile_w, h);
        clip_x1 = ox;
    }
}

image_t *image_resize(const image_t *src, int w, int h)
{
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    image_t *dst = image_new(w, h);
    if (!dst) return NULL;
    for (int j = 0; j < h; j++) {
        int sy0 = j * src->h / h, sy1 = (j + 1) * src->h / h;
        if (sy1 <= sy0) sy1 = sy0 + 1;
        for (int i = 0; i < w; i++) {
            int sx0 = i * src->w / w, sx1 = (i + 1) * src->w / w;
            if (sx1 <= sx0) sx1 = sx0 + 1;
            u32 a = 0, r = 0, g = 0, b = 0, n = 0;
            for (int y = sy0; y < sy1; y++)
                for (int x = sx0; x < sx1; x++) {
                    u32 c = src->px[y * src->w + x];
                    a += c >> 24;
                    r += (c >> 16) & 0xFF;
                    g += (c >> 8) & 0xFF;
                    b += c & 0xFF;
                    n++;
                }
            dst->px[j * w + i] = ((a / n) << 24) | ((r / n) << 16) | ((g / n) << 8) | (b / n);
        }
    }
    return dst;
}

image_t *image_fit(const image_t *src, int max_w, int max_h)
{
    float s = fminf((float)max_w / src->w, (float)max_h / src->h);
    if (s > 1) s = 1;
    return image_resize(src, (int)(src->w * s + 0.5f), (int)(src->h * s + 0.5f));
}
