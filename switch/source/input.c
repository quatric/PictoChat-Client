#include <stdlib.h>
#include <math.h>
#include <SDL2/SDL.h>
#include "input.h"

#define REPEAT_DELAY 22 /* frames before a held direction repeats */
#define REPEAT_RATE  5

static int canvas_w = 852, canvas_h = 480;
static int last_px = -1000, last_py = -1000;
static u32 prev_dirs;
static int dir_frames;
static int quit_requested;

/* the touch screen (handheld mode), which is the stylus */
static PadState pad;
static int touch_down, touch_x, touch_y, touch_mode;

/* ── Controller mapping ──────────────────────────────────────────────────── */

static u32 map_pad(u64 b)
{
    u32 out = 0;
    if (b & (HidNpadButton_R | HidNpadButton_ZR | HidNpadButton_Plus)) out |= BTN_PLUS;
    if (b & (HidNpadButton_L | HidNpadButton_ZL)) out |= BTN_MINUS;
    if (b & HidNpadButton_A) out |= BTN_A;
    if (b & HidNpadButton_B) out |= BTN_B;
    if (b & HidNpadButton_X) out |= BTN_2;
    if (b & HidNpadButton_Y) out |= BTN_1;
    if (b & HidNpadButton_Minus) out |= BTN_HOME; /* the Home button belongs to the system */
    if (b & (HidNpadButton_Up | HidNpadButton_StickLUp)) out |= BTN_UP;
    if (b & (HidNpadButton_Down | HidNpadButton_StickLDown)) out |= BTN_DOWN;
    if (b & (HidNpadButton_Left | HidNpadButton_StickLLeft)) out |= BTN_LEFT;
    if (b & (HidNpadButton_Right | HidNpadButton_StickLRight)) out |= BTN_RIGHT;
    return out;
}

/* ── USB keyboard (through SDL) ──────────────────────────────────────────── */

#define KEYQ 64
static key_event keyq[KEYQ];
static int keyq_head, keyq_tail;
static int keyboard_seen;

static const char plain[] =   "abcdefghijklmnopqrstuvwxyz1234567890\n\x1b\b\t -=[]\\#;'`,./";
static const char shifted[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ!@#$%^&*()\n\x1b\b\t _+{}|~:\"~<>?";

static char key_char(u8 code, int shift, int caps)
{
    if (code >= 0x04 && code <= 0x38) {
        char c = (shift ? shifted : plain)[code - 0x04];
        if (code <= 0x1D && caps) c = (c >= 'a' && c <= 'z') ? c - 32 : c + 32;
        return (c >= 0x20 && c < 0x7F) ? c : 0;
    }
    switch (code) {
    case 0x54: return '/';
    case 0x55: return '*';
    case 0x56: return '-';
    case 0x57: return '+';
    case 0x62: return '0';
    case 0x63: return '.';
    }
    if (code >= 0x59 && code <= 0x61) return '1' + (code - 0x59);
    return 0;
}

static void push_key(u8 code, char ch, int shift, int ctrl)
{
    int next = (keyq_head + 1) % KEYQ;
    if (next == keyq_tail) return; /* full */
    key_event *ev = &keyq[keyq_head];
    ev->code = code;
    ev->ch = ch;
    ev->shift = shift;
    ev->ctrl = ctrl;
    keyq_head = next;
}

static void key_down(const SDL_KeyboardEvent *k)
{
    u8 code = (u8)k->keysym.scancode;
    if (k->keysym.scancode > 0xFF || code >= 0xE0 || code == 0x39) return; /* modifiers, Caps Lock */
    keyboard_seen = 1;
    int shift = (k->keysym.mod & KMOD_SHIFT) != 0, ctrl = (k->keysym.mod & KMOD_CTRL) != 0;
    int caps = (k->keysym.mod & KMOD_CAPS) != 0;
    push_key(code, ctrl ? 0 : key_char(code, shift, caps), shift, ctrl);
}

#ifdef AUTOTEST
void autotest_push_key(u8 code, char ch)
{
    push_key(ch == '\n' ? KEY_ENTER : code, ch == '\n' ? 0 : ch, 0, 0);
}
void autotest_apply(input_t *in);
#endif

int input_key(key_event *ev)
{
    if (keyq_tail == keyq_head) return 0;
    *ev = keyq[keyq_tail];
    keyq_tail = (keyq_tail + 1) % KEYQ;
    return 1;
}

int input_keyboard_connected(void) { return keyboard_seen; }

int input_quit_requested(void) { return quit_requested; }

/* ── Setup and polling ───────────────────────────────────────────────────── */

void input_init(void)
{
    padConfigureInput(8, HidNpadStyleSet_NpadStandard);
    padInitializeDefault(&pad);
    hidInitializeTouchScreen();
}

void input_set_canvas(int width, int height)
{
    canvas_w = width;
    canvas_h = height;
}

void input_set_gamepad_turn(int degrees) { (void)degrees; /* one screen, never turned */ }

void input_shutdown(void) {}

void input_poll(input_t *in)
{
    u32 down = 0, held = 0;

    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_QUIT) quit_requested = 1;
        else if (e.type == SDL_KEYDOWN) key_down(&e.key);
    }
    if (!appletMainLoop()) quit_requested = 1; /* the system asked the app to close */

    in->pointer = 0;
    in->touch = 0;

    /* Every controller at once: handheld Joy-Cons, a Pro Controller, a pair of detached Joy-Cons */
    padUpdate(&pad);
    down |= map_pad(padGetButtonsDown(&pad));
    held |= map_pad(padGetButtons(&pad));

    /* The touch screen is the stylus: a tap is A, a drag draws */
    HidTouchScreenState ts = {0};
    int touched = hidGetTouchScreenStates(&ts, 1) > 0 && ts.count > 0;
    if (touched) {
        /* touch coordinates are 1280x720 whatever the output resolution */
        touch_x = ts.touches[0].x * canvas_w / 1280;
        touch_y = ts.touches[0].y * canvas_h / 720;
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
