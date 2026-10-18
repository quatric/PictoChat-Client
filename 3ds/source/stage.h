#pragma once
#include <3ds/types.h>
#include "gfx.h"

/* The two DS screens, stacked as on pict.chat: a 256x384 canvas in DS pixels,
 * drawn with the usual gfx_* calls between stage_begin() and stage_end(). The
 * top half goes to the top LCD and the bottom half to the touch screen. */

#define STAGE_W 256
#define STAGE_H 384

enum { SCALE_FIT, SCALE_PIXEL };

void stage_init(void);
void stage_set_scale(int mode);  /* SCALE_FIT (1.25x, fills the screens) or SCALE_PIXEL (1:1) */
int stage_scale(void);
void stage_begin(void);
void stage_end(void);
void stage_present(int allow_dark); /* show the stage on the LCDs this frame (see gfx_stage) */
/* Where a DS screen sits on an LCD canvas */
void stage_rect(int screen, int *x, int *y, int *w, int *h);
/* Touch screen pixel -> stage coordinates; 1 when inside the DS bottom screen */
int stage_from_touch(int tx, int ty, float *sx, float *sy);

/* Drawing helpers in stage pixels, as PIXI sprites with tint and alpha */
void stage_blit(const image_t *img, int x, int y, u32 tint, int alpha);
void stage_blit_region(const image_t *img, int sx, int sy, int w, int h, int x, int y, u32 tint, int alpha);
void stage_blit_multiply(const image_t *img, int x, int y, u32 tint, int alpha);
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
