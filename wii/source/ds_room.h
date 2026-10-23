#pragma once
#include "chat.h"

/* A local PictoChat room for Nintendo DS consoles: the Wii hosts it over the
 * DS wireless protocol and talks to it through the same events as
 * PictoChat Online. WD takes the WiFi chip, so the Internet is gone until
 * the next boot. */

/* room 0..3 (A..D); color_index is the DS colour 0..15. 0, or -1 with
 * the reason in *why. */
int ds_room_start(int room, const char *name, int color_index, char *why, int why_len);
void ds_room_stop(void);
int ds_room_active(void);
/* 1 once WD has run in this boot: the Internet won't come back */
int ds_room_used(void);

int ds_room_poll(chat_event *ev);   /* like chat_poll */
void ds_room_send(const pc_msg *msg);
