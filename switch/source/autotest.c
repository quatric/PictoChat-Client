/* Test builds only (make AUTOTEST=1): replays sd:/autotest.txt as controller
 * and keyboard input and saves screenshots to sd:/shots/, so the whole app
 * can be exercised in Cemu without a person at the controls. */
#ifdef AUTOTEST
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "platform.h"
#include "input.h"
#include "gfx.h"

static FILE *script;
static int wait_frames;
static int pointing, px, py;
static u32 held_mask;
int autotest_quit;

typedef struct { const char *name; u32 bit; } btn_t;
static const btn_t BTNS[] = {
    { "A", BTN_A }, { "B", BTN_B }, { "UP", BTN_UP }, { "DOWN", BTN_DOWN }, { "LEFT", BTN_LEFT },
    { "RIGHT", BTN_RIGHT }, { "PLUS", BTN_PLUS }, { "MINUS", BTN_MINUS }, { "1", BTN_1 }, { "2", BTN_2 },
    { "HOME", BTN_HOME },
};

void autotest_push_key(u8 code, char ch);

static void save_shot(const char *name, const u32 *px, int SW, int SH)
{
    mkdir(SD_ROOT "/shots", 0777);
    char path[128];
    snprintf(path, sizeof path, SD_ROOT "/shots/%s.bmp", name);
    FILE *f = fopen(path, "wb");
    if (!f) return;
    int row = (SW * 3 + 3) & ~3, size = 54 + row * SH;
    unsigned char h[54] = { 'B', 'M' };
#define LE32(o, v) do { h[o] = (v) & 255; h[o + 1] = ((v) >> 8) & 255; h[o + 2] = ((v) >> 16) & 255; h[o + 3] = ((v) >> 24) & 255; } while (0)
    LE32(2, size); LE32(10, 54); LE32(14, 40); LE32(18, SW); LE32(22, SH);
    h[26] = 1; h[28] = 24; LE32(34, row * SH);
    fwrite(h, 1, 54, f);
    unsigned char *line = calloc(row, 1);
    for (int y = SH - 1; y >= 0; y--) {
        for (int x = 0; x < SW; x++) {
            u32 c = px[y * SW + x];
            line[x * 3] = c & 255;
            line[x * 3 + 1] = (c >> 8) & 255;
            line[x * 3 + 2] = (c >> 16) & 255;
        }
        fwrite(line, 1, row, f);
    }
    free(line);
    fclose(f);
}

static int pending_shot;
static char shot_name[64];

int autotest_shot_pending(void) { return pending_shot; }

/* called with the frame as it went to the TV */
void autotest_save(const u32 *px, int w, int h)
{
    if (pending_shot) {
        save_shot(shot_name, px, w, h);
        pending_shot = 0;
    }
}

void autotest_apply(input_t *in)
{
    if (!script) {
        static int tried;
        if (tried) goto pointer;
        tried = 1;
        script = fopen(SD_ROOT "/autotest.txt", "r");
        if (!script) return;
    }
    if (wait_frames > 0) {
        wait_frames--;
        goto pointer;
    }
    char line[512];
    while (fgets(line, sizeof line, script)) {
        line[strcspn(line, "\r\n")] = 0;
        if (!line[0] || line[0] == '#') continue;
        if (!strncmp(line, "wait ", 5)) {
            wait_frames = atoi(line + 5);
            break;
        } else if (!strncmp(line, "press ", 6)) {
            for (unsigned i = 0; i < sizeof BTNS / sizeof *BTNS; i++)
                if (!strcmp(line + 6, BTNS[i].name)) in->down |= BTNS[i].bit;
            wait_frames = 6;
            break;
        } else if (!strncmp(line, "hold ", 5) || !strncmp(line, "release ", 8)) {
            int hold = line[0] == 'h';
            for (unsigned i = 0; i < sizeof BTNS / sizeof *BTNS; i++)
                if (!strcmp(line + (hold ? 5 : 8), BTNS[i].name)) {
                    if (hold) { held_mask |= BTNS[i].bit; in->down |= BTNS[i].bit; }
                    else held_mask &= ~BTNS[i].bit;
                }
            wait_frames = 2;
            break;
        } else if (!strncmp(line, "type ", 5)) {
            for (const char *c = line + 5; *c; c++) autotest_push_key(0x04, *c == '~' ? '\n' : *c);
        } else if (!strncmp(line, "key ", 4)) {
            autotest_push_key((u8)strtol(line + 4, NULL, 16), 0);
        } else if (!strncmp(line, "point ", 6)) {
            pointing = 1;
            sscanf(line + 6, "%d %d", &px, &py);
            in->moved = 1;
            in->pointer = 1;
            in->px = px;
            in->py = py;
            in->held |= held_mask;
            wait_frames = 2;
            return;
        } else if (!strcmp(line, "nopoint")) {
            pointing = 0;
        } else if (!strncmp(line, "shot ", 5)) {
            snprintf(shot_name, sizeof shot_name, "%s", line + 5);
            pending_shot = 1;
            wait_frames = 2;
            break;
        } else if (!strcmp(line, "quit")) {
            autotest_quit = 1;
            break;
        }
    }
pointer:
    in->held |= held_mask;
    /* the script owns the Wii Remote pointer; GamePad touches (from the
     * emulator's GamePad window) still come through */
    if (pointing) {
        in->pointer = 1;
        in->touch = 0;
        in->px = px;
        in->py = py;
    } else if (!in->touch) {
        in->pointer = 0;
        in->moved = 0;
    }
}
#endif

#ifdef AUTOTEST
#include <stdarg.h>
void autotest_log(const char *fmt, ...)
{
    FILE *f = fopen(SD_ROOT "/pictochat-log.txt", "a");
    if (!f) {
        printf("(no sd) ");
        f = fopen("/dev/null", "w");
        if (!f) return;
    }
    va_list ap;
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    printf("\n");
    fputc('\n', f);
    fclose(f);
}
#endif
