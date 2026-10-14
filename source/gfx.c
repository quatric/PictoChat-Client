#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <malloc.h>
#include "gfx.h"
#include "image.h"
#include "pointer_png.h"

int SW = 640, SH = 480;
u32 *canvas;

static GXRModeObj *rmode;
static void *xfb[2];
static int which;
static int wide;
static int clip_x0, clip_y0, clip_x1, clip_y1;
static int canvas_dirty = 1;

/* GX draws the canvas, the layer on top of it and the pointer as textured
 * quads; it also does the scaling (16:9 squeeze, layer zoom) and the TV copy. */
#define FIFO_SIZE (256 * 1024)
static void *fifo;
static u16 *canvas_tex;
static u16 *layer_tex;
static int layer_w, layer_h, layer_on;
static float layer_x, layer_y, layer_dw, layer_dh;
static u8 *pointer_tex;
static int pointer_tw, pointer_th;

#define R(c) (((c) >> 16) & 0xFF)
#define G(c) (((c) >> 8) & 0xFF)
#define B(c) ((c) & 0xFF)

void gfx_init(void)
{
    VIDEO_Init();
    rmode = VIDEO_GetPreferredMode(NULL);
    wide = CONF_GetAspectRatio() == CONF_ASPECT_16_9;
    if (wide) { /* use the full width of a 16:9 picture */
        rmode->viWidth = 678;
        rmode->viXOrigin = (VI_MAX_WIDTH_NTSC - rmode->viWidth) / 2;
    }

    SW = wide ? 852 : 640;
    SH = 480;
    canvas = memalign(32, SW * SH * 4);
    memset(canvas, 0, SW * SH * 4);
    canvas_tex = memalign(32, SW * SH * 2);

    for (int i = 0; i < 2; i++) {
        xfb[i] = MEM_K0_TO_K1(SYS_AllocateFramebuffer(rmode));
        VIDEO_ClearFrameBuffer(rmode, xfb[i], COLOR_BLACK);
    }
    VIDEO_Configure(rmode);
    VIDEO_SetNextFramebuffer(xfb[0]);
    VIDEO_SetBlack(FALSE);
    VIDEO_Flush();
    VIDEO_WaitVSync();
    if (rmode->viTVMode & VI_NON_INTERLACE) VIDEO_WaitVSync();

    fifo = memalign(32, FIFO_SIZE);
    memset(fifo, 0, FIFO_SIZE);
    GX_Init(fifo, FIFO_SIZE);
    GX_SetCopyClear((GXColor){ 0, 0, 0, 255 }, GX_MAX_Z24);
    GX_SetViewport(0, 0, rmode->fbWidth, rmode->efbHeight, 0, 1);
    f32 yscale = GX_GetYScaleFactor(rmode->efbHeight, rmode->xfbHeight);
    u32 xfb_height = GX_SetDispCopyYScale(yscale);
    GX_SetScissor(0, 0, rmode->fbWidth, rmode->efbHeight);
    GX_SetDispCopySrc(0, 0, rmode->fbWidth, rmode->efbHeight);
    GX_SetDispCopyDst(rmode->fbWidth, xfb_height);
    GX_SetCopyFilter(rmode->aa, rmode->sample_pattern, GX_TRUE, rmode->vfilter);
    GX_SetFieldMode(rmode->field_rendering, rmode->viHeight == 2 * rmode->xfbHeight ? GX_ENABLE : GX_DISABLE);
    GX_SetPixelFmt(rmode->aa ? GX_PF_RGB565_Z16 : GX_PF_RGB8_Z24, GX_ZC_LINEAR);
    GX_SetCullMode(GX_CULL_NONE);
    GX_SetDispCopyGamma(GX_GM_1_0);

    Mtx44 proj;
    guOrtho(proj, 0, SH, 0, SW, 0, 1); /* canvas pixels; GX squeezes 852 into the 16:9 picture */
    GX_LoadProjectionMtx(proj, GX_ORTHOGRAPHIC);
    Mtx view;
    guMtxIdentity(view);
    GX_LoadPosMtxImm(view, GX_PNMTX0);
    GX_ClearVtxDesc();
    GX_SetVtxDesc(GX_VA_POS, GX_DIRECT);
    GX_SetVtxDesc(GX_VA_TEX0, GX_DIRECT);
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XY, GX_F32, 0);
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);
    GX_SetNumChans(0);
    GX_SetNumTexGens(1);
    GX_SetTexCoordGen(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY);
    GX_SetNumTevStages(1);
    GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLORNULL);
    GX_SetTevOp(GX_TEVSTAGE0, GX_REPLACE);
    GX_SetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);
    GX_SetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
    GX_SetAlphaUpdate(GX_TRUE);
    GX_SetColorUpdate(GX_TRUE);
    gfx_noclip();
}

void gfx_shutdown(void)
{
    GX_DrawDone();
    GX_AbortFrame();
    VIDEO_SetBlack(TRUE);
    VIDEO_Flush();
    VIDEO_WaitVSync();
}

int gfx_widescreen(void) { return wide; }

/* ── Cursor ──────────────────────────────────────────────────────────────── */
/* The Wii pointer hand (Player 1) from PrimmR's Wii Pointer Cursors, https://primmr.dev/projects/wii-pointer-cursors/ */
#define CURSOR_HOT_X 10
#define CURSOR_HOT_Y 1
#define UNDER_MAX 64
static image_t *pointer_img;
static int pointer_tried;

#ifdef AUTOTEST /* screenshots need the pointer drawn into the canvas */
static u32 under[UNDER_MAX * UNDER_MAX];
static void cursor_draw(int cx, int cy, int save)
{
    if (!pointer_tried) {
        pointer_tried = 1;
        pointer_img = image_decode(pointer_png, pointer_png_size, 128);
        if (pointer_img && (pointer_img->w > UNDER_MAX || pointer_img->h > UNDER_MAX)) {
            image_free(pointer_img);
            pointer_img = NULL;
        }
    }
    int w = pointer_img ? pointer_img->w : 12, h = pointer_img ? pointer_img->h : 12;
    int x0 = cx - CURSOR_HOT_X, y0 = cy - CURSOR_HOT_Y;
    for (int j = 0; j < h; j++) {
        int y = y0 + j;
        if (y < 0 || y >= SH) continue;
        for (int i = 0; i < w; i++) {
            int x = x0 + i;
            if (x < 0 || x >= SW) continue;
            u32 *p = &canvas[y * SW + x];
            if (save == 1) under[j * UNDER_MAX + i] = *p;
            else if (save == 2) {
                *p = under[j * UNDER_MAX + i];
                continue;
            }
            if (!pointer_img) {
                *p = 0xFFFFFF;
                continue;
            }
            u32 c = pointer_img->px[j * w + i];
            int a = c >> 24;
            if (a) *p = gfx_mix(*p, c & 0xFFFFFF, a);
        }
    }
}
#endif

static inline u16 rgb565(u32 c) { return ((c >> 8) & 0xF800) | ((c >> 5) & 0x07E0) | ((c >> 3) & 0x001F); }

/* 0xRRGGBB pixels -> GX RGB565 texture (4x4 tiles) */
static void to_rgb565(const u32 *src, int w, int h, u16 *dst)
{
    u16 *out = dst;
    for (int by = 0; by < h; by += 4)
        for (int bx = 0; bx < w; bx += 4)
            for (int y = by; y < by + 4; y++) {
                const u32 *p = src + y * w + bx;
                out[0] = rgb565(p[0]);
                out[1] = rgb565(p[1]);
                out[2] = rgb565(p[2]);
                out[3] = rgb565(p[3]);
                out += 4;
            }
    DCFlushRange(dst, w * h * 2);
}

/* The same at twice the size: each 4x4 tile holds 2x2 source pixels */
static void to_rgb565_2x(const u32 *src, int w, int h, u16 *dst)
{
    u16 *out = dst;
    for (int sy = 0; sy < h; sy += 2)
        for (int sx = 0; sx < w; sx += 2) {
            const u32 *p = src + sy * w + sx;
            u16 a = rgb565(p[0]), b = rgb565(p[1]), c = rgb565(p[w]), d = rgb565(p[w + 1]);
            out[0] = out[1] = out[4] = out[5] = a;
            out[2] = out[3] = out[6] = out[7] = b;
            out[8] = out[9] = out[12] = out[13] = c;
            out[10] = out[11] = out[14] = out[15] = d;
            out += 16;
        }
    DCFlushRange(dst, w * h * 8);
}

static void quad(float x, float y, float w, float h)
{
    GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
    GX_Position2f32(x, y);
    GX_TexCoord2f32(0, 0);
    GX_Position2f32(x + w, y);
    GX_TexCoord2f32(1, 0);
    GX_Position2f32(x + w, y + h);
    GX_TexCoord2f32(1, 1);
    GX_Position2f32(x, y + h);
    GX_TexCoord2f32(0, 1);
    GX_End();
}

static void draw_texture(void *data, int w, int h, u8 format, int linear, float x, float y, float dw, float dh)
{
    GXTexObj tex;
    GX_InitTexObj(&tex, data, w, h, format, GX_CLAMP, GX_CLAMP, GX_FALSE);
    GX_InitTexObjLOD(&tex, linear ? GX_LINEAR : GX_NEAR, linear ? GX_LINEAR : GX_NEAR, 0, 0, 0, GX_FALSE, GX_FALSE, GX_ANISO_1);
    GX_LoadTexObj(&tex, GX_TEXMAP0);
    quad(x, y, dw, dh);
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
    pointer_tw = (pointer_img->w + 3) & ~3;
    pointer_th = (pointer_img->h + 3) & ~3;
    pointer_tex = memalign(32, pointer_tw * pointer_th * 4);
    /* RGBA8: each 4x4 tile is 32 bytes of AR pairs, then 32 bytes of GB pairs */
    u8 *out = pointer_tex;
    for (int by = 0; by < pointer_th; by += 4)
        for (int bx = 0; bx < pointer_tw; bx += 4) {
            for (int y = by; y < by + 4; y++)
                for (int x = bx; x < bx + 4; x++) {
                    u32 c = x < pointer_img->w && y < pointer_img->h ? pointer_img->px[y * pointer_img->w + x] : 0;
                    *out++ = c >> 24;
                    *out++ = c >> 16;
                }
            for (int y = by; y < by + 4; y++)
                for (int x = bx; x < bx + 4; x++) {
                    u32 c = x < pointer_img->w && y < pointer_img->h ? pointer_img->px[y * pointer_img->w + x] : 0;
                    *out++ = c >> 8;
                    *out++ = c;
                }
        }
    DCFlushRange(pointer_tex, pointer_tw * pointer_th * 4);
}

void gfx_canvas_changed(void) { canvas_dirty = 1; }

void gfx_layer(const u32 *px, int w, int h, float x, float y, float dw, float dh)
{
    if (!px) {
        layer_on = 0;
        return;
    }
    if (!layer_tex || layer_w != w || layer_h != h) {
        free(layer_tex);
        layer_tex = memalign(32, w * 2 * h * 2 * 2);
        layer_w = w;
        layer_h = h;
    }
    /* doubled with nearest, then scaled down by GX with linear filtering:
     * sharp pixels without the uneven widths of plain nearest scaling */
    to_rgb565_2x(px, w, h, layer_tex);
    layer_on = 1;
    layer_x = x;
    layer_y = y;
    layer_dw = dw;
    layer_dh = dh;
}

void gfx_present(int cursor, int cx, int cy)
{
#ifdef AUTOTEST
    /* screenshots are taken from the canvas, so draw the pointer into it */
    if (cursor) cursor_draw(cx, cy, 1);
    { extern void autotest_after_draw(void); autotest_after_draw(); }
    if (cursor) cursor_draw(cx, cy, 2);
#endif
    if (canvas_dirty) {
        to_rgb565(canvas, SW, SH, canvas_tex);
        canvas_dirty = 0;
    }
    GX_InvalidateTexAll();
    draw_texture(canvas_tex, SW, SH, GX_TF_RGB565, 1, 0, 0, SW, SH);
    if (layer_on) draw_texture(layer_tex, layer_w * 2, layer_h * 2, GX_TF_RGB565, 1, layer_x, layer_y, layer_dw, layer_dh);
    if (cursor) {
        pointer_texture();
        if (pointer_tex)
            draw_texture(pointer_tex, pointer_tw, pointer_th, GX_TF_RGBA8, 1, cx - CURSOR_HOT_X, cy - CURSOR_HOT_Y,
                         pointer_tw, pointer_th);
    }
    GX_DrawDone();

    which ^= 1;
    GX_SetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
    GX_SetColorUpdate(GX_TRUE);
    GX_CopyDisp(xfb[which], GX_TRUE);
    GX_Flush();
    VIDEO_SetNextFramebuffer(xfb[which]);
    VIDEO_Flush();
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
