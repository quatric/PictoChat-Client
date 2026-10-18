/* PictoChat for Wii U — a Wii U homebrew client for PictoChat Online (https://pict.chat). */
#include <stdlib.h>
#include <SDL2/SDL.h>
#include "platform.h"
#include "app.h"
#include "font.h"
#include "gfx.h"
#include "input.h"
#include "settings.h"
#include "sound.h"

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    /* SDL runs ProcUI, the TV and GamePad pictures, audio and the USB keyboard;
     * controllers are read directly (input.c), so its joystick driver stays off */
    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO);
    gfx_init();
    input_init();
    input_set_canvas(SW, SH);
    settings_load(NULL);
    font_init();
    sound_init();
    srand((unsigned)gettime());

    app_run();

    sound_shutdown();
    input_shutdown();
    gfx_shutdown();
    SDL_Quit(); /* back to the Wii U Menu */
    return 0;
}
