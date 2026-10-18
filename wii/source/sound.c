#include <gccore.h>
#include <asndlib.h>
#include "sound.h"
#include "assets.h"

static int volume = 200;
static int ready;
static int last_voice[SND_COUNT];

void sound_init(void)
{
    ASND_Init();
    ASND_Pause(0);
    for (int i = 0; i < SND_COUNT; i++) last_voice[i] = -1;
    ready = 1;
}

void sound_volume(int v) { volume = v < 0 ? 0 : v > 255 ? 255 : v; }

void sound_play(int id)
{
    if (!ready || !volume || id < 0 || id >= SND_COUNT) return;
    size_t bytes;
    const short *pcm = assets_sound(id, &bytes);
    if (!pcm || !bytes) return;
    /* restart a sound that is still playing instead of stacking copies */
    if (last_voice[id] >= 0 && ASND_StatusVoice(last_voice[id]) == SND_WORKING) ASND_StopVoice(last_voice[id]);
    int voice = ASND_GetFirstUnusedVoice();
    if (voice < 0) return;
    if (ASND_SetVoice(voice, VOICE_STEREO_16BIT, 32000, 0, (void *)pcm, bytes, volume, volume, NULL) == SND_OK)
        last_voice[id] = voice;
}

void sound_shutdown(void)
{
    if (!ready) return;
    ASND_Pause(1);
    ASND_End();
    ready = 0;
}
