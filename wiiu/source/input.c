#include <stdlib.h>
#include <math.h>
#include <SDL2/SDL.h>
#include <coreinit/systeminfo.h>
#include <input_wiiu.h>
#include "input.h"

#define REPEAT_DELAY 22 /* frames before a held direction repeats */
#define REPEAT_RATE  5

static int canvas_w = 852, canvas_h = 480;
static int last_px = -1000, last_py = -1000;
static u32 prev_dirs;
static int dir_frames;
static int quit_requested;
static int pad_turn;

/* the GamePad touch screen, which is the stylus */
static int touch_down, touch_x, touch_y, touch_mode;

/* ── Controller mapping ──────────────────────────────────────────────────── */

/* libwiiuinput folds every controller into WPAD_BUTTON_* bits */
static u32 map_folded(u32 b)
{
    u32 out = 0;
    if (b & WPAD_BUTTON_PLUS) out |= BTN_PLUS;
    if (b & WPAD_BUTTON_MINUS) out |= BTN_MINUS;
    if (b & WPAD_BUTTON_A) out |= BTN_A;
    if (b & WPAD_BUTTON_B) out |= BTN_B;
    if (b & WPAD_BUTTON_1) out |= BTN_1;
    if (b & WPAD_BUTTON_2) out |= BTN_2;
    if (b & WPAD_BUTTON_HOME) out |= BTN_HOME;
    if (b & WPAD_BUTTON_UP) out |= BTN_UP;
    if (b & WPAD_BUTTON_DOWN) out |= BTN_DOWN;
    if (b & WPAD_BUTTON_LEFT) out |= BTN_LEFT;
    if (b & WPAD_BUTTON_RIGHT) out |= BTN_RIGHT;
    return out;
}

/* Directions as the player sees them on a GamePad held upright. Turned 90
 * degrees (the D-Pad at the bottom), its up points left; turned 270, right. */
static u32 turn_dirs(u32 b)
{
    if (!pad_turn) return b;
    u32 out = b & ~BTN_DIRS;
    int cw = pad_turn == 270;
    if (b & BTN_UP) out |= cw ? BTN_RIGHT : BTN_LEFT;
    if (b & BTN_RIGHT) out |= cw ? BTN_DOWN : BTN_UP;
    if (b & BTN_DOWN) out |= cw ? BTN_LEFT : BTN_RIGHT;
    if (b & BTN_LEFT) out |= cw ? BTN_UP : BTN_DOWN;
    return out;
}

/* A stick (-1..1, y grows upward) pushed far enough becomes a D-Pad direction */
static u32 stick_dirs(float x, float y)
{
    const float threshold = 0.5f;
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

/* ── USB keyboard (through libwiiuinput) ─────────────────────────────────── */

int input_key(key_event *ev)
{
    input_key_event k;
    if (!INPUT_KeyPop(&k)) return 0;
    ev->code = k.code;
    ev->shift = (k.mods & INPUT_MOD_SHIFT) != 0;
    ev->ctrl = (k.mods & INPUT_MOD_CTRL) != 0;
    ev->ch = ev->ctrl ? 0 : k.ch;
    return 1;
}

int input_keyboard_connected(void) { return INPUT_KeyboardConnected(); }

#ifdef AUTOTEST
void autotest_push_key(u8 code, char ch)
{
    (void)code; (void)ch;
}
void autotest_apply(input_t *in);
#endif

int input_quit_requested(void) { return quit_requested; }

/* ── Setup and polling ───────────────────────────────────────────────────── */

void input_init(void)
{
    /* HOME leaves rooms and quits, like on the Wii; quitting returns to the Wii U Menu */
    OSEnableHomeButtonMenu(FALSE);
    INPUT_Init();
    INPUT_SetScreenSize(canvas_w, canvas_h);
    INPUT_SetCursorMode(INPUT_CURSOR_IR | INPUT_CURSOR_MOUSE); /* touch is read on its own */
}

void input_set_canvas(int width, int height)
{
    canvas_w = width;
    canvas_h = height;
    INPUT_SetScreenSize(width, height);
}

void input_set_gamepad_turn(int degrees) { pad_turn = degrees; }

void input_shutdown(void)
{
    INPUT_Shutdown();
    OSEnableHomeButtonMenu(TRUE);
}

void input_poll(input_t *in)
{
    u32 down = 0, held = 0;

    SDL_Event e;
    while (SDL_PollEvent(&e))
        if (e.type == SDL_QUIT) quit_requested = 1;

    INPUT_Update();

    in->pointer = 0;
    in->touch = 0;

    /* Every controller: the GamePad, Wii Remotes, Pro and Classic Controllers, the GameCube
     * adapter and USB pads, folded into Wii Remote buttons. A stick pushed far enough is a direction. */
    for (int i = 0, n = INPUT_DeviceCount(); i < n; i++) {
        input_device d;
        if (!INPUT_DeviceGet(i, &d)) continue;
        u32 dh = map_folded(d.held), dd = map_folded(d.down);
        dh |= stick_dirs(d.lx, d.ly);
        if (d.kind == INPUT_DEV_GAMEPAD) {
            dh = turn_dirs(dh);
            dd = turn_dirs(dd);
        }
        held |= dh;
        down |= dd;
    }

    /* Wii Remote IR (or a USB mouse) is the pointer */
    if (INPUT_CursorActive()) {
        int cx, cy;
        INPUT_CursorPosition(&cx, &cy);
        in->pointer = 1;
        touch_mode = 0;
        in->px = cx;
        in->py = cy;
    }

    /* The GamePad touch screen is the stylus and wins over the pointer */
    int tx, ty;
    if (INPUT_Touch(&tx, &ty)) {
        touch_x = tx;
        touch_y = ty;
        if (!touch_down) down |= BTN_A;
        touch_down = 1;
        touch_mode = 1;
    } else {
        touch_down = 0;
    }
    if (touch_down) {
        held |= BTN_A;
        in->pointer = 1;
        in->touch = 1;
        in->px = touch_x;
        in->py = touch_y;
    }

    in->touch_mode = touch_mode;

    /* Directions: a new press fires at once, then repeats while held. */
    u32 dirs = held & BTN_DIRS;
    u32 fresh = (dirs & ~prev_dirs) | (down & BTN_DIRS);
    if (dirs && dirs == prev_dirs) dir_frames++;
    else dir_frames = 0;
    if (dir_frames >= REPEAT_DELAY && (dir_frames - REPEAT_DELAY) % REPEAT_RATE == 0) fresh |= dirs;
    prev_dirs = dirs;

    in->down = (down & ~BTN_DIRS) | fresh;
    in->held = held;

    if (in->pointer) {
        if (in->px < 0) in->px = 0;
        if (in->py < 0) in->py = 0;
        if (in->px >= canvas_w) in->px = canvas_w - 1;
        if (in->py >= canvas_h) in->py = canvas_h - 1;
        in->moved = in->touch || abs(in->px - last_px) > 3 || abs(in->py - last_py) > 3; /* ignore IR jitter */
        if (in->moved) {
            last_px = in->px;
            last_py = in->py;
        }
    } else {
        in->moved = 0;
    }
#ifdef AUTOTEST
    autotest_apply(in);
#endif
}
