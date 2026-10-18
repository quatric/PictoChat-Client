#include <stdlib.h>
#include <string.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include "font.h"
#include "gfx.h"
#include "rodin_db_woff.h"
#include "rodin_b_woff.h"
#include "pictochat_ttf.h"

typedef struct {
    u32 key; /* font << 28 | size << 21 | codepoint */
    s16 left, top, advance;
    u16 w, h;
    u8 *alpha;
} glyph_t;

#define CACHE 4096
static glyph_t cache[CACHE];
static FT_Library ft;
static FT_Face faces[FONT_COUNT];
static int face_size[FONT_COUNT];

void font_init(void)
{
    FT_Init_FreeType(&ft);
    FT_New_Memory_Face(ft, rodin_db_woff, rodin_db_woff_size, 0, &faces[FONT_REGULAR]);
    FT_New_Memory_Face(ft, rodin_b_woff, rodin_b_woff_size, 0, &faces[FONT_BOLD]);
    FT_New_Memory_Face(ft, pictochat_ttf, pictochat_ttf_size, 0, &faces[FONT_PIXEL]);
}

static int ds_loaded;

void font_load_ds(const unsigned char *data, size_t size)
{
    if (ds_loaded || !data) return;
    if (FT_New_Memory_Face(ft, data, size, 0, &faces[FONT_DS]) == 0) {
        ds_loaded = 1;
        /* drop FONT_DS glyphs that were drawn with the stand-in font */
        for (int i = 0; i < CACHE; i++)
            if (cache[i].key && (cache[i].key >> 28) == FONT_DS) {
                free(cache[i].alpha);
                memset(&cache[i], 0, sizeof cache[i]);
            }
    }
}

static FT_Face face_at(int font, int size)
{
    FT_Face f = faces[font];
    if (f && face_size[font] != size) {
        FT_Set_Pixel_Sizes(f, 0, size);
        face_size[font] = size;
    }
    return f;
}

int font_line_height(int font, int size)
{
    (void)font;
    return size * 155 / 100; /* line-height: 1.55, as on the site */
}

static glyph_t *glyph(int font, int size, u32 cp)
{
    u32 key = ((u32)font << 28) | ((u32)size << 21) | (cp & 0x1FFFFF);
    u32 h = (key * 2654435761u) % CACHE;
    for (int n = 0; n < CACHE; n++, h = (h + 1) % CACHE) {
        if (cache[h].key == key) return &cache[h];
        if (cache[h].key == 0) break;
    }
    FT_Face f = font == FONT_DS && !ds_loaded ? face_at(FONT_PIXEL, size) : face_at(font, size);
    glyph_t *g = &cache[h];
    if (g->key) { /* table full: reuse this slot */
        free(g->alpha);
    }
    memset(g, 0, sizeof(*g));
    g->key = key;
    if (!f) return g;
    FT_UInt index = FT_Get_Char_Index(f, cp);
    if (!index && font != FONT_REGULAR) { /* fall back for missing characters */
        f = face_at(FONT_REGULAR, size);
        index = FT_Get_Char_Index(f, cp);
    }
    if (FT_Load_Glyph(f, index, FT_LOAD_RENDER | (font >= FONT_PIXEL ? FT_LOAD_TARGET_MONO : FT_LOAD_TARGET_LIGHT)))
        return g;
    FT_Bitmap *bm = &f->glyph->bitmap;
    g->left = f->glyph->bitmap_left;
    g->top = f->glyph->bitmap_top;
    g->advance = (f->glyph->advance.x + 32) >> 6;
    g->w = bm->width;
    g->h = bm->rows;
    if (g->w && g->h) {
        g->alpha = malloc(g->w * g->h);
        for (int y = 0; y < g->h; y++)
            for (int x = 0; x < g->w; x++) {
                u8 a;
                if (bm->pixel_mode == FT_PIXEL_MODE_MONO)
                    a = (bm->buffer[y * bm->pitch + (x >> 3)] & (0x80 >> (x & 7))) ? 255 : 0;
                else
                    a = bm->buffer[y * bm->pitch + x];
                g->alpha[y * g->w + x] = a;
            }
    }
    return g;
}

u32 utf8_next(const char **ps)
{
    const unsigned char *s = (const unsigned char *)*ps;
    u32 c = *s++;
    if (c >= 0xF0 && (s[0] & 0xC0) == 0x80 && (s[1] & 0xC0) == 0x80 && (s[2] & 0xC0) == 0x80) {
        c = ((c & 7) << 18) | ((s[0] & 0x3F) << 12) | ((s[1] & 0x3F) << 6) | (s[2] & 0x3F);
        s += 3;
    } else if (c >= 0xE0 && (s[0] & 0xC0) == 0x80 && (s[1] & 0xC0) == 0x80) {
        c = ((c & 0xF) << 12) | ((s[0] & 0x3F) << 6) | (s[1] & 0x3F);
        s += 2;
    } else if (c >= 0xC0 && (s[0] & 0xC0) == 0x80) {
        c = ((c & 0x1F) << 6) | (s[0] & 0x3F);
        s += 1;
    } else if (c >= 0x80) {
        c = 0xFFFD;
    }
    *ps = (const char *)s;
    return c;
}

int utf8_prev(const char *s, int pos)
{
    if (pos <= 0) return 0;
    pos--;
    while (pos > 0 && ((unsigned char)s[pos] & 0xC0) == 0x80) pos--;
    return pos;
}

int utf8_encode(u32 cp, char *out)
{
    if (cp < 0x80) { out[0] = cp; return 1; }
    if (cp < 0x800) { out[0] = 0xC0 | (cp >> 6); out[1] = 0x80 | (cp & 0x3F); return 2; }
    if (cp < 0x10000) {
        out[0] = 0xE0 | (cp >> 12); out[1] = 0x80 | ((cp >> 6) & 0x3F); out[2] = 0x80 | (cp & 0x3F);
        return 3;
    }
    out[0] = 0xF0 | (cp >> 18); out[1] = 0x80 | ((cp >> 12) & 0x3F);
    out[2] = 0x80 | ((cp >> 6) & 0x3F); out[3] = 0x80 | (cp & 0x3F);
    return 4;
}

int text_width_n(int font, int size, const char *s, int bytes)
{
    const char *end = s + bytes;
    int w = 0;
    while (s < end && *s) w += glyph(font, size, utf8_next(&s))->advance;
    return w;
}

int text_width(int font, int size, const char *s) { return text_width_n(font, size, s, strlen(s)); }

static int baseline(int font, int size)
{
    /* centre the cap height in the 1.55 line box like the browser does */
    return (font_line_height(font, size) - size) / 2 + size * (font >= FONT_PIXEL ? 88 : 86) / 100;
}

int text_draw_n(int font, int size, int x, int y, u32 color, const char *s, int bytes)
{
    const char *end = s + bytes;
    int base = y + baseline(font, size);
    while (s < end && *s) {
        u32 cp = utf8_next(&s);
        if (cp == '\n') continue;
        glyph_t *g = glyph(font, size, cp);
        if (g->alpha) gfx_mask(g->alpha, g->w, g->h, x + g->left, base - g->top, color);
        x += g->advance;
    }
    return x;
}

int text_draw(int font, int size, int x, int y, u32 color, const char *s)
{
    return text_draw_n(font, size, x, y, color, s, strlen(s));
}

void text_draw_fit(int font, int size, int x, int y, int max_w, u32 color, const char *s)
{
    int len = strlen(s);
    if (text_width_n(font, size, s, len) <= max_w) {
        text_draw_n(font, size, x, y, color, s, len);
        return;
    }
    int ell = text_width(font, size, "...");
    while (len > 0 && text_width_n(font, size, s, len) + ell > max_w) len = utf8_prev(s, len);
    x = text_draw_n(font, size, x, y, color, s, len);
    text_draw(font, size, x, y, color, "...");
}

void text_draw_center(int font, int size, int x, int y, int w, u32 color, const char *s)
{
    text_draw(font, size, x + (w - text_width(font, size, s)) / 2, y, color, s);
}

int text_wrap(int font, int size, const char *s, int max_w, int *starts, int *lens, int max_lines)
{
    int n = 0, pos = 0, total = strlen(s);
    while (n < max_lines) {
        int line_start = pos, last_space = -1, w = 0, i = pos;
        while (i < total && s[i] != '\n') {
            const char *p = s + i;
            u32 cp = utf8_next(&p);
            int cw = glyph(font, size, cp)->advance;
            if (w + cw > max_w && i > line_start) break;
            if (cp == ' ') last_space = i;
            w += cw;
            i = p - s;
        }
        if (i < total && s[i] != '\n' && last_space > line_start) i = last_space + 1;
        starts[n] = line_start;
        lens[n] = i - line_start;
        n++;
        if (i >= total) break;
        pos = s[i] == '\n' ? i + 1 : i;
        if (pos >= total && s[i] == '\n') {
            if (n < max_lines) { starts[n] = pos; lens[n] = 0; n++; }
            break;
        }
    }
    return n;
}

/* ── DS text ─────────────────────────────────────────────────────────────── */

#define DS_SIZE 10
#define DS_BASELINE 9

int ds_text_n(int x, int y, u32 color, const char *s, int bytes)
{
    const char *end = s + bytes;
    int base = y + DS_BASELINE;
    while (s < end && *s) {
        u32 cp = utf8_next(&s);
        if (cp == '\n') continue;
        glyph_t *g = glyph(FONT_DS, DS_SIZE, cp);
        if (g->alpha) gfx_mask(g->alpha, g->w, g->h, x + g->left, base - g->top, color);
        x += g->advance;
    }
    return x;
}

int ds_text(int x, int y, u32 color, const char *s) { return ds_text_n(x, y, color, s, strlen(s)); }
int ds_width_n(const char *s, int bytes) { return text_width_n(FONT_DS, DS_SIZE, s, bytes); }
int ds_width(const char *s) { return text_width_n(FONT_DS, DS_SIZE, s, strlen(s)); }
