#include <string.h>
#include "dsmsg.h"
#include "gfx.h"
#include "msglog.h"
#include "stage.h"

/* The rainbow pen steps through 13 values; on a DS they go round the hue
 * circle from violet through red, yellow and green to blue. */
#define RAINBOW_FIRST 3
#define RAINBOW_STEP  28
#define RAINBOW_ZERO  6   /* the value that is red (hue 0) */

/* The DS rows sit one pixel under the box's top edge: stage row 208 */
#define DS_TOP 208

u32 dsmsg_color(int v)
{
    if (v <= 0 || v == 2) return 0xFBFBFB;
    if (v == 1) return 0x000000;
    return rainbow(((v - RAINBOW_ZERO) * RAINBOW_STEP + 360) % 360);
}

void dsmsg_tiles_to_pixels(const u8 *tiles, int rows, u8 *pixels)
{
    int tile_rows = rows / 8;
    for (int t = 0; t < tile_rows * 32; t++) {
        int tx = t % 32, ty = t / 32;
        const u8 *tile = tiles + t * 32;
        for (int i = 0; i < 32; i++) {
            u8 *p = pixels + (ty * 8 + i / 4) * DSMSG_W + tx * 8 + (i % 4) * 2;
            p[0] = tile[i] & 0xF;
            p[1] = tile[i] >> 4;
        }
    }
}

void dsmsg_pixels_to_tiles(const u8 *pixels, int rows, u8 *tiles)
{
    int tile_rows = rows / 8;
    for (int t = 0; t < tile_rows * 32; t++) {
        int tx = t % 32, ty = t / 32;
        u8 *tile = tiles + t * 32;
        for (int i = 0; i < 32; i++) {
            const u8 *p = pixels + (ty * 8 + i / 4) * DSMSG_W + tx * 8 + (i % 4) * 2;
            tile[i] = (p[0] & 0xF) | (p[1] << 4);
        }
    }
}

/* Plain pixels become the pen, coloured ones the rainbow value nearest
 * their hue, the background and the eraser nothing */
static u8 quantize(u32 c)
{
    int r = (c >> 16) & 0xFF, g = (c >> 8) & 0xFF, b = c & 0xFF;
    int mx = r > g ? (r > b ? r : b) : (g > b ? g : b);
    int mn = r < g ? (r < b ? r : b) : (g < b ? g : b);
    if (mx - mn < 64) return (r + g + b) / 3 < 128 ? 1 : 0;
    int d = mx - mn, h;
    if (mx == r) h = 60 * (g - b) / d;
    else if (mx == g) h = 120 + 60 * (b - r) / d;
    else h = 240 + 60 * (r - g) / d;
    h = (h + 360) % 360;
    /* the values sit at hues -84..252; past halfway to -84 (+360) wrap round */
    int last = (15 - RAINBOW_ZERO) * RAINBOW_STEP, first = (RAINBOW_FIRST - RAINBOW_ZERO) * RAINBOW_STEP + 360;
    if (h > (last + first) / 2) h -= 360;
    int v = RAINBOW_ZERO + (h >= 0 ? (h + RAINBOW_STEP / 2) / RAINBOW_STEP : -((-h + RAINBOW_STEP / 2) / RAINBOW_STEP));
    if (v < RAINBOW_FIRST) v = RAINBOW_FIRST;
    if (v > 15) v = 15;
    return v;
}

int dsmsg_render(const pc_msg *m, u8 *pixels)
{
    static u32 buf[DSMSG_W * DSMSG_MAX_ROWS];
    int rows = m->lines * 16;
    if (rows < 16) rows = 16;
    if (rows > DSMSG_MAX_ROWS) rows = DSMSG_MAX_ROWS;

    /* draw into our own small canvas, in light mode */
    u32 *saved = canvas;
    int saved_w = SW, saved_h = SH, dark = stage_dark();
    stage_set_dark(0);
    canvas = buf;
    SW = DSMSG_W;
    SH = rows;
    gfx_noclip();
    gfx_fill(0, 0, DSMSG_W, rows, 0xFBFBFB);
    msg_draw_content(m, 0, -DS_TOP);
    canvas = saved;
    SW = saved_w;
    SH = saved_h;
    gfx_noclip();
    stage_set_dark(dark);

    for (int i = 0; i < DSMSG_W * rows; i++) pixels[i] = quantize(buf[i]);
    return rows;
}
