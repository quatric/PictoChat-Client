#pragma once
#include "platform.h"

/* Every controller is folded into Wii Remote style buttons:
 *   Switch Joy-Cons / Pro Controller / handheld touch screen.
 * Analog sticks act as the D-Pad. Directions auto-repeat while held. */
#define BTN_UP    0x0001
#define BTN_DOWN  0x0002
#define BTN_LEFT  0x0004
#define BTN_RIGHT 0x0008
#define BTN_A     0x0010
#define BTN_B     0x0020
#define BTN_1     0x0040
#define BTN_2     0x0080
#define BTN_PLUS  0x0100
#define BTN_MINUS 0x0200
#define BTN_HOME  0x0400
#define BTN_DIRS  (BTN_UP | BTN_DOWN | BTN_LEFT | BTN_RIGHT)

typedef struct {
    u32 down;     /* pressed this frame (directions include auto-repeat) */
    u32 held;
    int pointer;  /* a Wii Remote is pointed at the screen, or the GamePad is touched */
    int touch;    /* the pointer is a finger or stylus on the GamePad: no cursor, A is held */
    int touch_mode; /* the GamePad screen was touched last: the D-Pad scrolls instead of steering a cursor */
    int px, py;   /* pointer position in canvas pixels */
    int moved;    /* pointer moved noticeably this frame */
} input_t;

/* USB keyboard (HID usage codes, which are also SDL scancodes) */
enum {
    KEY_ENTER = 0x28, KEY_ESC = 0x29, KEY_BACKSPACE = 0x2A, KEY_TAB = 0x2B,
    KEY_DELETE = 0x4C, KEY_HOME = 0x4A, KEY_END = 0x4D, KEY_PAGEUP = 0x4B, KEY_PAGEDOWN = 0x4E,
    KEY_RIGHT = 0x4F, KEY_LEFT = 0x50, KEY_DOWN = 0x51, KEY_UP = 0x52, KEY_KP_ENTER = 0x58,
    KEY_F1 = 0x3A
};

typedef struct {
    u8 code;      /* HID usage code */
    char ch;      /* printable ASCII after Shift/Caps Lock, or 0 */
    u8 shift, ctrl;
} key_event;

void input_init(void);
void input_shutdown(void);
void input_set_canvas(int width, int height);
/* The GamePad picture is turned this far (0, 90 or 270 degrees clockwise) for a
 * GamePad held upright: its D-Pad and stick turn with it */
void input_set_gamepad_turn(int degrees);
void input_poll(input_t *in);
int input_key(key_event *ev); /* pop one keyboard event; 0 when none */
int input_keyboard_connected(void);
int input_quit_requested(void); /* the system asked the app to close */
