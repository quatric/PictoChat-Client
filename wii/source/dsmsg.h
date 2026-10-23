#pragma once
#include <gctypes.h>
#include "chat.h"

/* Messages as DS PictoChat sends them: a picture 256 pixels wide and 16..80
 * tall at 4 bits per pixel, in 8x8 tiles (32 per row of tiles, low nibble =
 * left pixel). Pixel values: 0 background, 1 the pen, 3..15 the rainbow pen.
 * Image x is screen x, and row 0 is the line under the box's top edge; the
 * name tag covers the top left. */

#define DSMSG_W 256
#define DSMSG_MAX_ROWS 80

/* The colour a pixel value is drawn in (before dark mode) */
u32 dsmsg_color(int value);
/* 4bpp tiles <-> one byte per pixel (DSMSG_W * rows) */
void dsmsg_tiles_to_pixels(const u8 *tiles, int rows, u8 *pixels);
void dsmsg_pixels_to_tiles(const u8 *pixels, int rows, u8 *tiles);
/* Draws a message's strokes and text the way the log shows them and turns
 * them into DS pixels. Returns the number of rows (lines * 16). */
int dsmsg_render(const pc_msg *m, u8 *pixels);
