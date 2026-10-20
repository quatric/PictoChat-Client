/* PictoChat for 3DS — a 3DS homebrew client for PictoChat Online (https://pict.chat). */
#include <stdlib.h>
#include <3ds.h>
#include <3dsupdate.h>
#include "app.h"
#include "font.h"
#include "gfx.h"
#include "net.h"
#include "settings.h"
#include "sound.h"

int main(int argc, char **argv)
{
    osSetSpeedupEnable(true); /* the New 3DS's faster clock and L2 cache */
    gfx_init();
    settings_load();
    CTRUPDATE_SetAppPath(argc > 0 ? argv[0] : NULL);
    CTRUPDATE_Start("quatric", "PictoChat-Client", APP_VERSION);
    font_init();
    sound_init();
    srand(osGetTime());

    app_run();

    /* let the update check thread finish before the network goes away */
    CTRUPDATE_Cancel();
    CTRUPDATE_Wait(5000);

    sound_shutdown();
    net_stop();
    gfx_shutdown();
    return 0;
}
