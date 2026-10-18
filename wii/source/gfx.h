#pragma once
#include <gccore.h>

/* Software canvas, presented to the TV through the external framebuffer.
 * Colors are 0xRRGGBB; images are 0xAARRGGBB. In 16:9 mode the canvas is
 * wider (852 px) and squeezed back to 640 so nothing looks stretched. */

typedef struct image {
    int w, h;
    u32 *px;
} image_t;

extern int SW, SH;
extern u32 *canvas;

void gfx_init(void);
void gfx_shutdown(void);
int gfx_widescreen(void);
void gfx_present(int cursor, int cx, int cy);
/* Call after drawing into the canvas: it is only re-uploaded to the GPU when it changed */
void gfx_canvas_changed(void);
/* A second picture shown over the canvas this frame (w, h multiples of 2), scaled by the GPU; NULL for none */
void gfx_layer(const u32 *px, int w, int h, float x, float y, float dw, float dh, int invert);

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
