#pragma once
#include <3ds/types.h>

typedef struct {
    char name[16];
    u32 color;
    char chat[128];     /* the PictoChat Online WebSocket */
    char site[128];     /* the Origin the chat server expects */
    char pair[128];     /* captcha pairing (pictocard.net) */
    int captcha;        /* 0 for self-hosted servers without a captcha */
    int volume;         /* 0..100 */
    int dark;           /* pict.chat's dark mode */
    int pixel;          /* show the DS screens 1:1 instead of filling the LCDs */
} settings_t;

extern settings_t settings;
extern char app_dir[128];
void settings_load(void); /* from sdmc:/3ds/PictoChat/settings.txt */
void settings_save(void);
