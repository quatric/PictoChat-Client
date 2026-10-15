#include <stdio.h>
#include <string.h>
#include "input.h"

#define REPEAT_DELAY 22 /* frames before a held direction repeats */
#define REPEAT_RATE  5

#ifdef AUTOTEST
void autotest_apply(input_t *in);
int autotest_text(char *out, size_t size);
#endif

static u32 prev_dirs;
static int dir_frames;

void input_poll(input_t *in)
{
    hidScanInput();
    u32 down = hidKeysDown(), held = hidKeysHeld();
    /* the Circle Pad (and the C-Stick on a New 3DS) act as the D-Pad */
    if (held & (KEY_CPAD_UP | KEY_CSTICK_UP)) held |= BTN_UP;
    if (held & (KEY_CPAD_DOWN | KEY_CSTICK_DOWN)) held |= BTN_DOWN;
    if (held & (KEY_CPAD_LEFT | KEY_CSTICK_LEFT)) held |= BTN_LEFT;
    if (held & (KEY_CPAD_RIGHT | KEY_CSTICK_RIGHT)) held |= BTN_RIGHT;
    /* ZL and ZR (New 3DS) work like L and R */
    if (held & KEY_ZL) held |= BTN_L;
    if (held & KEY_ZR) held |= BTN_R;
    if (down & KEY_ZL) down |= BTN_L;
    if (down & KEY_ZR) down |= BTN_R;

    /* Directions: a new press fires at once, then repeats while held. */
    u32 dirs = held & BTN_DIRS;
    u32 fresh = (dirs & ~prev_dirs) | (down & BTN_DIRS);
    if (dirs && dirs == prev_dirs) dir_frames++;
    else dir_frames = 0;
    if (dir_frames >= REPEAT_DELAY && (dir_frames - REPEAT_DELAY) % REPEAT_RATE == 0) fresh |= dirs;
    prev_dirs = dirs;

    in->down = (down & ~BTN_DIRS) | fresh;
    in->held = held;
    in->touch = (held & KEY_TOUCH) != 0;
    if (in->touch) {
        touchPosition t;
        hidTouchRead(&t);
        in->tx = t.px;
        in->ty = t.py;
    }
#ifdef AUTOTEST
    autotest_apply(in);
#endif
}

int input_swkbd(const char *hint, const char *initial, int max_chars, int name_only, char *out, size_t out_size)
{
#ifdef AUTOTEST
    if (autotest_text(out, out_size)) return 1;
#endif
    SwkbdState kb;
    swkbdInit(&kb, SWKBD_TYPE_NORMAL, 2, max_chars);
    swkbdSetHintText(&kb, hint);
    if (initial) swkbdSetInitialText(&kb, initial);
    swkbdSetValidation(&kb, name_only ? SWKBD_NOTBLANK_NOTEMPTY : SWKBD_ANYTHING, 0, 0);
    if (!name_only) swkbdSetFeatures(&kb, SWKBD_MULTILINE);
    char buf[512];
    SwkbdButton b = swkbdInputText(&kb, buf, sizeof buf);
    if (b != SWKBD_BUTTON_CONFIRM) return 0;
    snprintf(out, out_size, "%s", buf);
    return 1;
}
