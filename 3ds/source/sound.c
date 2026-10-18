#include <string.h>
#include <3ds.h>
#include "sound.h"
#include "assets.h"

/* One DSP channel per sound effect, so a sound that is still playing restarts
 * instead of stacking copies. Without the DSP firmware (sdmc:/3ds/dspfirm.cdc,
 * dumped by DSP1) the app simply stays silent. */
static int volume = 200;
static int ready;
static ndspWaveBuf waves[SND_COUNT];

void sound_init(void)
{
    if (R_FAILED(ndspInit())) return;
    ndspSetOutputMode(NDSP_OUTPUT_STEREO);
    for (int i = 0; i < SND_COUNT; i++) {
        ndspChnReset(i);
        ndspChnSetInterp(i, NDSP_INTERP_LINEAR);
        ndspChnSetRate(i, 32000);
        ndspChnSetFormat(i, NDSP_FORMAT_STEREO_PCM16);
    }
    ready = 1;
}

void sound_volume(int v) { volume = v < 0 ? 0 : v > 255 ? 255 : v; }

void sound_play(int id)
{
    if (!ready || !volume || id < 0 || id >= SND_COUNT) return;
    size_t bytes;
    const short *pcm = assets_sound(id, &bytes);
    if (!pcm || !bytes) return;
    ndspChnWaveBufClear(id);
    float mix[12] = { 0 };
    mix[0] = mix[1] = volume / 255.0f;
    ndspChnSetMix(id, mix);
    memset(&waves[id], 0, sizeof waves[id]);
    waves[id].data_vaddr = pcm;
    waves[id].nsamples = bytes / 4; /* stereo frames */
    ndspChnWaveBufAdd(id, &waves[id]);
}

void sound_shutdown(void)
{
    if (!ready) return;
    for (int i = 0; i < SND_COUNT; i++) ndspChnWaveBufClear(i);
    ndspExit();
    ready = 0;
}
