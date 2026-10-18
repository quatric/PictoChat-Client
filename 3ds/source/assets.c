#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sys.h"
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
static sys_thread thread;

/* The DSP plays from linear memory, in native (little-endian) byte order;
 * the files are big-endian. */
static short *linear_pcm(const unsigned char *data, size_t len, size_t *bytes)
{
    len &= ~(size_t)3; /* whole stereo frames */
    short *pcm = linearAlloc(len);
    if (!pcm) return NULL;
    for (size_t i = 0; i < len / 2; i++) pcm[i] = (short)(data[i * 2] << 8 | data[i * 2 + 1]);
    DSP_FlushDataCache(pcm, len);
    *bytes = len;
    return pcm;
}

/* ── Loader thread ───────────────────────────────────────────────────────── */

static void loader(void *arg)
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
        sounds[i] = linear_pcm(SOUND_DATA[i], *SOUND_SIZE[i], &sound_bytes[i]); /* sounds are optional */
        done_count++;
    }
}

void assets_start(void)
{
    done_count = 0;
    failed = 0;
    error[0] = 0;
    thread = sys_thread_start(loader, NULL, 64 * 1024);
    if (!thread) loader(NULL);
}

int assets_progress(void)
{
    if (done_count >= TOTAL) {
        sys_thread_join(&thread);
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
