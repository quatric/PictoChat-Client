#pragma once
#include <stddef.h>
#include "gfx.h"

/* PictoChat Online's own pictures, font and sounds (from
 * github.com/ayunami2000/ayunpictojava), built into the app. */

enum {
    IMG_BACKGROUND, IMG_BOTTOM_SCREEN, IMG_INTRO_BG, IMG_LOGO, IMG_DRAWUI, IMG_CHOOSE, IMG_CHOOSE_MASK,
    IMG_BOX_BG1, IMG_BOX_BG2, IMG_BOX_BG3, IMG_BOX_BG4, IMG_BOX_BG5,
    IMG_BOX_LINES1, IMG_BOX_LINES2, IMG_BOX_LINES3, IMG_BOX_LINES4, IMG_BOX_LINES5,
    IMG_BOX_OUTLINE1, IMG_BOX_OUTLINE2, IMG_BOX_OUTLINE3, IMG_BOX_OUTLINE4, IMG_BOX_OUTLINE5,
    IMG_BOX_LINES, IMG_OPENING_MESSAGE,
    IMG_ENTER_A, IMG_ENTER_B, IMG_ENTER_C, IMG_ENTER_D, IMG_ENTER_E,
    IMG_LEAVE_A, IMG_LEAVE_B, IMG_LEAVE_C, IMG_LEAVE_D, IMG_LEAVE_E,
    IMG_LETTER_A, IMG_LETTER_B, IMG_LETTER_C, IMG_LETTER_D, IMG_LETTER_E,
    IMG_ROOM_A, IMG_ROOM_B, IMG_ROOM_C, IMG_ROOM_D, IMG_ROOM_E,
    IMG_SCROLL, IMG_SHIFT, IMG_CAPS, IMG_ACCENTS, IMG_SYMBOLS, IMG_JAPANESE, IMG_JAPANESE2, IMG_EMOJIS,
    IMG_CONNECTION, IMG_CONNECTION_BAD, IMG_CONNECTION_CLOSED, IMG_COPY_LINK, IMG_BODY_BG,
    IMG_COUNT
};

enum {
    SND_START_APP, SND_JOIN_ROOM, SND_LEAVE_ROOM, SND_KEY_DOWN, SND_KEY_UP, SND_DROP_LETTER, SND_SEND,
    SND_BIG_PEN, SND_SMALL_PEN, SND_CLEAR, SND_DRAW, SND_DRAW_DOWN, SND_ERASER, SND_ERROR, SND_PEN,
    SND_PLAYER_JOIN, SND_PLAYER_LEAVE, SND_RECEIVE, SND_RETRIEVE, SND_SCROLL,
    SND_COUNT
};

extern image_t *img[IMG_COUNT];

/* Decode everything on a background thread; poll assets_progress() until it returns 100 or -1. */
void assets_start(void);
int assets_progress(void);           /* 0..100, or -1 when something could not be loaded */
const char *assets_error(void);
/* The NintendoDSBIOS font file, once loaded (NULL before that or when it failed) */
const unsigned char *assets_font(size_t *size);
/* Sounds: 16-bit stereo PCM at 32 kHz in linear memory */
const short *assets_sound(int id, size_t *bytes);
