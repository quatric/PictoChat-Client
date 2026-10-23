/* PictoChat for Wii — a Homebrew Channel client for PictoChat Online (https://pict.chat). */
#include <stdlib.h>
#include <gccore.h>
#include <fat.h>
#include <ogc/lwp_watchdog.h>
#include <wiiupdate.h>
#include "app.h"
#include "font.h"
#include "gfx.h"
#include "input.h"
#include "settings.h"
#include "sound.h"

int main(int argc, char **argv)
{
#ifdef AUTOTEST
    SYS_STDIO_Report(true);
#endif
    gfx_init();
    input_init();
    input_set_canvas(SW, SH);
    fatInitDefault();
    settings_load(argc > 0 ? argv[0] : NULL);
    WIIUPDATE_SetAppPath(argc > 0 ? argv[0] : NULL);
    WIIUPDATE_Start("quatric", "PictoChat-Client", APP_VERSION);
    font_init();
    sound_init();
    srand(gettime());

    app_run();

    /* let the update check thread finish before the network and video go away */
    WIIUPDATE_Cancel();
    WIIUPDATE_Wait(5000);

    int tablet = input_tablet_opened();
    sound_shutdown();
    input_shutdown();
    gfx_shutdown();
    /* A tablet read stays queued in IOS while the pen is away, and would hold
     * up the Homebrew Channel's stub as it closes everything: start afresh */
    if (tablet) IOS_ReloadIOS(IOS_GetVersion());
    return 0;
}
