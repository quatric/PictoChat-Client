#include <string.h>
#include <SDL2/SDL.h>
#include "platform.h"
#include "sound.h"
#include "assets.h"

/* pict.chat's sounds are 32 kHz 16-bit stereo, big-endian like the Wii U;
 * they are mixed here and SDL resamples them for the TV and GamePad. */
#define VOICES 8

typedef struct {
    const s16 *pcm;
    size_t frames, pos;
    int id;
} voice_t;

static voice_t voices[VOICES];
static int volume = 200;
static SDL_AudioDeviceID dev;

static void mix(void *userdata, Uint8 *stream, int len)
{
    (void)userdata;
    s16 *out = (s16 *)stream;
    int frames = len / 4;
    memset(stream, 0, len);
    for (int v = 0; v < VOICES; v++) {
        voice_t *vc = &voices[v];
        if (!vc->pcm) continue;
        for (int i = 0; i < frames && vc->pos < vc->frames; i++, vc->pos++) {
            for (int ch = 0; ch < 2; ch++) {
                int s = out[i * 2 + ch] + vc->pcm[vc->pos * 2 + ch] * volume / 255;
                out[i * 2 + ch] = s > 32767 ? 32767 : s < -32768 ? -32768 : s;
            }
        }
        if (vc->pos >= vc->frames) vc->pcm = NULL;
    }
}

void sound_init(void)
{
    SDL_AudioSpec want, have;
    SDL_zero(want);
    want.freq = 32000;
    want.format = AUDIO_S16MSB;
    want.channels = 2;
    want.samples = 1024;
    want.callback = mix;
    dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (dev) SDL_PauseAudioDevice(dev, 0);
}

void sound_volume(int v) { volume = v < 0 ? 0 : v > 255 ? 255 : v; }

void sound_play(int id)
{
    if (!dev || !volume || id < 0 || id >= SND_COUNT) return;
    size_t bytes;
    const short *pcm = assets_sound(id, &bytes);
    if (!pcm || !bytes) return;
    SDL_LockAudioDevice(dev);
    /* restart a sound that is still playing instead of stacking copies */
    int slot = -1;
    for (int v = 0; v < VOICES; v++)
        if (voices[v].pcm && voices[v].id == id) slot = v;
    for (int v = 0; v < VOICES && slot < 0; v++)
        if (!voices[v].pcm) slot = v;
    if (slot >= 0) {
        voices[slot].pcm = pcm;
        voices[slot].frames = bytes / 4;
        voices[slot].pos = 0;
        voices[slot].id = id;
    }
    SDL_UnlockAudioDevice(dev);
}

void sound_shutdown(void)
{
    if (!dev) return;
    SDL_CloseAudioDevice(dev);
    dev = 0;
}
