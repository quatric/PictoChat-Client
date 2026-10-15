#pragma once
#include "platform.h"
#include "gfx.h"

/* The two DS screens, stacked as on pict.chat: a 256x384 canvas in DS pixels,
 * drawn with the usual gfx_* calls between stage_begin() and stage_end(), then
 * scaled onto the TV picture with stage_present(). */

#define STAGE_W 256
#define STAGE_H 384

/* The screen layouts, as on the DS Virtual Console; X / 2 cycles through them */
enum {
    VIEW_BOTH,        /* both screens stacked, as on pict.chat */
    VIEW_BOTTOM,      /* the bottom screen at 2x */
    VIEW_TOP,         /* the top screen at 2x */
    VIEW_SIDE,        /* both screens side by side */
    VIEW_BIG_BOTTOM,  /* a large bottom screen next to a small top screen */
    VIEW_BIG_TOP,     /* a large top screen next to a small bottom screen */
    VIEW_SPLIT,       /* the top screen on the TV, the bottom screen on the GamePad */
    VIEW_TALL,        /* GamePad held upright, D-Pad at the bottom */
    VIEW_TALL_FLIP,   /* GamePad held upright, D-Pad at the top */
    VIEW_COUNT
};
enum { SCREEN_TV, SCREEN_GAMEPAD };

/* Rows sy..sy+sh of the stage, scaled into x, y, w, h of a picture */
typedef struct { int sy, sh, x, y, w, h; } pane_t;

/* What the TV or the GamePad shows: a picture of w x h, upright for the
 * player, turned by rot degrees clockwise onto the 852x480 screen (a GamePad
 * held upright has a 480x852 picture). The name and controls go either side of
 * pane 0 (sides) or in the box hud_x, hud_y, hud_w, hud_h. */
typedef struct {
    int w, h, rot;
    int npanes;
    pane_t pane[2];
    int sides;
    int hud_x, hud_y, hud_w, hud_h;
} layout_t;

void stage_init(void);
void stage_set_view(int view);
int stage_view(void);
const char *stage_view_name(int view);
const char *stage_view_key(int view);   /* its name in settings.txt */
int stage_view_from_key(const char *key); /* VIEW_BOTH when unknown */
const layout_t *stage_layout(int screen);
int stage_mirrored(void);        /* the GamePad shows the TV picture */
int stage_gamepad_turn(void);    /* 0, 90 or 270: how far the GamePad picture is turned */
/* Draw the side panels and overlays of one screen: canvas, SW and SH become its picture */
void stage_select(int screen);
void stage_unselect(void);
u32 *stage_canvas(int screen);
void stage_begin(void);
void stage_end(void);
void stage_present(void);           /* scale pane 0 into the selected picture on the CPU (for overlays) */
const u32 *stage_pixels(void);      /* all 384 rows */
/* Where pane 0 sits on the selected picture (the TV's outside stage_select) */
void stage_rect(int *x, int *y, int *w, int *h);
/* A point on the 852x480 screen -> stage coordinates. Returns the pane it is
 * in, or -1; with pane >= 0 that pane is used even outside it (strokes leaving
 * the screen they started on). */
int stage_from_screen(int screen, int px, int py, int pane, float *sx, float *sy);

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
