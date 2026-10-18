#pragma once
#include "chat.h"

/* The top screen: the scrolling list of messages and join/leave notices. */

void log_reset(void);
void log_add_message(const pc_msg *msg);  /* copies msg */
void log_add_banner(int image, const char *name, int text_x);
void log_add_image(int image);
void log_update(void);                     /* animate scrolling, once per frame */
void log_draw(void);
void log_scroll(int delta);                /* -1 older, +1 newer */
void log_scroll_end(void);
/* The message the view is resting on (for Renew), or NULL */
const pc_msg *log_current(void);

/* Draw a message box with its top-left corner at x,y (also used for the compose box) */
void msg_draw(const pc_msg *msg, int x, int y);
int msg_height(const pc_msg *msg);
/* Draw a list of drawing operations; ox, oy shift pict.chat's stage coordinates */
void ops_draw(const pc_op *ops, int n, int ox, int oy, int clip_lines);
