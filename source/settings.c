#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "settings.h"
#include "version.h"

settings_t settings;
char app_dir[128] = "sdmc:/3ds/PictoChat";

static void trim_slash(char *s)
{
    size_t n = strlen(s);
    while (n && s[n - 1] == '/') s[--n] = 0;
}

void settings_load(void)
{
    snprintf(settings.chat, sizeof settings.chat, "%s", DEFAULT_CHAT);
    snprintf(settings.site, sizeof settings.site, "%s", DEFAULT_SITE);
    snprintf(settings.pair, sizeof settings.pair, "%s", DEFAULT_PAIR);
    settings.captcha = 1;
    settings.volume = 80;
    settings.color = 0xFFFFFFFF; /* none yet: pick one at random */
    char path[192];
    snprintf(path, sizeof path, "%s/settings.txt", app_dir);
    FILE *f = fopen(path, "r");
    if (!f) return;
    char line[256];
    while (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = 0;
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        const char *v = eq + 1;
        if (!strcmp(line, "name")) snprintf(settings.name, sizeof settings.name, "%s", v);
        else if (!strcmp(line, "color") && *v) settings.color = strtoul(v[0] == '#' ? v + 1 : v, NULL, 16) & 0xFFFFFF;
        else if (!strcmp(line, "chat") && *v) snprintf(settings.chat, sizeof settings.chat, "%s", v);
        else if (!strcmp(line, "site") && *v) snprintf(settings.site, sizeof settings.site, "%s", v);
        else if (!strcmp(line, "pair") && *v) snprintf(settings.pair, sizeof settings.pair, "%s", v);
        else if (!strcmp(line, "captcha")) settings.captcha = atoi(v) != 0;
        else if (!strcmp(line, "volume")) settings.volume = atoi(v);
        else if (!strcmp(line, "dark")) settings.dark = atoi(v) != 0;
        else if (!strcmp(line, "pixel")) settings.pixel = atoi(v) != 0;
    }
    fclose(f);
    trim_slash(settings.site);
    trim_slash(settings.pair);
    if (settings.volume < 0) settings.volume = 0;
    if (settings.volume > 100) settings.volume = 100;
}

void settings_save(void)
{
    mkdir("sdmc:/3ds", 0777);
    mkdir(app_dir, 0777);
    char path[192];
    snprintf(path, sizeof path, "%s/settings.txt", app_dir);
    FILE *f = fopen(path, "w");
    if (!f) return;
    fprintf(f, "name=%s\ncolor=#%06x\nvolume=%d\n", settings.name, (unsigned)settings.color & 0xFFFFFF, settings.volume);
    if (strcmp(settings.chat, DEFAULT_CHAT)) fprintf(f, "chat=%s\n", settings.chat);
    if (strcmp(settings.site, DEFAULT_SITE)) fprintf(f, "site=%s\n", settings.site);
    if (strcmp(settings.pair, DEFAULT_PAIR)) fprintf(f, "pair=%s\n", settings.pair);
    if (!settings.captcha) fprintf(f, "captcha=0\n");
    if (settings.dark) fprintf(f, "dark=1\n");
    if (settings.pixel) fprintf(f, "pixel=1\n");
    fclose(f);
}
