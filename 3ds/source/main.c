/* PictoChat for 3DS — a 3DS homebrew client for PictoChat Online (https://pict.chat). */
#include <stdlib.h>
#include <3ds.h>
#include "app.h"
#include "font.h"
#include "gfx.h"
#include "net.h"
#include "settings.h"
#include "sound.h"

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    osSetSpeedupEnable(true); /* the New 3DS's faster clock and L2 cache */
    gfx_init();
    settings_load();
    font_init();
    sound_init();
    srand(osGetTime());

    app_run();

    sound_shutdown();
    net_stop();
    gfx_shutdown();
    return 0;
}
