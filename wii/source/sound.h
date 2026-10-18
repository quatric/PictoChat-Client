#pragma once

/* pict.chat's sound effects (see assets.h for the ids). */
void sound_init(void);
void sound_shutdown(void);
void sound_play(int id);
void sound_volume(int volume); /* 0..255 */
