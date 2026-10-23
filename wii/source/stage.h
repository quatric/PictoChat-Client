#pragma once
#include <gctypes.h>
#include "gfx.h"

/* The two DS screens, stacked as on pict.chat: a 256x384 canvas in DS pixels,
 * drawn with the usual gfx_* calls between stage_begin() and stage_end(), then
 * scaled onto the TV picture with stage_present(). */

#define STAGE_W 256
#define STAGE_H 384

enum { VIEW_BOTH, VIEW_BOTTOM, VIEW_TOP };

void stage_init(void);
void stage_set_view(int view);   /* both screens, or one of them zoomed in */
int stage_view(void);
void stage_begin(void);
void stage_end(void);
void stage_present(void);           /* scale into the canvas on the CPU (for overlays) */
const u32 *stage_pixels(int *rows);
/* Where the stage sits on the TV canvas */
void stage_rect(int *x, int *y, int *w, int *h);
/* TV canvas pixel -> stage coordinates; 1 when inside the stage */
int stage_from_canvas(int px, int py, float *sx, float *sy);
void stage_to_canvas(float sx, float sy, int *px, int *py);

/* Drawing helpers in stage pixels, as PIXI sprites with tint and alpha */
void stage_blit(const image_t *img, int x, int y, u32 tint, int alpha);
void stage_blit_region(const image_t *img, int sx, int sy, int w, int h, int x, int y, u32 tint, int alpha);
void stage_blit_multiply(const image_t *img, int x, int y, u32 tint, int alpha);
void stage_blit_multiply_region(const image_t *img, int sx, int sy, int w, int h, int x, int y, u32 tint, int alpha);
void stage_line(float x0, float y0, float x1, float y1, int width, u32 color);

/* pict.chat's dark mode: the page is inverted, and sprites it loads with
 * PIXI.Sprite.from get their hue turned 180 degrees first so they keep their
 * colours. Colours pict.chat passes through darkIt() go through dk(). */
void stage_set_dark(int on);
int stage_dark(void);
void stage_flip(int on);          /* hue-flip the sprites drawn from now on */
static inline u32 dk(u32 c) { extern int stage_dark_on; return stage_dark_on ? c ^ 0xFFFFFF : c; }

u32 brighten(u32 color, int percent);   /* pict.chat's increase_brightness */
u32 rainbow(int degrees);                /* hsl(deg, 100%, 50%) */
