#pragma once
#include <3ds/types.h>

/* Software canvases for the two LCDs, 400x240 on top and 320x240 below.
 * Colors are 0xRRGGBB; images are 0xAARRGGBB. The gfx_* calls draw into the
 * canvas chosen with gfx_target (or the DS stage, see stage.h). */

enum { SCREEN_TOP, SCREEN_BOTTOM };
#define TOP_W 400
#define BOTTOM_W 320
#define SCREEN_H 240

typedef struct image {
    int w, h;
    u32 *px;
    u32 *flipped;  /* the hue-flipped copy for dark mode, made when first needed */
    u8 *row_kind;  /* per row: 0 transparent, 1 opaque, 2 mixed (made when first needed) */
} image_t;

extern int SW, SH;
extern u32 *canvas;

void gfx_init(void);
void gfx_shutdown(void);
void gfx_target(int screen);         /* SCREEN_TOP or SCREEN_BOTTOM */
void gfx_canvas_changed(int screen); /* call after drawing into a canvas */
/* The DS stage (256x384) shown over the canvases this frame, scaled by the GPU; NULL for none */
void gfx_stage(const u32 *px, int invert);
void gfx_present(void);              /* draw both screens and wait for the next frame */

/* gfx_clip pushes a clip rectangle (nested inside the current one); gfx_noclip pops it */
void gfx_clip(int x, int y, int w, int h);
void gfx_noclip(void);

u32 gfx_mix(u32 a, u32 b, int alpha); /* alpha 0..255 of b over a */
void gfx_fill(int x, int y, int w, int h, u32 color);
void gfx_fill_alpha(int x, int y, int w, int h, u32 color, int alpha);
void gfx_round(int x, int y, int w, int h, int r, u32 color);
void gfx_frame(int x, int y, int w, int h, int r, u32 fill, u32 border, int thickness);
void gfx_grid(int x, int y, int w, int h, int step, u32 line);
void gfx_circle(int cx, int cy, int r, u32 color);
void gfx_ring(int cx, int cy, int r, int thickness, u32 color);

void gfx_mask(const u8 *alpha, int w, int h, int x, int y, u32 color);

void gfx_blit(const image_t *img, int x, int y);
void gfx_blit_alpha(const image_t *img, int x, int y, int alpha);
void gfx_stretch(const image_t *img, int x, int y, int w, int h);
/* repeat an image across a strip, scaled to the strip's height */
void gfx_tile(const image_t *img, int x, int y, int w, int h, int tile_w);

image_t *image_new(int w, int h);
void image_free(image_t *img);
image_t *image_resize(const image_t *src, int w, int h); /* area-averaged */
image_t *image_fit(const image_t *src, int max_w, int max_h);
