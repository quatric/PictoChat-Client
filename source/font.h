#pragma once
#include <stddef.h>
#include <3ds/types.h>

/* Text in Rodin NTLG Pro DB and B (menus), the Pictochat font and, once it has been
 * downloaded, pict.chat's NintendoDSBIOS font. Strings are UTF-8. y is the top
 * of the line box. */
enum { FONT_REGULAR, FONT_BOLD, FONT_PIXEL, FONT_DS, FONT_COUNT };

void font_init(void);
/* Use pict.chat's NintendoDSBIOS font for FONT_DS (until then FONT_DS is FONT_PIXEL). */
void font_load_ds(const unsigned char *data, size_t size);
/* DS text as pict.chat draws it: 10 px, y is the top of a 12 px line. */
int ds_text(int x, int y, u32 color, const char *s);
int ds_text_n(int x, int y, u32 color, const char *s, int bytes);
int ds_width(const char *s);
int ds_width_n(const char *s, int bytes);
int font_line_height(int font, int size);
int text_width(int font, int size, const char *s);
int text_width_n(int font, int size, const char *s, int bytes);
int text_draw(int font, int size, int x, int y, u32 color, const char *s);
int text_draw_n(int font, int size, int x, int y, u32 color, const char *s, int bytes);
/* Truncate with an ellipsis to fit max_w */
void text_draw_fit(int font, int size, int x, int y, int max_w, u32 color, const char *s);
void text_draw_center(int font, int size, int x, int y, int w, u32 color, const char *s);

/* Word-wrap into at most max_lines; starts[i]/lens[i] are byte ranges. */
int text_wrap(int font, int size, const char *s, int max_w, int *starts, int *lens, int max_lines);

u32 utf8_next(const char **s);
int utf8_prev(const char *s, int pos);
int utf8_encode(u32 cp, char *out);
