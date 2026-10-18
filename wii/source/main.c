/* PictoChat for Wii — a Homebrew Channel client for PictoChat Online (https://pict.chat). */
#include <stdlib.h>
#include <gccore.h>
#include <fat.h>
#include <ogc/lwp_watchdog.h>
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
    font_init();
    sound_init();
    srand(gettime());

    app_run();

    sound_shutdown();
    input_shutdown();
    gfx_shutdown();
    return 0;
}
