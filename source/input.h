#pragma once
#include <gccore.h>
#include <wiiuse/wpad.h>

/* Every controller is folded into Wii Remote button bits, as in GlowWii:
 *   Wii Remote / Classic / Wii U Pro / GamePad / GameCube / DualShock 3.
 * Analog sticks (Nunchuk, Classic, GameCube, Pro, GamePad, DS3) act as the
 * D-Pad. Directions auto-repeat while held. */
#define BTN_UP    WPAD_BUTTON_UP
#define BTN_DOWN  WPAD_BUTTON_DOWN
#define BTN_LEFT  WPAD_BUTTON_LEFT
#define BTN_RIGHT WPAD_BUTTON_RIGHT
#define BTN_A     WPAD_BUTTON_A
#define BTN_B     WPAD_BUTTON_B
#define BTN_1     WPAD_BUTTON_1
#define BTN_2     WPAD_BUTTON_2
#define BTN_PLUS  WPAD_BUTTON_PLUS
#define BTN_MINUS WPAD_BUTTON_MINUS
#define BTN_HOME  WPAD_BUTTON_HOME
#define BTN_DIRS  (BTN_UP | BTN_DOWN | BTN_LEFT | BTN_RIGHT)

typedef struct {
    u32 down;     /* pressed this frame (directions include auto-repeat) */
    u32 held;
    int pointer;  /* a Wii Remote is pointed at the screen */
    int px, py;   /* pointer position in canvas pixels */
    int moved;    /* pointer moved noticeably this frame */
} input_t;

/* USB keyboard (HID usage codes) */
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
void input_poll(input_t *in);
int input_key(key_event *ev); /* pop one keyboard event; 0 when none */
int input_keyboard_connected(void);
