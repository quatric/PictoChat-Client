/* The screens: loading, setup (name and colour), captcha pairing, the room
 * list and the chat room itself. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <3ds.h>
#include <3dsupdate.h>
#include "app.h"
#include "assets.h"
#include "chat.h"
#include "compose.h"
#include "font.h"
#include "gfx.h"
#include "input.h"
#include "msglog.h"
#include "net.h"
#include "pair.h"
#include "qrcodegen.h"
#include "settings.h"
#include "sound.h"
#include "stage.h"

#ifdef AUTOTEST
extern int autotest_quit;
void autotest_log(const char *fmt, ...);
#define TLOG(...) autotest_log(__VA_ARGS__)
#else
#define TLOG(...) ((void)0)
#endif

/* pict.chat's colour swatches */
static const u32 SWATCHES[16] = {
    0x61829a, 0xba4900, 0xfb0018, 0xfb8afb, 0xfb9200, 0xf3e300, 0xaafb00, 0x00fb00,
    0x00a238, 0x49db8a, 0x30baf3, 0x0059f3, 0x000092, 0x8a00d3, 0xd300eb, 0xfb0092,
};
static const char *const ROOM_IDS[5] = { "room_a", "room_b", "room_c", "room_d", "room_e" };

typedef enum { SCR_LOADING, SCR_SETUP, SCR_NAME, SCR_PAIR, SCR_CONNECTING, SCR_LOBBY, SCR_ROOM, SCR_CLOSED } screen_t;

static screen_t screen = SCR_LOADING;
static int frame;
static int quit;
static char status[192];        /* a message on the setup screen */
static char net_ip[16];
static int net_ok = -1;

/* the player as pict.chat knows them */
static char my_name[32];
static u32 my_color;

/* lobby and room */
static int counts[4];
static int room_shift, room_shift_target; /* lobby list scroll, 0 or 32 */
static int room_index = -1;               /* 0..4 while in a room */
static int left_room = -1;
static int private_room;
static int fade;                          /* 0..255 */
static int closed_y;
static screen_t closed_from;
static int join_wait;                     /* frames since asking to join */

/* name entry */
static char name_edit[16];
static int name_shift;

/* the stylus */
static int touching;
static float touch_x, touch_y;

/* ── Helpers ─────────────────────────────────────────────────────────────── */

static void toggle_scale(void);
void ui_spinner_text(int x, int y, int w, int f, const char *text);

static int valid_name_char(int c) { return isalnum(c) || c == '_'; }

static void sanitize_name(char *s)
{
    char *w = s;
    for (char *r = s; *r; r++)
        if (valid_name_char((unsigned char)*r)) *w++ = *r;
    *w = 0;
    if (strlen(s) > 10) s[10] = 0;
}

static void center(int y, u32 color, const char *text)
{
    ds_text((STAGE_W - ds_width(text)) / 2, y, color, text);
}

static void button(int x, int y, int w, int h, const char *text, u32 color, int pressed)
{
    gfx_fill(x, y, w, h, color);
    gfx_fill(x + 1, y + 1, w - 2, h - 2, pressed ? brighten(color, 40) : 0xFBFBFB);
    ds_text(x + (w - ds_width(text)) / 2, y + (h - 12) / 2, 0x000000, text);
}

static int in_rect(float x, float y, int rx, int ry, int rw, int rh)
{
    return x >= rx && y >= ry && x < rx + rw && y < ry + rh;
}

/* ── Network ─────────────────────────────────────────────────────────────── */

static void begin_connect(void)
{
    if (net_ok != 0) net_ok = net_start(net_ip, sizeof net_ip);
    if (net_ok != 0) {
        snprintf(status, sizeof status, "The 3DS is not connected to the Internet. Turn on Wi-Fi and try again.");
        sound_play(SND_ERROR);
        screen = SCR_SETUP;
        return;
    }
    snprintf(my_name, sizeof my_name, "%s", settings.name);
    my_color = settings.color;
    status[0] = 0;
    if (settings.captcha) {
        pair_start(settings.pair);
        screen = SCR_PAIR;
    } else {
        chat_connect(settings.chat, settings.site);
        screen = SCR_CONNECTING;
    }
}

static void connection_lost(const char *why)
{
    snprintf(status, sizeof status, "%s", why);
    closed_from = screen;
    closed_y = STAGE_H;
    screen = SCR_CLOSED;
    chat_disconnect();
}

static void enter_lobby(void)
{
    screen = SCR_LOBBY;
    fade = 0;
    room_index = -1;
}

static void handle_events(void)
{
    chat_event ev;
    while (chat_poll(&ev)) {
        TLOG("event %d %s", ev.type, ev.name);
        switch (ev.type) {
        case EV_OPEN:
            chat_verify(my_name, my_color, settings.captcha ? pair_token() : "");
            break;
        case EV_NAME_VERIFIED:
            snprintf(my_name, sizeof my_name, "%s", ev.name);
            my_color = ev.color;
            snprintf(settings.name, sizeof settings.name, "%s", ev.name);
            settings.color = ev.color;
            settings_save();
            sound_play(SND_START_APP);
            enter_lobby();
            break;
        case EV_ROOMS:
            memcpy(counts, ev.counts, sizeof counts);
            break;
        case EV_ROOM_DATA: {
            int idx = ev.room[5] >= 'a' && ev.room[5] <= 'e' ? ev.room[5] - 'a' : 0;
            room_index = idx;
            private_room = ev.private_room;
            compose_reset(my_name, my_color);
            screen = SCR_ROOM;
            fade = 0;
            join_wait = 0;
            sound_play(SND_JOIN_ROOM);
            break;
        }
        case EV_PLAYER_JOINED:
            if (room_index >= 0) {
                log_add_banner(IMG_ENTER_A + room_index, ev.name, 89);
                sound_play(SND_PLAYER_JOIN);
            }
            break;
        case EV_PLAYER_LEFT:
            if (room_index >= 0) {
                log_add_banner(IMG_LEAVE_A + room_index, ev.name, 82);
                sound_play(SND_PLAYER_LEAVE);
            }
            break;
        case EV_MESSAGE:
            log_add_message(&ev.msg);
            pc_msg_free(&ev.msg);
            sound_play(SND_RECEIVE);
            break;
        case EV_LEFT_ROOM: /* the server moved us out */
            if (room_index >= 0 || left_room >= 0)
                log_add_banner(IMG_LEAVE_A + (room_index >= 0 ? room_index : left_room), my_name, 82);
            if (screen == SCR_ROOM) enter_lobby();
            break;
        case EV_CLOSED:
            if (screen == SCR_CONNECTING || screen == SCR_PAIR) {
                /* pict.chat hangs up when it does not accept the captcha */
                snprintf(status, sizeof status, "PictoChat didn't let you in. The captcha may have expired, so please try again.");
                chat_disconnect();
                screen = SCR_SETUP;
                sound_play(SND_ERROR);
            } else if (screen != SCR_CLOSED && screen != SCR_SETUP) {
                connection_lost(ev.text);
            }
            break;
        }
    }
}

static void leave_room(void)
{
    chat_leave();
    sound_play(SND_LEAVE_ROOM);
    left_room = room_index;
    enter_lobby();
}

/* ── Touch handling per screen ───────────────────────────────────────────── */

#define NAME_BOX_X 20
#define NAME_BOX_Y 222
#define SWATCH_Y 262
#define DARK_Y 298
#define JOIN_Y 322

static void toggle_dark(void)
{
    settings.dark = !settings.dark;
    stage_set_dark(settings.dark);
    settings_save();
    sound_play(SND_PEN);
}

static void setup_touch(int phase, float x, float y)
{
    if (phase != TOUCH_UP) return;
    if (in_rect(x, y, NAME_BOX_X, NAME_BOX_Y, 216, 18)) {
        snprintf(name_edit, sizeof name_edit, "%s", settings.name);
        name_shift = 0;
        screen = SCR_NAME;
        sound_play(SND_KEY_UP);
        return;
    }
    for (int i = 0; i < 16; i++) {
        int sx = 20 + (i % 8) * 27, sy = SWATCH_Y + (i / 8) * 17;
        if (in_rect(x, y, sx, sy, 24, 14)) {
            settings.color = SWATCHES[i];
            sound_play(SND_PEN);
            return;
        }
    }
    if (in_rect(x, y, 16, DARK_Y - 3, 100, 18)) {
        toggle_dark();
        return;
    }
    if (in_rect(x, y, 64, JOIN_Y, 128, 26)) {
        if (!settings.name[0]) {
            sound_play(SND_ERROR);
            snprintf(status, sizeof status, "Choose a name first.");
            return;
        }
        settings_save();
        begin_connect();
    }
}

static void name_finish(int keep)
{
    if (keep) {
        sanitize_name(name_edit);
        snprintf(settings.name, sizeof settings.name, "%s", name_edit);
        settings_save();
    }
    screen = SCR_SETUP;
    sound_play(SND_SCROLL);
}

static void name_type(int c)
{
    if (!valid_name_char(c)) {
        sound_play(SND_ERROR);
        return;
    }
    size_t n = strlen(name_edit);
    if (n >= 10) {
        sound_play(SND_ERROR);
        return;
    }
    name_edit[n] = c;
    name_edit[n + 1] = 0;
    sound_play(SND_KEY_UP);
}

static void name_backspace(void)
{
    size_t n = strlen(name_edit);
    if (n) name_edit[n - 1] = 0;
    sound_play(SND_KEY_UP);
}

static int name_pressed = -1;

static void name_touch(int phase, float x, float y)
{
    int key = keyboard_key_at(x, y);
    if (phase == TOUCH_DOWN) {
        name_pressed = key;
        if (key >= 0) sound_play(SND_KEY_DOWN);
        else if (in_rect(x, y, 225, 296, 31, 30)) name_pressed = 100;
        return;
    }
    if (phase != TOUCH_UP) return;
    int was = name_pressed;
    name_pressed = -1;
    if (was == 100 && in_rect(x, y, 225, 296, 31, 30)) {
        name_finish(1);
        return;
    }
    if (was < 0 || was != key) return;
    const char *label = keyboard_label(name_shift, key);
    if (!strcmp(label, "BACKSPACE")) name_backspace();
    else if (!strcmp(label, "ENTER")) name_finish(1);
    else if (!strcmp(label, "SHIFT")) name_shift = name_shift == 1 ? 0 : 1;
    else if (!strcmp(label, "CAPS")) name_shift = name_shift == 2 ? 0 : 2;
    else if (label[0] && !label[1]) {
        name_type(label[0] == '-' && name_shift == 1 ? '_' : label[0]);
        if (name_shift == 1) name_shift = 0;
    } else {
        sound_play(SND_ERROR);
    }
}

static void lobby_touch(int phase, float x, float y)
{
    if (phase != TOUCH_UP || fade < 255 || join_wait > 0) return;
    if (in_rect(x, y, 233, 337, 21, 21)) {
        room_shift_target = room_shift_target ? 0 : 32;
        sound_play(SND_SCROLL);
        return;
    }
    for (int i = 0; i < 5; i++) {
        int ry = 224 + i * 32 - room_shift;
        if (ry < 224 || ry + 32 > 352) continue;
        if (in_rect(x, y, 31, ry, 192, 32)) {
            if (i < 4 && counts[i] >= 16) {
                sound_play(SND_ERROR);
                return;
            }
            chat_join(ROOM_IDS[i]);
            join_wait = 1;
            sound_play(SND_KEY_UP);
            return;
        }
    }
}

static void room_touch(int phase, float x, float y)
{
    if (fade < 255) return;
    switch (compose_touch(phase, x, y)) {
    case COMPOSE_EXIT: leave_room(); break;
    case COMPOSE_SCROLL_UP: log_scroll(-1); break;
    case COMPOSE_SCROLL_DOWN: log_scroll(1); break;
    case COMPOSE_SCROLL_END: log_scroll_end(); break;
    case COMPOSE_SENT:
        chat_send(compose_last_sent());
        log_add_message(compose_last_sent());
        log_scroll_end();
        break;
    case COMPOSE_RENEW: compose_renew(log_current()); break;
    }
}

static void send_now(void)
{
    if (compose_send() == COMPOSE_SENT) {
        chat_send(compose_last_sent());
        log_add_message(compose_last_sent());
        log_scroll_end();
    }
}

static void touch(int phase, float x, float y)
{
    switch (screen) {
    case SCR_SETUP: setup_touch(phase, x, y); break;
    case SCR_NAME: name_touch(phase, x, y); break;
    case SCR_LOBBY: lobby_touch(phase, x, y); break;
    case SCR_ROOM: room_touch(phase, x, y); break;
    case SCR_CLOSED:
        if (phase == TOUCH_UP) screen = SCR_SETUP;
        break;
    default: break;
    }
}

/* ── Input ───────────────────────────────────────────────────────────────── */

/* The system keyboard, for typing faster than on the DS keyboards */
static void swkbd_name(void)
{
    char text[64];
    if (!input_swkbd("Your name (letters, numbers and _)", screen == SCR_NAME ? name_edit : settings.name, 10, 1, text,
                     sizeof text))
        return;
    sanitize_name(text);
    snprintf(name_edit, sizeof name_edit, "%s", text);
    name_finish(1);
}

static void swkbd_message(void)
{
    char text[512];
    if (!input_swkbd("Message", NULL, 200, 0, text, sizeof text)) return;
    for (const char *p = text; *p;) {
        char ch[8];
        int n = utf8_encode(utf8_next(&p), ch);
        ch[n] = 0;
        if (ch[0] != '\r') compose_type(ch);
    }
    sound_play(SND_KEY_UP);
}

static void handle_input(const input_t *in)
{
    /* the stylus on the DS bottom screen */
    float sx, sy;
    int inside = in->touch && stage_from_touch(in->tx, in->ty, &sx, &sy);
    if (in->touch && !touching) {
        if (inside) {
            touching = 1;
            touch_x = sx;
            touch_y = sy;
            touch(TOUCH_DOWN, sx, sy);
        }
    } else if (touching && in->touch) {
        /* keep drawing along the edge when the stylus slides off the DS screen */
        touch_x = sx < 0 ? 0 : sx >= STAGE_W ? STAGE_W - 1 : sx;
        touch_y = sy < 192 ? 192 : sy >= STAGE_H ? STAGE_H - 1 : sy;
        touch(TOUCH_HELD, touch_x, touch_y);
    } else if (touching) {
        touching = 0;
        touch(TOUCH_UP, touch_x, touch_y);
    }

    if (in->down & BTN_SELECT) toggle_scale();

    /* buttons */
    switch (screen) {
    case SCR_SETUP:
        if (in->down & BTN_A) setup_touch(TOUCH_UP, 70, JOIN_Y + 5);
        if (in->down & BTN_X) toggle_dark();
        if (in->down & BTN_Y) swkbd_name();
        if (in->down & BTN_START) quit = 1;
        break;
    case SCR_NAME:
        if (in->down & BTN_B) name_backspace();
        if (in->down & (BTN_A | BTN_START)) name_finish(1);
        if (in->down & BTN_Y) swkbd_name();
        break;
    case SCR_PAIR:
    case SCR_CONNECTING:
        if (in->down & (BTN_B | BTN_START)) {
            pair_cancel();
            chat_disconnect();
            screen = SCR_SETUP;
        }
        break;
    case SCR_LOBBY:
        if (in->down & (BTN_B | BTN_START)) {
            chat_disconnect();
            screen = SCR_SETUP;
        }
        if (in->down & (BTN_UP | BTN_DOWN)) {
            room_shift_target = (in->down & BTN_DOWN) ? 32 : 0;
            sound_play(SND_SCROLL);
        }
        break;
    case SCR_ROOM:
        if (fade < 255) break;
        if (in->down & BTN_START) {
            leave_room();
            break;
        }
        if (in->down & BTN_A) send_now();
        if (in->down & BTN_B) {
            compose_backspace();
            sound_play(SND_KEY_UP);
        }
        if (in->down & BTN_X) {
            compose_clear();
            sound_play(SND_CLEAR);
        }
        if (in->down & BTN_Y) swkbd_message();
        if (in->down & BTN_L) compose_next_keyboard();
        if (in->down & BTN_R) compose_renew(log_current());
        if (in->down & BTN_UP) log_scroll(-1), sound_play(SND_SCROLL);
        if (in->down & BTN_DOWN) log_scroll(1), sound_play(SND_SCROLL);
        if (in->down & BTN_LEFT) compose_toggle_pen_size();
        if (in->down & BTN_RIGHT) compose_toggle_pen_mode();
        break;
    case SCR_CLOSED:
        if (in->down & (BTN_A | BTN_B | BTN_START)) screen = SCR_SETUP;
        break;
    default:
        if (in->down & BTN_START) quit = 1;
        break;
    }
}

/* ── Drawing ─────────────────────────────────────────────────────────────── */

static void draw_top_screen(void)
{
    stage_blit_region(img[IMG_BACKGROUND], 0, 0, 256, 192, 0, 0, 0xFFFFFF, 255);
    log_draw();
}

static void draw_setup(void)
{
    /* the intro is pict.chat's HTML page: inverted, but not hue-flipped */
    stage_flip(0);
    stage_blit(img[IMG_INTRO_BG], 0, 0, 0xFFFFFF, 255);
    if (img[IMG_LOGO]) stage_blit(img[IMG_LOGO], (STAGE_W - img[IMG_LOGO]->w) / 2, 22, 0xFFFFFF, 255);
    stage_flip(1);
    center(76, 0x000000, "PictoChat Online for 3DS");
    center(100, 0x404040, "Public rooms are public and logged.");
    center(114, 0x404040, "Flagged messages go to moderators.");
    center(128, 0x404040, "Don't share private info. Be kind!");
    center(162, 0x707070, "pict.chat by ayunami2000");

    u32 c = dk(settings.color);
    ds_text(NAME_BOX_X, NAME_BOX_Y - 14, 0x000000, "Username:");
    gfx_fill(NAME_BOX_X, NAME_BOX_Y, 216, 18, c);
    gfx_fill(NAME_BOX_X + 1, NAME_BOX_Y + 1, 214, 16, 0xFBFBFB);
    ds_text(NAME_BOX_X + 5, NAME_BOX_Y + 3, settings.name[0] ? c : 0x9A9A9A, settings.name[0] ? settings.name : "Choose a name");

    ds_text(20, SWATCH_Y - 14, 0x000000, "Color:");
    for (int i = 0; i < 16; i++) {
        int sx = 20 + (i % 8) * 27, sy = SWATCH_Y + (i / 8) * 17;
        if (SWATCHES[i] == settings.color) gfx_fill(sx - 2, sy - 2, 28, 18, 0x000000);
        gfx_fill(sx, sy, 24, 14, dk(SWATCHES[i]));
    }
    gfx_fill(20, DARK_Y, 11, 11, 0x000000);
    gfx_fill(21, DARK_Y + 1, 9, 9, 0xFBFBFB);
    if (settings.dark) gfx_fill(23, DARK_Y + 3, 5, 5, 0x000000);
    ds_text(36, DARK_Y, 0x000000, "Dark Mode");
    button(64, JOIN_Y, 128, 26, "Join PictoChat", c, touching && in_rect(touch_x, touch_y, 64, JOIN_Y, 128, 26));
    if (status[0]) {
        int starts[3], lens[3];
        int n = text_wrap(FONT_DS, 10, status, 230, starts, lens, 3);
        for (int i = 0; i < n; i++) ds_text_n(13, 352 + i * 12, 0xC00000, status + starts[i], lens[i]);
    }
}

static void draw_name(void)
{
    stage_flip(0);
    stage_blit(img[IMG_INTRO_BG], 0, 0, 0xFFFFFF, 255);
    stage_flip(1);
    center(70, 0x000000, "Type your name:");
    u32 c = dk(settings.color);
    gfx_fill(48, 88, 160, 22, c);
    gfx_fill(49, 89, 158, 20, 0xFBFBFB);
    int w = ds_width(name_edit);
    ds_text(128 - w / 2, 94, c, name_edit);
    if ((frame / 30) % 2 == 0) gfx_fill(128 - w / 2 + w + 1, 93, 1, 12, c);
    center(124, 0x404040, "Letters, numbers and _ (up to 10)");

    stage_blit(img[IMG_BOTTOM_SCREEN], 0, 192, 0xFFFFFF, 255);
    /* just the keyboard part of the room screen */
    stage_blit_region(img[IMG_DRAWUI], 18, 100, 238, 92, 18, 292, 0xFFFFFF, 255);
    if (name_shift) stage_blit_region(img[name_shift == 1 ? IMG_SHIFT : IMG_CAPS], 18, 100, 202, 92, 18, 292, 0xFFFFFF, 255);
    ds_text(14, 212, 0x000000, "Press SEND or START when you're done.");
    ds_text(14, 226, 0x404040, "Y: the system keyboard");
    if (name_pressed == 100) gfx_fill_alpha(225, 296, 31, 30, c, 102);
}

static void draw_lobby(void)
{
    draw_top_screen();
    stage_blit(img[IMG_BOTTOM_SCREEN], 0, 192, 0xFFFFFF, 255);
    int a = fade;
    for (int i = 0; i < 5; i++) {
        int ry = 224 + i * 32 - room_shift;
        stage_blit(img[IMG_ROOM_A + i], 31, ry, 0xFFFFFF, a);
        if (i < 4) {
            char n[8];
            snprintf(n, sizeof n, "%d/16", counts[i]);
            ds_text(161, 234 + i * 32 - room_shift, 0x000000, n);
        }
    }
    stage_blit(img[IMG_CHOOSE], 0, 192, 0xFFFFFF, a);
    stage_blit(img[IMG_CHOOSE_MASK], 0, 192, dk(my_color), a);
    if (img[IMG_SCROLL]) {
        /* the arrow flips once the list has scrolled */
        image_t *s = img[IMG_SCROLL];
        for (int y = 0; y < s->h; y++)
            stage_blit_region(s, 0, room_shift_target ? s->h - 1 - y : y, s->w, 1, 240, 344 + y, 0xFFFFFF, a);
    }
    if (join_wait > 0) center(366, 0x404040, "Joining...");
}

static void draw_room(void)
{
    draw_top_screen();
    stage_blit(img[IMG_BOTTOM_SCREEN], 0, 192, 0xFFFFFF, 255);
    compose_draw(fade);
    stage_blit(img[IMG_CONNECTION], 1, 1, 0xFFFFFF, fade);
    if (room_index >= 0) stage_blit(img[IMG_LETTER_A + room_index], 1, 175, 0xFFFFFF, fade);
}

static void draw_closed(void)
{
    if (closed_from == SCR_ROOM) draw_room();
    else draw_lobby();
    stage_blit(img[IMG_CONNECTION_BAD], 1, 1, 0xFFFFFF, 255);
    stage_blit(img[IMG_CONNECTION_CLOSED], 24, closed_y, 0xFFFFFF, 255);
}

/* The tiled page background around the DS screens, with a thin frame */
static void draw_margins(int which)
{
    int x, y, w, h;
    gfx_target(which);
    stage_rect(which, &x, &y, &w, &h);
    if (img[IMG_BODY_BG]) {
        for (int ty = 0; ty < SH; ty += img[IMG_BODY_BG]->h)
            for (int tx = 0; tx < SW; tx += img[IMG_BODY_BG]->w) gfx_blit(img[IMG_BODY_BG], tx, ty);
    } else {
        gfx_fill(0, 0, SW, SH, 0xD8D8D8);
    }
    if (x > 0) {
        gfx_fill(x - 1, y, 1, h, 0x000000);
        gfx_fill(x + w, y, 1, h, 0x000000);
    }
    if (y > 0) {
        gfx_fill(x - 1, y - 1, w + 2, 1, 0x000000);
        gfx_fill(x - 1, y + h, w + 2, 1, 0x000000);
    }
}

/* Captcha pairing: the code on the top screen, the QR code on the touch
 * screen. Drawn in stage pixels. */
static void draw_pairing(void)
{
    gfx_fill(0, 0, STAGE_W, STAGE_H, 0xFBFBFB);
    int cx = 10, cw = STAGE_W - 20, ty = 6;
    text_draw_center(FONT_BOLD, 15, 0, ty, STAGE_W, 0x202020, "Join PictoChat");
    ty += 30;
    pair_state_t st = pair_state();
    if (st == PAIR_REQUESTING) {
        text_draw_center(FONT_REGULAR, 12, 0, 80, STAGE_W, 0x404040, "Getting a code...");
    } else if (st == PAIR_FAILED) {
        int starts[6], lens[6];
        int n = text_wrap(FONT_REGULAR, 12, pair_error(), cw, starts, lens, 6);
        for (int i = 0; i < n; i++) text_draw_n(FONT_REGULAR, 12, cx, ty + i * 18, 0xC00000, pair_error() + starts[i], lens[i]);
    } else {
        const char *intro = "PictoChat asks for a captcha. On your phone or computer, open";
        int starts[3], lens[3];
        int n = text_wrap(FONT_REGULAR, 11, intro, cw, starts, lens, 3);
        for (int i = 0; i < n; i++) text_draw_n(FONT_REGULAR, 11, cx, ty + i * 16, 0x303030, intro + starts[i], lens[i]);
        ty += n * 16 + 4;
        const char *host = settings.pair;
        if (!strncmp(host, "https://", 8)) host += 8;
        else if (!strncmp(host, "http://", 7)) host += 7;
        char where[160];
        snprintf(where, sizeof where, "%s/pictochat", host);
        text_draw_center(FONT_BOLD, 12, 0, ty, STAGE_W, 0x0059F3, where);
        ty += 22;
        text_draw_center(FONT_REGULAR, 11, 0, ty, STAGE_W, 0x303030, "and enter this code:");
        ty += 20;
        char spaced[32] = "";
        for (const char *c = pair_code(); *c; c++) {
            size_t l = strlen(spaced);
            spaced[l] = *c;
            spaced[l + 1] = *(c + 1) ? ' ' : 0;
            spaced[l + 2] = 0;
        }
        text_draw_center(FONT_BOLD, 26, 0, ty, STAGE_W, settings.color, spaced);
        text_draw_center(FONT_REGULAR, 10, 0, 174, STAGE_W, 0x707070, "Or scan the QR code below.");

        static uint8_t qr[qrcodegen_BUFFER_LEN_FOR_VERSION(6)], tmp[qrcodegen_BUFFER_LEN_FOR_VERSION(6)];
        static char qr_for[192];
        static int qr_ok;
        if (strcmp(qr_for, pair_url())) {
            snprintf(qr_for, sizeof qr_for, "%s", pair_url());
            qr_ok = qr_for[0] && qrcodegen_encodeText(qr_for, tmp, qr, qrcodegen_Ecc_LOW, 1, 6, qrcodegen_Mask_AUTO, true);
        }
        if (qr_ok) {
            int size = qrcodegen_getSize(qr), room = 192 - 50;
            int m = room / (size + 4);
            if (m > 5) m = 5;
            if (m >= 2) {
                int qw = (size + 4) * m, qx = (STAGE_W - qw) / 2, qy = 192 + 6 + (room - qw) / 2;
                gfx_fill(qx, qy, qw, qw, 0xFFFFFF);
                for (int j = 0; j < size; j++)
                    for (int i = 0; i < size; i++)
                        if (qrcodegen_getModule(qr, i, j)) gfx_fill(qx + (i + 2) * m, qy + (j + 2) * m, m, m, 0x000000);
            }
        }
        ui_spinner_text(0, STAGE_H - 40, STAGE_W, frame, st == PAIR_DONE ? "Joining..." : "Waiting for the captcha...");
    }
    text_draw_center(FONT_REGULAR, 11, 0, STAGE_H - 20, STAGE_W, 0x707070, "B: Back");
}

/* A small activity line: the text and a row of dots */
void ui_spinner_text(int x, int y, int w, int f, const char *text)
{
    int tw = text_width(FONT_REGULAR, 11, text);
    int dots = 6, gap = 6, total = tw + gap + dots * 6;
    int sx = x + (w - total) / 2;
    text_draw(FONT_REGULAR, 11, sx, y, 0x505050, text);
    for (int i = 0; i < dots; i++) {
        int on = (f / 6) % dots == i;
        gfx_fill(sx + tw + gap + i * 6, y + 7, 3, 3, on ? settings.color : 0xC0C0C0);
    }
}

static void draw_connecting(void)
{
    gfx_fill_alpha(0, 192 + 66, STAGE_W, 60, 0xFFFFFF, 220);
    ui_spinner_text(0, 192 + 86, STAGE_W, frame, "Connecting to PictoChat...");
}

/* ── Update popup ────────────────────────────────────────────────────────── */
/* Drawn on the stage, over the top DS screen. */

static ctrupdate_popup update_pop;
static int update_open;

static void draw_update_popup(void)
{
    const ctrupdate_popup *p = &update_pop;
    int w = 240, x = (STAGE_W - w) / 2;
    int ts[3], tl[3], hs[2], hl[2];
    int n = text_wrap(FONT_REGULAR, 11, p->text, w - 20, ts, tl, 3);
    int nh = text_wrap(FONT_REGULAR, 11, p->hint, w - 20, hs, hl, 2);
    int extra = p->progress >= 0 || p->detail[0] ? 20 : 0;
    int h = 32 + n * 16 + extra + nh * 16 + 12, y = (192 - h) / 2;
    gfx_fill_alpha(0, 0, STAGE_W, 192, 0x000000, 110);
    gfx_frame(x, y, w, h, 5, 0xFBFBFB, settings.color, 2);
    text_draw_center(FONT_BOLD, 13, x, y + 8, w, 0x202020, p->title);
    int row = y + 30;
    for (int i = 0; i < n; i++, row += 16) {
        int tw = text_width_n(FONT_REGULAR, 11, p->text + ts[i], tl[i]);
        text_draw_n(FONT_REGULAR, 11, x + (w - tw) / 2, row, 0x404040, p->text + ts[i], tl[i]);
    }
    if (p->progress >= 0) {
        gfx_fill(x + 24, row + 6, w - 48, 8, 0xC8C8C8);
        gfx_fill(x + 24, row + 6, (w - 48) * p->progress / 100, 8, settings.color);
    } else if (p->detail[0]) {
        text_draw_center(FONT_REGULAR, 10, x, row + 2, w, 0x0059F3, p->detail);
    }
    row += extra + 6;
    for (int i = 0; i < nh; i++, row += 16) {
        int tw = text_width_n(FONT_REGULAR, 11, p->hint + hs[i], hl[i]);
        text_draw_n(FONT_REGULAR, 11, x + (w - tw) / 2, row, 0x606060, p->hint + hs[i], hl[i]);
    }
}

static void draw_loading(void)
{
    gfx_target(SCREEN_TOP);
    gfx_fill(0, 0, SW, SH, 0xE8E8E8);
    int p = assets_progress();
    text_draw_center(FONT_BOLD, 22, 0, SH / 2 - 60, SW, 0x202020, "PictoChat for 3DS");
    if (p < 0) {
        int starts[4], lens[4];
        const char *e = assets_error();
        int n = text_wrap(FONT_REGULAR, 14, e, SW - 60, starts, lens, 4);
        for (int i = 0; i < n; i++)
            text_draw_n(FONT_REGULAR, 14, 30, SH / 2 - 10 + i * 22, 0xC00000, e + starts[i], lens[i]);
        text_draw_center(FONT_REGULAR, 13, 0, SH - 36, SW, 0x606060, "Press A to try again, or START to quit.");
    } else {
        text_draw_center(FONT_REGULAR, 14, 0, SH / 2 - 14, SW, 0x404040, "Loading...");
        int bw = SW / 2, bx = (SW - bw) / 2, by = SH / 2 + 18;
        gfx_fill(bx, by, bw, 8, 0xC8C8C8);
        gfx_fill(bx, by, bw * p / 100, 8, 0x30BAF3);
    }
    gfx_target(SCREEN_BOTTOM);
    gfx_fill(0, 0, SW, SH, 0xE8E8E8);
    text_draw_center(FONT_REGULAR, 12, 0, SH - 30, SW, 0x808080, "pict.chat by ayunami2000");
}

static void toggle_scale(void)
{
    settings.pixel = !settings.pixel;
    settings_save();
    sound_play(SND_SCROLL);
}

static void draw(void)
{
    if (screen == SCR_LOADING) {
        /* redraw only when the bar moves, so the loader thread gets the CPU */
        static int shown = -2;
        if (assets_progress() != shown) {
            shown = assets_progress();
            draw_loading();
            gfx_canvas_changed(SCREEN_TOP);
            gfx_canvas_changed(SCREEN_BOTTOM);
            gfx_stage(NULL, 0);
            gfx_present();
        } else {
            gspWaitForVBlank();
        }
        return;
    }
    stage_set_scale(settings.pixel ? SCALE_PIXEL : SCALE_FIT);
#ifdef PROFILE
    u64 t0 = svcGetSystemTick();
#endif
    stage_begin();
    switch (screen) {
    case SCR_SETUP: draw_setup(); break;
    case SCR_NAME: draw_name(); break;
    case SCR_PAIR: draw_pairing(); break;
    case SCR_CONNECTING: draw_setup(), draw_connecting(); break;
    case SCR_LOBBY: draw_lobby(); break;
    case SCR_ROOM: draw_room(); break;
    case SCR_CLOSED: draw_closed(); break;
    default: break;
    }
    if (update_open) draw_update_popup();
    stage_end();

    /* the margins only change with the layout */
    static int last_scale = -1;
    if (stage_scale() != last_scale) {
        draw_margins(SCREEN_TOP);
        draw_margins(SCREEN_BOTTOM);
        gfx_canvas_changed(SCREEN_TOP);
        gfx_canvas_changed(SCREEN_BOTTOM);
        last_scale = stage_scale();
    }
    /* keep the QR code dark on light so phones can read it */
    stage_present(screen != SCR_PAIR);
#ifdef PROFILE
    u64 t1 = svcGetSystemTick();
#endif
    gfx_present();
#ifdef PROFILE
    /* make PROFILE=1: average CPU time per frame, in sdmc:/pictochat-profile.txt */
    static u64 draw_ticks, present_ticks, frames, last_end, loop_ticks;
    u64 end = svcGetSystemTick();
    draw_ticks += t1 - t0;
    present_ticks += end - t1;
    if (last_end) loop_ticks += end - last_end;
    last_end = end;
    if (++frames == 120) {
        FILE *f = fopen("sdmc:/pictochat-profile.txt", "a");
        if (f) {
            extern u64 prof_upload;
            fprintf(f, "upload %llu us\n", prof_upload / frames * 1000000 / SYSCLOCK_ARM11);
            prof_upload = 0;
            fprintf(f, "screen %d: draw %llu us, present %llu us, whole frame %llu us\n", screen,
                    draw_ticks / frames * 1000000 / SYSCLOCK_ARM11, present_ticks / frames * 1000000 / SYSCLOCK_ARM11,
                    loop_ticks / frames * 1000000 / SYSCLOCK_ARM11);
            fclose(f);
        }
        draw_ticks = present_ticks = frames = loop_ticks = 0;
    }
#endif
}

/* ── Main loop ───────────────────────────────────────────────────────────── */

static void update(void)
{
    frame++;
    switch (screen) {
    case SCR_LOADING: {
        if (frame == 2) assets_start();
        int p = assets_progress();
        if (p == 100) {
            size_t size;
            font_load_ds(assets_font(&size), size);
            log_reset();
            log_add_image(IMG_OPENING_MESSAGE);
            sound_play(SND_START_APP);
            screen = SCR_SETUP;
        }
        break;
    }
    case SCR_PAIR:
        if (pair_state() == PAIR_DONE) {
            chat_connect(settings.chat, settings.site);
            screen = SCR_CONNECTING;
        }
        break;
    case SCR_LOBBY:
        if (fade < 255) fade = fade + 40 > 255 ? 255 : fade + 40;
        if (room_shift < room_shift_target) room_shift += 4;
        if (room_shift > room_shift_target) room_shift -= 4;
        if (join_wait > 0 && ++join_wait > 60 * 6) {
            join_wait = 0; /* the server ignores joins into full rooms or with a taken name */
            sound_play(SND_ERROR);
        }
        break;
    case SCR_ROOM:
        if (fade < 255) fade = fade + 51 > 255 ? 255 : fade + 51;
        compose_update();
        break;
    case SCR_CLOSED:
        if (closed_y > 254) closed_y -= 6;
        if (closed_y < 254) closed_y = 254;
        break;
    default: break;
    }
    log_update();
    if (screen != SCR_LOADING && screen != SCR_SETUP && screen != SCR_NAME) handle_events();
}

void app_run(void)
{
    if (settings.color == 0xFFFFFFFF) settings.color = SWATCHES[rand() % 16];
    sanitize_name(settings.name);
    stage_init();
    stage_set_dark(settings.dark);
    sound_volume(settings.volume * 255 / 100);
    input_t in;
    while (!quit && aptMainLoop()) {
        input_poll(&in);
        if (screen == SCR_LOADING) {
            if (assets_progress() < 0 && (in.down & BTN_A)) frame = 0;
            if (in.down & BTN_START) break;
        } else {
            /* The update popup takes the controls while it is up, and on the frame it closes. */
            int was_open = update_open;
            int ust = CTRUPDATE_Popup(in.down, &update_pop);
#ifdef AUTOTEST
            static int last_ust = -1, last_ist = -1;
            if (ust != last_ust || (int)CTRUPDATE_InstallState() != last_ist)
                TLOG("update popup %d install %d %s", last_ust = ust, last_ist = CTRUPDATE_InstallState(),
                     CTRUPDATE_InstallError());
#endif
            if (ust == CTRUPDATE_POPUP_EXIT) break;
            update_open = ust == CTRUPDATE_POPUP_OPEN;
            if (!update_open && !was_open) handle_input(&in);
        }
        update();
        draw();
#ifdef AUTOTEST
        static int last_screen = -1;
        if (screen != last_screen) TLOG("screen %d", screen), last_screen = screen;
        if (autotest_quit) break;
#endif
    }
    pair_cancel();
    chat_disconnect();
}
