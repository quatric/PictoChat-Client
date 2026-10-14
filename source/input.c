#include <stdlib.h>
#include <unistd.h>
#include <math.h>
#include <ogc/usb.h>
#include <ogc/usbmouse.h>
#include <wiikeyboard/usbkeyboard.h>
#ifdef HAVE_WIIDRC
#include <wiidrc/wiidrc.h>
#endif
#ifdef HAVE_WUPC
#include <wupc/wupc.h>
#endif
#ifdef HAVE_SICKSAXIS
#include <sicksaxis.h>
#endif
#include "input.h"

#define REPEAT_DELAY 22 /* frames before a held direction repeats */
#define REPEAT_RATE  5

static int canvas_w = 640, canvas_h = 480;
static int last_px = -1000, last_py = -1000;
static u32 prev_dirs;
static int dir_frames;

/* USB mouse: a relative-motion pointer. Left = A, right = B, middle = +, wheel = Up/Down. */
static int mouse_x = 320, mouse_y = 240;
static int mouse_active;
static u8 mouse_prev_buttons;

#ifdef HAVE_SICKSAXIS
static struct ss_device ds3;
static int ds3_initialized;
#endif

/* ── Controller mapping (from GlowWii) ───────────────────────────────────── */

static u32 map_classic(u32 buttons)
{
    u32 out = 0;
    if (buttons & WPAD_CLASSIC_BUTTON_ZR) out |= WPAD_BUTTON_PLUS;
    if (buttons & WPAD_CLASSIC_BUTTON_ZL) out |= WPAD_BUTTON_MINUS;
    if (buttons & WPAD_CLASSIC_BUTTON_FULL_R) out |= WPAD_BUTTON_PLUS;
    if (buttons & WPAD_CLASSIC_BUTTON_FULL_L) out |= WPAD_BUTTON_MINUS;
    if (buttons & WPAD_CLASSIC_BUTTON_PLUS)  out |= WPAD_BUTTON_PLUS;
    if (buttons & WPAD_CLASSIC_BUTTON_MINUS) out |= WPAD_BUTTON_MINUS;
    if (buttons & WPAD_CLASSIC_BUTTON_A)     out |= WPAD_BUTTON_A;
    if (buttons & WPAD_CLASSIC_BUTTON_B)     out |= WPAD_BUTTON_B;
    if (buttons & WPAD_CLASSIC_BUTTON_X)     out |= WPAD_BUTTON_2;
    if (buttons & WPAD_CLASSIC_BUTTON_Y)     out |= WPAD_BUTTON_1;
    if (buttons & WPAD_CLASSIC_BUTTON_HOME)  out |= WPAD_BUTTON_HOME;
    if (buttons & WPAD_CLASSIC_BUTTON_UP)    out |= WPAD_BUTTON_UP;
    if (buttons & WPAD_CLASSIC_BUTTON_DOWN)  out |= WPAD_BUTTON_DOWN;
    if (buttons & WPAD_CLASSIC_BUTTON_LEFT)  out |= WPAD_BUTTON_LEFT;
    if (buttons & WPAD_CLASSIC_BUTTON_RIGHT) out |= WPAD_BUTTON_RIGHT;
    return out;
}

#ifdef HAVE_WIIDRC
static u32 map_drc(u32 buttons)
{
    u32 out = 0;
    if (buttons & (WIIDRC_BUTTON_R | WIIDRC_BUTTON_ZR)) out |= WPAD_BUTTON_PLUS;
    if (buttons & (WIIDRC_BUTTON_L | WIIDRC_BUTTON_ZL)) out |= WPAD_BUTTON_MINUS;
    if (buttons & WIIDRC_BUTTON_PLUS)  out |= WPAD_BUTTON_PLUS;
    if (buttons & WIIDRC_BUTTON_MINUS) out |= WPAD_BUTTON_MINUS;
    if (buttons & WIIDRC_BUTTON_A)     out |= WPAD_BUTTON_A;
    if (buttons & WIIDRC_BUTTON_B)     out |= WPAD_BUTTON_B;
    if (buttons & WIIDRC_BUTTON_X)     out |= WPAD_BUTTON_2;
    if (buttons & WIIDRC_BUTTON_Y)     out |= WPAD_BUTTON_1;
    if (buttons & WIIDRC_BUTTON_HOME)  out |= WPAD_BUTTON_HOME;
    if (buttons & WIIDRC_BUTTON_UP)    out |= WPAD_BUTTON_UP;
    if (buttons & WIIDRC_BUTTON_DOWN)  out |= WPAD_BUTTON_DOWN;
    if (buttons & WIIDRC_BUTTON_LEFT)  out |= WPAD_BUTTON_LEFT;
    if (buttons & WIIDRC_BUTTON_RIGHT) out |= WPAD_BUTTON_RIGHT;
    return out;
}
#endif

static u32 map_gc(u32 gc)
{
    u32 out = 0;
    if (gc & PAD_TRIGGER_R) out |= WPAD_BUTTON_PLUS;
    if (gc & PAD_TRIGGER_L) out |= WPAD_BUTTON_MINUS;
    if (gc & PAD_TRIGGER_Z) out |= WPAD_BUTTON_PLUS;
    if (gc & PAD_BUTTON_A) out |= WPAD_BUTTON_A;
    if (gc & PAD_BUTTON_B) out |= WPAD_BUTTON_B;
    if (gc & PAD_BUTTON_X) out |= WPAD_BUTTON_2;
    if (gc & PAD_BUTTON_Y) out |= WPAD_BUTTON_1;
    if (gc & PAD_BUTTON_MENU) out |= WPAD_BUTTON_HOME;
    if (gc & PAD_BUTTON_UP) out |= WPAD_BUTTON_UP;
    if (gc & PAD_BUTTON_DOWN) out |= WPAD_BUTTON_DOWN;
    if (gc & PAD_BUTTON_LEFT) out |= WPAD_BUTTON_LEFT;
    if (gc & PAD_BUTTON_RIGHT) out |= WPAD_BUTTON_RIGHT;
    return out;
}

/* A stick pushed past `threshold` becomes a D-Pad direction (y grows upward). */
static u32 stick_dirs(int x, int y, int threshold)
{
    u32 out = 0;
    if (abs(x) > abs(y)) {
        if (x > threshold) out |= BTN_RIGHT;
        else if (x < -threshold) out |= BTN_LEFT;
    } else {
        if (y > threshold) out |= BTN_UP;
        else if (y < -threshold) out |= BTN_DOWN;
    }
    return out;
}

static u32 joystick_dirs(const joystick_t *js)
{
    if (js->mag < 0.55f) return 0;
    float rad = js->ang * (float)M_PI / 180.0f;
    return stick_dirs((int)(sinf(rad) * 100), (int)(cosf(rad) * 100), 40);
}

#ifdef HAVE_SICKSAXIS
static u32 poll_ds3(void)
{
    if (!ds3_initialized) return 0;
    if (!ss_is_connected(&ds3)) {
        if (ss_open(&ds3) > 0) ss_start_reading(&ds3);
        else return 0;
    }
    const struct SS_BUTTONS *b = &ds3.pad.buttons;
    u32 held = 0;
    if (b->start || b->R1 || b->R2) held |= WPAD_BUTTON_PLUS;
    if (b->select || b->L1 || b->L2) held |= WPAD_BUTTON_MINUS;
    if (b->cross) held |= WPAD_BUTTON_A;
    if (b->circle) held |= WPAD_BUTTON_B;
    if (b->triangle) held |= WPAD_BUTTON_2;
    if (b->square) held |= WPAD_BUTTON_1;
    if (b->PS) held |= WPAD_BUTTON_HOME;
    if (b->up) held |= WPAD_BUTTON_UP;
    if (b->down) held |= WPAD_BUTTON_DOWN;
    if (b->left) held |= WPAD_BUTTON_LEFT;
    if (b->right) held |= WPAD_BUTTON_RIGHT;
    held |= stick_dirs((int)ds3.pad.left_analog.x - 128, 128 - (int)ds3.pad.left_analog.y, 60);
    return held;
}
#endif

/* ── USB keyboard ────────────────────────────────────────────────────────── */

#define KEYQ 64
static key_event keyq[KEYQ];
static volatile int keyq_head, keyq_tail;
static int shift_l, shift_r, ctrl_l, ctrl_r, caps;
static lwp_t kbd_thread_hndl = LWP_THREAD_NULL;
static volatile bool kbd_thread_should_run;

static const char plain[] =   "abcdefghijklmnopqrstuvwxyz1234567890\n\x1b\b\t -=[]\\#;'`,./";
static const char shifted[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ!@#$%^&*()\n\x1b\b\t _+{}|~:\"~<>?";

static char key_char(u8 code, int shift)
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

static void kbd_event(USBKeyboard_event event)
{
    if (event.type != USBKEYBOARD_PRESSED && event.type != USBKEYBOARD_RELEASED) return;
    int pressed = event.type == USBKEYBOARD_PRESSED;
    switch (event.keyCode) {
    case 0xE1: shift_l = pressed; return;
    case 0xE5: shift_r = pressed; return;
    case 0xE0: ctrl_l = pressed; return;
    case 0xE4: ctrl_r = pressed; return;
    }
    if (!pressed) return;
    if (event.keyCode == 0x39) { /* Caps Lock */
        caps = !caps;
        USBKeyboard_SetLed(USBKEYBOARD_LEDCAPS, caps);
        return;
    }
    int next = (keyq_head + 1) % KEYQ;
    if (next == keyq_tail) return; /* full */
    key_event *ev = &keyq[keyq_head];
    ev->code = event.keyCode;
    ev->shift = shift_l || shift_r;
    ev->ctrl = ctrl_l || ctrl_r;
    ev->ch = ev->ctrl ? 0 : key_char(event.keyCode, ev->shift);
    keyq_head = next;
}

static void *kbd_thread(void *arg)
{
    (void)arg;
    while (kbd_thread_should_run) {
        if (!USBKeyboard_IsConnected() && USBKeyboard_Open(kbd_event)) {
            for (int i = 0; i < 3; i++) {
                USBKeyboard_SetLed(i, 1);
                usleep(120000);
                USBKeyboard_SetLed(i, 0);
            }
            caps = 0;
        }
        USBKeyboard_Scan();
        usleep(400);
    }
    return NULL;
}

#ifdef AUTOTEST
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
    if (keyq_tail == keyq_head) return 0;
    *ev = keyq[keyq_tail];
    keyq_tail = (keyq_tail + 1) % KEYQ;
    return 1;
}

int input_keyboard_connected(void)
{
    return USBKeyboard_IsConnected();
}

/* ── Setup and polling ───────────────────────────────────────────────────── */

void input_init(void)
{
    PAD_Init();
#ifdef HAVE_WUPC
    WUPC_Init();
#endif
    WPAD_Init();
    WPAD_SetDataFormat(WPAD_CHAN_ALL, WPAD_FMT_BTNS_ACC_IR);
#ifdef HAVE_WIIDRC
    WiiDRC_Init();
#endif
    USB_Initialize();
    MOUSE_Init();
#ifdef HAVE_SICKSAXIS
    if (ss_init() >= 0 && ss_initialize(&ds3) >= 0) {
        ds3_initialized = 1;
        if (ss_open(&ds3) > 0) ss_start_reading(&ds3);
    }
#endif
    USBKeyboard_Initialize();
    kbd_thread_should_run = true;
    LWP_CreateThread(&kbd_thread_hndl, kbd_thread, NULL, NULL, 0x4000, 0x7F);
}

void input_set_canvas(int width, int height)
{
    canvas_w = width;
    canvas_h = height;
    WPAD_SetVRes(WPAD_CHAN_ALL, width, height);
}

void input_shutdown(void)
{
    kbd_thread_should_run = false;
    usleep(400);
    MOUSE_Deinit();
    USBKeyboard_Close();
    USBKeyboard_Deinitialize();
    if (kbd_thread_hndl != LWP_THREAD_NULL) LWP_JoinThread(kbd_thread_hndl, NULL);
    kbd_thread_hndl = LWP_THREAD_NULL;
#ifdef HAVE_SICKSAXIS
    if (ds3_initialized) {
        if (ss_is_connected(&ds3)) ss_stop_reading(&ds3);
        ss_close(&ds3);
    }
#endif
#ifdef HAVE_WUPC
    WUPC_Shutdown();
#endif
    WPAD_Shutdown();
}

void input_poll(input_t *in)
{
    u32 down = 0, held = 0;

#ifdef HAVE_WUPC
    WUPC_UpdateButtonStats();
    for (int chan = 0; chan < 4; chan++) {
        down |= map_classic(WUPC_ButtonsDown(chan));
        held |= map_classic(WUPC_ButtonsHeld(chan));
        if (WUPC_Data(chan)) held |= stick_dirs(WUPC_lStickX(chan), WUPC_lStickY(chan), 400);
    }
#endif

    in->pointer = 0;
    WPAD_ScanPads();
    for (int chan = 0; chan < 4; chan++) {
        WPADData *wd = WPAD_Data(chan);
        if (!wd || wd->err != WPAD_ERR_NONE) continue;
        u32 d = WPAD_ButtonsDown(chan), h = WPAD_ButtonsHeld(chan);
        down |= d | map_classic(d);
        held |= h | map_classic(h);
        if (wd->exp.type == WPAD_EXP_NUNCHUK) held |= joystick_dirs(&wd->exp.nunchuk.js);
        else if (wd->exp.type == WPAD_EXP_CLASSIC) held |= joystick_dirs(&wd->exp.classic.ljs);
        if (!in->pointer && wd->ir.valid) {
            in->pointer = 1;
            in->px = (int)wd->ir.x;
            in->py = (int)wd->ir.y;
        }
    }

#ifdef HAVE_WIIDRC
    if (WiiDRC_Inited() && WiiDRC_Connected() && WiiDRC_ScanPads()) {
        down |= map_drc(WiiDRC_ButtonsDown());
        held |= map_drc(WiiDRC_ButtonsHeld());
        held |= stick_dirs(WiiDRC_lStickX(), WiiDRC_lStickY(), 50);
    }
#endif
#ifdef HAVE_SICKSAXIS
    {
        static u32 ds3_prev;
        u32 h = poll_ds3();
        down |= h & ~ds3_prev;
        held |= h;
        ds3_prev = h;
    }
#endif

    PAD_ScanPads();
    for (int chan = 0; chan < 4; chan++) {
        down |= map_gc(PAD_ButtonsDown(chan));
        held |= map_gc(PAD_ButtonsHeld(chan));
        held |= stick_dirs(PAD_StickX(chan), PAD_StickY(chan), 45);
    }

    u32 wheel_dirs = 0;
    int mouse_moved = 0;
    {
        mouse_event me;
        u8 buttons = mouse_prev_buttons;
        while (MOUSE_GetEvent(&me)) {
            if (me.rx || me.ry) {
                mouse_x += me.rx * 2;
                mouse_y += me.ry * 2;
                mouse_moved = 1;
            }
            if (me.rz > 0) wheel_dirs = BTN_DOWN;
            else if (me.rz < 0) wheel_dirs = BTN_UP;
            buttons = me.button;
        }
        if (mouse_x < 0) mouse_x = 0;
        if (mouse_y < 0) mouse_y = 0;
        if (mouse_x >= canvas_w) mouse_x = canvas_w - 1;
        if (mouse_y >= canvas_h) mouse_y = canvas_h - 1;
        u32 mh = 0;
        if (buttons & 1) mh |= WPAD_BUTTON_A;
        if (buttons & 2) mh |= WPAD_BUTTON_B;
        if (buttons & 4) mh |= WPAD_BUTTON_PLUS;
        u32 prevh = 0;
        if (mouse_prev_buttons & 1) prevh |= WPAD_BUTTON_A;
        if (mouse_prev_buttons & 2) prevh |= WPAD_BUTTON_B;
        if (mouse_prev_buttons & 4) prevh |= WPAD_BUTTON_PLUS;
        down |= mh & ~prevh;
        held |= mh;
        mouse_prev_buttons = buttons;
        if (mouse_moved || (mh & ~prevh)) mouse_active = 1;
        if (!MOUSE_IsConnected()) mouse_active = 0;
        if (!in->pointer && mouse_active) {
            in->pointer = 1;
            in->px = mouse_x;
            in->py = mouse_y;
        } else if (in->pointer) {
            mouse_x = in->px; /* hand over smoothly between Wii Remote and mouse */
            mouse_y = in->py;
            if (mouse_moved) mouse_active = 1;
        }
    }

    /* Directions: a new press fires at once, then repeats while held. */
    u32 dirs = held & BTN_DIRS;
    u32 fresh = (dirs & ~prev_dirs) | (down & BTN_DIRS);
    if (dirs && dirs == prev_dirs) dir_frames++;
    else dir_frames = 0;
    if (dir_frames >= REPEAT_DELAY && (dir_frames - REPEAT_DELAY) % REPEAT_RATE == 0) fresh |= dirs;
    prev_dirs = dirs;

    in->down = (down & ~BTN_DIRS) | fresh | wheel_dirs;
    in->held = held;

    if (in->pointer) {
        if (in->px < 0) in->px = 0;
        if (in->py < 0) in->py = 0;
        if (in->px >= canvas_w) in->px = canvas_w - 1;
        if (in->py >= canvas_h) in->py = canvas_h - 1;
        in->moved = abs(in->px - last_px) > 3 || abs(in->py - last_py) > 3; /* ignore IR jitter */
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
