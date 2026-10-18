#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <3ds.h>
#include <citro2d.h>
#include "gfx.h"
#include "stage.h"

int SW = TOP_W, SH = SCREEN_H;
u32 *canvas;

static int clip_x0, clip_y0, clip_x1, clip_y1;

#define R(c) (((c) >> 16) & 0xFF)
#define G(c) (((c) >> 8) & 0xFF)
#define B(c) ((c) & 0xFF)

/* Each LCD has a software canvas (0xRRGGBB) for what is around the DS screens;
 * it is uploaded to a texture when it changes. The DS stage is uploaded every
 * frame at twice its size with nearest neighbour and scaled down by the GPU
 * with linear filtering, as on the Wii: sharp pixels without the uneven widths
 * of plain nearest scaling. */
static u32 *screens[2];
static int screen_dirty[2] = { 1, 1 };
static C3D_RenderTarget *targets[2];
static C3D_Tex canvas_tex[2], stage_tex[2];
static Tex3DS_SubTexture canvas_sub[2], stage_sub;
static const u32 *stage_src;
static int stage_invert;

#define STAGE_TEX 512 /* one DS screen at 2x: 512x384 in a 512x512 texture */

void gfx_init(void)
{
    gfxInitDefault();
    gfxSet3D(false);
    C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
    C2D_Init(C2D_DEFAULT_MAX_OBJECTS);
    C2D_Prepare();
    targets[SCREEN_TOP] = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
    targets[SCREEN_BOTTOM] = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);
    for (int i = 0; i < 2; i++) {
        int w = i == SCREEN_TOP ? TOP_W : BOTTOM_W;
        screens[i] = calloc(w * SCREEN_H, 4);
        C3D_TexInit(&canvas_tex[i], 512, 256, GPU_RGB565);
        C3D_TexSetFilter(&canvas_tex[i], GPU_NEAREST, GPU_NEAREST);
        canvas_sub[i] = (Tex3DS_SubTexture){ w, SCREEN_H, 0.0f, 1.0f, w / 512.0f, 1.0f - SCREEN_H / 256.0f };
        C3D_TexInit(&stage_tex[i], STAGE_TEX, STAGE_TEX, GPU_RGB565);
        C3D_TexSetFilter(&stage_tex[i], GPU_LINEAR, GPU_LINEAR);
    }
    stage_sub = (Tex3DS_SubTexture){ STAGE_TEX, 384, 0.0f, 1.0f, 1.0f, 1.0f - 384.0f / STAGE_TEX };
    gfx_target(SCREEN_TOP);
}

void gfx_shutdown(void)
{
    for (int i = 0; i < 2; i++) {
        C3D_TexDelete(&canvas_tex[i]);
        C3D_TexDelete(&stage_tex[i]);
    }
    C2D_Fini();
    C3D_Fini();
    gfxExit();
}

void gfx_target(int screen)
{
    canvas = screens[screen];
    SW = screen == SCREEN_TOP ? TOP_W : BOTTOM_W;
    SH = SCREEN_H;
    clip_x0 = clip_y0 = 0;
    clip_x1 = SW;
    clip_y1 = SH;
}

void gfx_canvas_changed(int screen) { screen_dirty[screen] = 1; }

void gfx_stage(const u32 *px, int invert)
{
    stage_src = px;
    stage_invert = invert;
}

static inline u16 rgb565(u32 c) { return ((c >> 8) & 0xF800) | ((c >> 5) & 0x07E0) | ((c >> 3) & 0x001F); }

/* Texel (x, y) of a tiled texture (y from the top, which is where the
 * subtextures' top = 1.0 points): 8x8 tiles, Morton order inside. */
static inline u32 morton(u32 x, u32 y)
{
    return (x & 1) | (y & 1) << 1 | (x & 2) << 1 | (y & 2) << 2 | (x & 4) << 2 | (y & 4) << 3;
}

static inline u32 tiled(int x, int y, int tw)
{
    return ((y >> 3) * (tw >> 3) + (x >> 3)) * 64 + morton(x & 7, y & 7);
}

static void upload_canvas(int screen)
{
    int w = screen == SCREEN_TOP ? TOP_W : BOTTOM_W;
    u16 *dst = canvas_tex[screen].data;
    const u32 *src = screens[screen];
    for (int y = 0; y < SCREEN_H; y++)
        for (int x = 0; x < w; x++) dst[tiled(x, y, 512)] = rgb565(src[y * w + x]);
    C3D_TexFlush(&canvas_tex[screen]);
}

/* One DS screen (192 rows of the stage) at 2x: each stage pixel becomes an
 * aligned 2x2 block, which is 4 consecutive texels in Morton order. */
static u32 *stage_prev; /* what the stage textures hold */
static int stage_shown_invert = -1;

/* Upload the stage rows that changed since the last frame; returns how many */
static int upload_stage(int half)
{
    static u32 col[STAGE_W]; /* where each stage column's block starts within a tile row, in u64s */
    if (!col[1])
        for (int x = 0; x < STAGE_W; x++) col[x] = ((2 * x >> 3) * 64 + morton(2 * x & 7, 0)) >> 2;
    if (!stage_prev && !(stage_prev = malloc(STAGE_W * 384 * 4))) return 0;
    int all = stage_invert != stage_shown_invert, changed = 0;
    u64 *dst = stage_tex[half].data;
    const u32 *src = stage_src + half * 192 * STAGE_W;
    u32 *prev = stage_prev + half * 192 * STAGE_W;
    u16 inv = stage_invert ? 0xFFFF : 0;
    for (int y = 0; y < 192; y++, src += STAGE_W, prev += STAGE_W) {
        if (!all && !memcmp(src, prev, STAGE_W * 4)) continue;
        memcpy(prev, src, STAGE_W * 4);
        changed++;
        int ty = 2 * y; /* the block's first texture row */
        u64 *row = dst + (((ty >> 3) * (STAGE_TEX >> 3) * 64 + morton(0, ty & 7)) >> 2);
        for (int x = 0; x < STAGE_W; x++) {
            u64 c = rgb565(src[x]) ^ inv;
            c |= c << 16;
            row[col[x]] = c | c << 32;
        }
    }
    if (changed) C3D_TexFlush(&stage_tex[half]);
    return changed;
}

#ifdef PROFILE
u64 prof_upload;
#endif
void gfx_present(void)
{
    /* nothing new on either screen: the LCDs keep showing the last frame */
    static int last_layout = -1;
    int x, y, w, h;
    stage_rect(SCREEN_TOP, &x, &y, &w, &h);
    int layout = (stage_src != NULL) | w << 1;
    int changed = screen_dirty[0] || screen_dirty[1] || layout != last_layout;
    if (!changed && stage_src)
        for (int i = 0; i < 384 && !changed; i++) changed = memcmp(stage_src + i * STAGE_W, stage_prev + i * STAGE_W, STAGE_W * 4) != 0;
    if (!changed && stage_invert == stage_shown_invert) {
        gspWaitForVBlank();
        return;
    }
    last_layout = layout;

    C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
#ifdef PROFILE
    u64 t = svcGetSystemTick();
#endif
    for (int i = 0; i < 2; i++) {
        if (screen_dirty[i]) upload_canvas(i);
        screen_dirty[i] = 0;
        if (stage_src) upload_stage(i);
    }
    stage_shown_invert = stage_src ? stage_invert : -1;
#ifdef PROFILE
    prof_upload += svcGetSystemTick() - t;
#endif
    for (int i = 0; i < 2; i++) {
        C2D_TargetClear(targets[i], C2D_Color32(0, 0, 0, 255));
        C2D_SceneBegin(targets[i]);
        C2D_DrawImageAt((C2D_Image){ &canvas_tex[i], &canvas_sub[i] }, 0, 0, 0, NULL, 1, 1);
        if (stage_src) {
            stage_rect(i, &x, &y, &w, &h);
            C2D_DrawImageAt((C2D_Image){ &stage_tex[i], &stage_sub }, x, y, 0.5f, NULL, w / (float)STAGE_TEX,
                            h / 384.0f);
        }
    }
    C3D_FrameEnd(0);
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
    img->flipped = NULL;
    img->row_kind = NULL;
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
    free(img->flipped);
    free(img->row_kind);
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
