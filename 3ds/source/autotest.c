/* Test builds only (make AUTOTEST=1): replays sdmc:/autotest.txt as button
 * presses, stylus touches and typed text, so the whole app can be exercised
 * in an emulator without a person at the controls. Commands, one per line:
 *   wait N           do nothing for N frames
 *   press BUTTON     A B X Y L R START SELECT UP DOWN LEFT RIGHT
 *   hold / release BUTTON
 *   touch X Y        the stylus at touch screen pixel X Y (320x240), held
 *   lift             the stylus leaves the screen
 *   type TEXT        as if typed on the system keyboard (~ is a new line)
 *   quit */
#ifdef AUTOTEST
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "input.h"

static FILE *script;
static int wait_frames;
static int touching, tx, ty;
static u32 held_mask;
static char typed[256];
int autotest_quit;

typedef struct { const char *name; u32 bit; } btn_t;
static const btn_t BTNS[] = {
    { "A", BTN_A }, { "B", BTN_B }, { "X", BTN_X }, { "Y", BTN_Y }, { "L", BTN_L }, { "R", BTN_R },
    { "START", BTN_START }, { "SELECT", BTN_SELECT }, { "UP", BTN_UP }, { "DOWN", BTN_DOWN },
    { "LEFT", BTN_LEFT }, { "RIGHT", BTN_RIGHT },
};

static u32 button(const char *name)
{
    for (unsigned i = 0; i < sizeof BTNS / sizeof *BTNS; i++)
        if (!strcmp(name, BTNS[i].name)) return BTNS[i].bit;
    return 0;
}

/* Text queued by "type", handed out instead of opening the system keyboard */
int autotest_text(char *out, size_t size)
{
    if (!typed[0]) return 0;
    snprintf(out, size, "%s", typed);
    typed[0] = 0;
    return 1;
}

void autotest_apply(input_t *in)
{
    if (!script) {
        static int tried;
        if (tried) goto apply;
        tried = 1;
        script = fopen("sdmc:/autotest.txt", "r");
        if (!script) return;
    }
    if (wait_frames > 0) {
        wait_frames--;
        goto apply;
    }
    char line[512];
    while (fgets(line, sizeof line, script)) {
        line[strcspn(line, "\r\n")] = 0;
        if (!line[0] || line[0] == '#') continue;
        if (!strncmp(line, "wait ", 5)) {
            wait_frames = atoi(line + 5);
            break;
        } else if (!strncmp(line, "press ", 6)) {
            in->down |= button(line + 6);
            wait_frames = 6;
            break;
        } else if (!strncmp(line, "hold ", 5)) {
            held_mask |= button(line + 5);
            in->down |= button(line + 5);
            wait_frames = 2;
            break;
        } else if (!strncmp(line, "release ", 8)) {
            held_mask &= ~button(line + 8);
            wait_frames = 2;
            break;
        } else if (!strncmp(line, "touch ", 6)) {
            touching = 1;
            sscanf(line + 6, "%d %d", &tx, &ty);
            wait_frames = 1;
            break;
        } else if (!strcmp(line, "lift")) {
            touching = 0;
            wait_frames = 2;
            break;
        } else if (!strncmp(line, "type ", 5)) {
            snprintf(typed, sizeof typed, "%s", line + 5);
            for (char *c = typed; *c; c++)
                if (*c == '~') *c = '\n';
            in->down |= BTN_Y;
            wait_frames = 6;
            break;
        } else if (!strcmp(line, "quit")) {
            autotest_quit = 1;
            break;
        }
    }
apply:
    in->held |= held_mask;
    /* the script owns the stylus */
    in->touch = touching;
    if (touching) {
        in->tx = tx;
        in->ty = ty;
    }
}

void autotest_log(const char *fmt, ...)
{
    FILE *f = fopen("sdmc:/pictochat-log.txt", "a");
    if (!f) return;
    va_list ap;
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fputc('\n', f);
    fclose(f);
}
#endif
