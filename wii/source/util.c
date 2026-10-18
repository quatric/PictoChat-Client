#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "util.h"

static const char B64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

char *base64_encode(const unsigned char *data, size_t len)
{
    size_t out_len = (len + 2) / 3 * 4;
    char *out = malloc(out_len + 1), *p = out;
    if (!out) return NULL;
    size_t i = 0;
    for (; i + 2 < len; i += 3) {
        unsigned v = data[i] << 16 | data[i + 1] << 8 | data[i + 2];
        *p++ = B64[v >> 18];
        *p++ = B64[(v >> 12) & 63];
        *p++ = B64[(v >> 6) & 63];
        *p++ = B64[v & 63];
    }
    if (i < len) {
        unsigned v = data[i] << 16 | (i + 1 < len ? data[i + 1] << 8 : 0);
        *p++ = B64[v >> 18];
        *p++ = B64[(v >> 12) & 63];
        *p++ = i + 1 < len ? B64[(v >> 6) & 63] : '=';
        *p++ = '=';
    }
    *p = 0;
    return out;
}

unsigned char *base64_decode(const char *in, size_t *out_len)
{
    static signed char map[256];
    if (!map['B']) {
        memset(map, -1, sizeof map);
        for (int i = 0; i < 64; i++) map[(unsigned char)B64[i]] = i;
    }
    size_t len = strlen(in);
    unsigned char *out = malloc(len / 4 * 3 + 3), *p = out;
    if (!out) return NULL;
    unsigned v = 0;
    int bits = 0;
    for (size_t i = 0; i < len; i++) {
        int c = map[(unsigned char)in[i]];
        if (c < 0) continue;
        v = v << 6 | c;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            *p++ = (v >> bits) & 0xFF;
        }
    }
    *out_len = p - out;
    return out;
}

unsigned char *read_file(const char *path, size_t *len, size_t max)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size < 0 || (size_t)size > max) {
        fclose(f);
        return NULL;
    }
    unsigned char *data = malloc(size + 1);
    if (data && fread(data, 1, size, f) != (size_t)size) {
        free(data);
        data = NULL;
    }
    fclose(f);
    if (data) {
        data[size] = 0;
        *len = size;
    }
    return data;
}
