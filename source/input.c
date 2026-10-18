#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <input_wii.h>
#include "input.h"

/* Controllers, keyboard, mouse and the optional Wii U / DualShock 3 backends
 * come from libinput-wii. This file adds the app's own behaviour on top:
 * sticks and the mouse wheel as the D-Pad, mouse buttons as A / B / +,
 * and auto-repeating directions. */

#define REPEAT_DELAY 22 /* frames before a held direction repeats */
#define REPEAT_RATE  5

static u32 prev_dirs;
static int dir_frames;

/* A stick pushed past `threshold` becomes a D-Pad direction (y grows upward). */
static u32 stick_dirs(float x, float y, float threshold)
{
    u32 out = 0;
    if (fabsf(x) > fabsf(y)) {
        if (x > threshold) out |= BTN_RIGHT;
        else if (x < -threshold) out |= BTN_LEFT;
    } else {
        if (y > threshold) out |= BTN_UP;
        else if (y < -threshold) out |= BTN_DOWN;
    }
    return out;
}

/* The per-device thresholds this app has always used, in the library's
 * -1..1 stick range. */
static u32 left_stick_dirs(void)
{
    float x, y;
    const char *src;
    float mag = INPUT_Stick(INPUT_STICK_LEFT, &x, &y, &src);

    if (!strcmp(src, "Nunchuk") || !strcmp(src, "Classic"))
        return mag < 0.55f ? 0 : stick_dirs(x, y, 0.40f);
    if (!strcmp(src, "GameCube"))
        return stick_dirs(x, y, 45.0f / 70.0f);
    if (!strcmp(src, "Wii U Pro"))
        return stick_dirs(x, y, 400.0f / 900.0f);
    if (!strcmp(src, "GamePad"))
        return stick_dirs(x, y, 50.0f / 900.0f);
    if (!strcmp(src, "DualShock 3"))
        return stick_dirs(x, y, 60.0f / 110.0f);
    return 0;
}

static u32 mouse_map(u8 buttons)
{
    u32 out = 0;
    if (buttons & INPUT_MOUSE_LEFT) out |= WPAD_BUTTON_A;
    if (buttons & INPUT_MOUSE_RIGHT) out |= WPAD_BUTTON_B;
    if (buttons & INPUT_MOUSE_MIDDLE) out |= WPAD_BUTTON_PLUS;
    return out;
}

/* ── USB keyboard ────────────────────────────────────────────────────────── */

#ifdef AUTOTEST
#define KEYQ 64
static key_event keyq[KEYQ];
static int keyq_head, keyq_tail;

void autotest_push_key(u8 code, char ch)
{
    int next = (keyq_head + 1) % KEYQ;
    if (next == keyq_tail) return;
    key_event *ev = &keyq[keyq_head];
    ev->code = ch == '\n' ? KEY_ENTER : code;
    ev->ch = ch == '\n' ? 0 : ch;
    ev->shift = ev->ctrl = 0;
    keyq_head = next;
}
void autotest_apply(input_t *in);
#endif

int input_key(key_event *ev)
{
#ifdef AUTOTEST
    if (keyq_tail != keyq_head) {
        *ev = keyq[keyq_tail];
        keyq_tail = (keyq_tail + 1) % KEYQ;
        return 1;
    }
#endif
    input_key_event k;
    if (!INPUT_KeyPop(&k)) return 0;
    ev->code = k.code;
    ev->shift = (k.mods & INPUT_MOD_SHIFT) != 0;
    ev->ctrl = (k.mods & INPUT_MOD_CTRL) != 0;
    ev->ch = ev->ctrl ? 0 : k.ch;
    return 1;
}

int input_keyboard_connected(void)
{
    return INPUT_KeyboardConnected();
}

/* ── Setup and polling ───────────────────────────────────────────────────── */

void input_init(void)
{
    INPUT_Init();
    /* The keyboard types text through input_key(); it is not a controller. */
    INPUT_KeyboardMapClear();
    INPUT_MouseSetSpeed(2);
    INPUT_SetCursorMode(INPUT_CURSOR_IR | INPUT_CURSOR_MOUSE);
}

void input_set_canvas(int width, int height)
{
    INPUT_SetScreenSize(width, height);
}

void input_shutdown(void)
{
    INPUT_Shutdown();
}

void input_poll(input_t *in)
{
    int dx, dy, wheel;

    INPUT_Update();

    u32 down = INPUT_ButtonsDown() | mouse_map(INPUT_MouseButtonsPressed());
    u32 held = INPUT_ButtonsHeld() | mouse_map(INPUT_MouseButtonsHeld()) | left_stick_dirs();

    u32 wheel_dirs = 0;
    INPUT_MouseDelta(&dx, &dy, &wheel);
    if (wheel > 0) wheel_dirs = BTN_DOWN;
    else if (wheel < 0) wheel_dirs = BTN_UP;

    /* Directions: a new press fires at once, then repeats while held. */
    u32 dirs = held & BTN_DIRS;
    u32 fresh = (dirs & ~prev_dirs) | (down & BTN_DIRS);
    if (dirs && dirs == prev_dirs) dir_frames++;
    else dir_frames = 0;
    if (dir_frames >= REPEAT_DELAY && (dir_frames - REPEAT_DELAY) % REPEAT_RATE == 0) fresh |= dirs;
    prev_dirs = dirs;

    in->down = (down & ~BTN_DIRS) | fresh | wheel_dirs;
    in->held = held;

    /* Wii Remote pointer, or the mouse once it is used, with a smooth
     * hand-over between the two. */
    in->pointer = INPUT_CursorActive();
    INPUT_CursorPosition(&in->px, &in->py);
    in->moved = in->pointer && INPUT_CursorMoved();
#ifdef AUTOTEST
    autotest_apply(in);
#endif
}
