#pragma once
#include <3ds.h>

/* The 3DS buttons (libctru KEY_* bits), with the Circle Pad folded into the
 * D-Pad. Directions auto-repeat while held. The touch screen is the stylus. */
#define BTN_UP     KEY_DUP
#define BTN_DOWN   KEY_DDOWN
#define BTN_LEFT   KEY_DLEFT
#define BTN_RIGHT  KEY_DRIGHT
#define BTN_A      KEY_A
#define BTN_B      KEY_B
#define BTN_X      KEY_X
#define BTN_Y      KEY_Y
#define BTN_L      KEY_L
#define BTN_R      KEY_R
#define BTN_START  KEY_START
#define BTN_SELECT KEY_SELECT
#define BTN_DIRS   (BTN_UP | BTN_DOWN | BTN_LEFT | BTN_RIGHT)

typedef struct {
    u32 down;     /* pressed this frame (directions include auto-repeat) */
    u32 held;
    int touch;    /* the stylus is on the touch screen */
    int tx, ty;   /* where, in touch screen pixels (320x240) */
} input_t;

void input_poll(input_t *in);
/* The system keyboard: fills out (UTF-8) and returns 1 when the player confirmed */
int input_swkbd(const char *hint, const char *initial, int max_chars, int name_only, char *out, size_t out_size);
