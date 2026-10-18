#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <gccore.h>
#include "assets.h"
#include "image.h"
#include "embedded.h"

static const char *const IMAGE_FILES[IMG_COUNT] = {
    "background", "bottom_screen", "intro_bg", "logo", "drawui", "choose", "choose_mask",
    "box_bg1", "box_bg2", "box_bg3", "box_bg4", "box_bg5",
    "box_lines1", "box_lines2", "box_lines3", "box_lines4", "box_lines5",
    "box_outline1", "box_outline2", "box_outline3", "box_outline4", "box_outline5",
    "box_lines", "opening_message",
    "enter_room_a", "enter_room_b", "enter_room_c", "enter_room_d", "enter_room_e",
    "leave_room_a", "leave_room_b", "leave_room_c", "leave_room_d", "leave_room_e",
    "letter_room_a", "letter_room_b", "letter_room_c", "letter_room_d", "letter_room_e",
    "room_a", "room_b", "room_c", "room_d", "room_e",
    "scroll", "shift", "caps", "accents", "symbols", "japanese", "japanese2", "emojis",
    "connection", "connection_bad", "connection_closed", "copy_link", "body-bg",
};


#define TOTAL (IMG_COUNT + SND_COUNT + 1)

image_t *img[IMG_COUNT];
static short *sounds[SND_COUNT];
static size_t sound_bytes[SND_COUNT];
static const unsigned char *font_data;
static size_t font_size;

static volatile int done_count, failed;
static char error[192];
static lwp_t thread = LWP_THREAD_NULL;

/* ASND wants 32-byte aligned buffers whose length is a multiple of 32 */
static short *aligned_pcm(const unsigned char *data, size_t len, size_t *bytes)
{
    size_t size = (len + 31) & ~31;
    short *pcm = memalign(32, size);
    if (!pcm) return NULL;
    memset(pcm, 0, size);
    memcpy(pcm, data, len);
    DCFlushRange(pcm, size);
    *bytes = size;
    return pcm;
}

/* ── Loader thread ───────────────────────────────────────────────────────── */

static void *loader(void *arg)
{
    (void)arg;
    font_data = nds_ttf;
    font_size = nds_ttf_size;
    done_count++;
    for (int i = 0; i < IMG_COUNT; i++) {
        img[i] = image_decode(IMAGE_DATA[i], *IMAGE_SIZE[i], 4096);
        if (!img[i]) {
            failed = 1;
            if (!error[0]) snprintf(error, sizeof error, "Could not load the picture %s.", IMAGE_FILES[i]);
        }
        done_count++;
    }
    for (int i = 0; i < SND_COUNT; i++) {
        sounds[i] = aligned_pcm(SOUND_DATA[i], *SOUND_SIZE[i], &sound_bytes[i]); /* sounds are optional */
        done_count++;
    }
    return NULL;
}

void assets_start(void)
{
    done_count = 0;
    failed = 0;
    error[0] = 0;
    LWP_CreateThread(&thread, loader, NULL, NULL, 64 * 1024, 50);
}

int assets_progress(void)
{
    if (done_count >= TOTAL) {
        if (thread != LWP_THREAD_NULL) {
            LWP_JoinThread(thread, NULL);
            thread = LWP_THREAD_NULL;
        }
        return failed ? -1 : 100;
    }
    return done_count * 100 / TOTAL;
}

const char *assets_error(void) { return error[0] ? error : "Could not load PictoChat's pictures."; }

const unsigned char *assets_font(size_t *size)
{
    *size = font_size;
    return font_data;
}

const short *assets_sound(int id, size_t *bytes)
{
    *bytes = sound_bytes[id];
    return sounds[id];
}
