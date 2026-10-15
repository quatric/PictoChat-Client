#pragma once
#include "platform.h"
#include "chat.h"

/* The bottom screen in a room: the drawing box, the keyboards and the tools,
 * laid out and behaving like pict.chat. Coordinates are stage pixels. */

enum { TOUCH_DOWN, TOUCH_HELD, TOUCH_UP };
enum { COMPOSE_NOTHING, COMPOSE_EXIT, COMPOSE_SCROLL_UP, COMPOSE_SCROLL_DOWN, COMPOSE_SCROLL_END, COMPOSE_SENT, COMPOSE_RENEW };

void compose_reset(const char *name, u32 color);
void compose_update(void);                    /* once per frame (drawing, rainbow) */
void compose_draw(int alpha);                 /* 0..255, for the fade in */
int compose_touch(int phase, float x, float y);
void compose_type(const char *utf8);          /* one character */
void compose_backspace(void);
void compose_enter(void);
int compose_send(void);                       /* COMPOSE_SENT, or COMPOSE_NOTHING when empty */
void compose_clear(void);
void compose_renew(const pc_msg *from);
void compose_next_keyboard(void);
void compose_toggle_pen_size(void);
void compose_toggle_pen_mode(void);
/* the last message sent, for the log */
const pc_msg *compose_last_sent(void);

/* The plain keyboard on its own (for typing a name): mode 0 normal, 1 shift, 2 caps */
int keyboard_key_at(float x, float y);
const char *keyboard_label(int mode, int key);
