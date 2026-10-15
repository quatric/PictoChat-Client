#include <stdlib.h>
#include <math.h>
#include <SDL2/SDL.h>
#include <vpad/input.h>
#include <padscore/kpad.h>
#include <padscore/wpad.h>
#include <coreinit/systeminfo.h>
#include "input.h"

#define REPEAT_DELAY 22 /* frames before a held direction repeats */
#define REPEAT_RATE  5

static int canvas_w = 852, canvas_h = 480;
static int last_px = -1000, last_py = -1000;
static u32 prev_dirs;
static int dir_frames;
static int quit_requested;

/* the GamePad touch screen, which is the stylus */
static int touch_down, touch_x, touch_y, touch_mode;

/* ── Controller mapping ──────────────────────────────────────────────────── */

static u32 map_vpad(u32 b)
{
    u32 out = 0;
    if (b & (VPAD_BUTTON_ZR | VPAD_BUTTON_R | VPAD_BUTTON_PLUS)) out |= BTN_PLUS;
    if (b & (VPAD_BUTTON_ZL | VPAD_BUTTON_L | VPAD_BUTTON_MINUS)) out |= BTN_MINUS;
    if (b & VPAD_BUTTON_A) out |= BTN_A;
    if (b & VPAD_BUTTON_B) out |= BTN_B;
    if (b & VPAD_BUTTON_X) out |= BTN_2;
    if (b & VPAD_BUTTON_Y) out |= BTN_1;
    if (b & VPAD_BUTTON_HOME) out |= BTN_HOME;
    if (b & (VPAD_BUTTON_UP | VPAD_STICK_L_EMULATION_UP)) out |= BTN_UP;
    if (b & (VPAD_BUTTON_DOWN | VPAD_STICK_L_EMULATION_DOWN)) out |= BTN_DOWN;
    if (b & (VPAD_BUTTON_LEFT | VPAD_STICK_L_EMULATION_LEFT)) out |= BTN_LEFT;
    if (b & (VPAD_BUTTON_RIGHT | VPAD_STICK_L_EMULATION_RIGHT)) out |= BTN_RIGHT;
    return out;
}

static u32 map_wiimote(u32 b)
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

/* The Classic Controller and the Pro Controller share their button bits */
static u32 map_classic(u32 b)
{
    u32 out = 0;
    if (b & (WPAD_CLASSIC_BUTTON_ZR | WPAD_CLASSIC_BUTTON_R | WPAD_CLASSIC_BUTTON_PLUS)) out |= BTN_PLUS;
    if (b & (WPAD_CLASSIC_BUTTON_ZL | WPAD_CLASSIC_BUTTON_L | WPAD_CLASSIC_BUTTON_MINUS)) out |= BTN_MINUS;
    if (b & WPAD_CLASSIC_BUTTON_A) out |= BTN_A;
    if (b & WPAD_CLASSIC_BUTTON_B) out |= BTN_B;
    if (b & WPAD_CLASSIC_BUTTON_X) out |= BTN_2;
    if (b & WPAD_CLASSIC_BUTTON_Y) out |= BTN_1;
    if (b & WPAD_CLASSIC_BUTTON_HOME) out |= BTN_HOME;
    if (b & WPAD_CLASSIC_BUTTON_UP) out |= BTN_UP;
    if (b & WPAD_CLASSIC_BUTTON_DOWN) out |= BTN_DOWN;
    if (b & WPAD_CLASSIC_BUTTON_LEFT) out |= BTN_LEFT;
    if (b & WPAD_CLASSIC_BUTTON_RIGHT) out |= BTN_RIGHT;
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
    VPADInit();
    KPADInit();
    WPADEnableURCC(1); /* Pro Controllers */
    VPADSetSensorBar(VPAD_CHAN_0, TRUE);
    /* HOME leaves rooms and quits, like on the Wii; quitting returns to the Wii U Menu */
    OSEnableHomeButtonMenu(FALSE);
}

void input_set_canvas(int width, int height)
{
    canvas_w = width;
    canvas_h = height;
}

void input_shutdown(void)
{
    OSEnableHomeButtonMenu(TRUE);
}

void input_poll(input_t *in)
{
    u32 down = 0, held = 0;

    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_QUIT) quit_requested = 1;
        else if (e.type == SDL_KEYDOWN) key_down(&e.key);
    }

    in->pointer = 0;
    in->touch = 0;

    /* Wii Remotes first: the one pointed at the screen is the pointer */
    for (int chan = 0; chan < 4; chan++) {
        KPADStatus ks;
        KPADError err = KPAD_ERROR_OK;
        if (KPADReadEx((KPADChan)chan, &ks, 1, &err) < 1 || err != KPAD_ERROR_OK) continue;
        if (ks.extensionType == WPAD_EXT_PRO_CONTROLLER) {
            down |= map_classic(ks.pro.trigger);
            held |= map_classic(ks.pro.hold) | stick_dirs(ks.pro.leftStick.x, ks.pro.leftStick.y);
            continue;
        }
        down |= map_wiimote(ks.trigger);
        held |= map_wiimote(ks.hold);
        if (ks.extensionType == WPAD_EXT_NUNCHUK || ks.extensionType == WPAD_EXT_MPLUS_NUNCHUK) {
            held |= stick_dirs(ks.nunchuk.stick.x, ks.nunchuk.stick.y);
        } else if (ks.extensionType == WPAD_EXT_CLASSIC || ks.extensionType == WPAD_EXT_MPLUS_CLASSIC) {
            down |= map_classic(ks.classic.trigger);
            held |= map_classic(ks.classic.hold) | stick_dirs(ks.classic.leftStick.x, ks.classic.leftStick.y);
        }
        if (!in->pointer && ks.posValid > 0) {
            in->pointer = 1;
            touch_mode = 0;
            in->px = (int)((ks.pos.x + 1.0f) * 0.5f * canvas_w);
            in->py = (int)((ks.pos.y + 1.0f) * 0.5f * canvas_h);
        }
    }

    /* The GamePad: its touch screen is the stylus and wins over a Wii Remote */
    VPADStatus vs;
    VPADReadError verr = VPAD_READ_SUCCESS;
    if (VPADRead(VPAD_CHAN_0, &vs, 1, &verr) > 0 && verr == VPAD_READ_SUCCESS) {
        down |= map_vpad(vs.trigger);
        held |= map_vpad(vs.hold);
        VPADTouchData tp;
        VPADGetTPCalibratedPoint(VPAD_CHAN_0, &tp, &vs.tpNormal);
        if (tp.touched) {
            /* calibrated points are in 1280x720 regardless of the screen's size */
            if (!(tp.validity & VPAD_INVALID_X)) touch_x = tp.x * canvas_w / 1280;
            if (!(tp.validity & VPAD_INVALID_Y)) touch_y = tp.y * canvas_h / 720;
            if (!touch_down) down |= BTN_A;
            touch_down = 1;
            touch_mode = 1;
        } else {
            touch_down = 0;
        }
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
