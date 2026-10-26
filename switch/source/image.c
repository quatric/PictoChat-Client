#include <stdlib.h>
#include <string.h>
#include <png.h>
#include <turbojpeg.h>
#include <webp/decode.h>
#include "image.h"

/* The Switch's libjpeg-turbo is 2.1, which has the TurboJPEG 2 API only */
static image_t *decode_jpeg(const unsigned char *data, size_t size, int max_side)
{
    tjhandle tj = tjInitDecompress();
    if (!tj) return NULL;
    image_t *img = NULL;
    int w, h, subsamp, cs;
    if (tjDecompressHeader3(tj, data, size, &w, &h, &subsamp, &cs) == 0) {
        tjscalingfactor sf = { 1, 1 };
        while (sf.denom < 8 && (w * sf.num / sf.denom > max_side * 2 || h * sf.num / sf.denom > max_side * 2))
            sf.denom *= 2;
        int sw = TJSCALED(w, sf), sh = TJSCALED(h, sf);
        img = image_new(sw, sh);
        if (img && tjDecompress2(tj, data, size, (unsigned char *)img->px, sw, sw * 4, sh, TJPF_ARGB, 0) < 0 &&
            tjGetErrorCode(tj) == TJERR_FATAL) {
            image_free(img);
            img = NULL;
        }
    }
    tjDestroy(tj);
    return img;
}

static image_t *decode_png(const unsigned char *data, size_t size)
{
    png_image png;
    memset(&png, 0, sizeof(png));
    png.version = PNG_IMAGE_VERSION;
    if (!png_image_begin_read_from_memory(&png, data, size)) return NULL;
    png.format = PNG_FORMAT_ARGB;
    if ((size_t)png.width * png.height > 24u * 1000 * 1000) {
        png_image_free(&png);
        return NULL;
    }
    image_t *img = image_new(png.width, png.height);
    if (!img) {
        png_image_free(&png);
        return NULL;
    }
    if (!png_image_finish_read(&png, NULL, img->px, 0, NULL)) {
        image_free(img);
        return NULL;
    }
    return img;
}

static image_t *decode_webp(const unsigned char *data, size_t size)
{
    int w, h;
    uint8_t *argb = WebPDecodeARGB(data, size, &w, &h);
    if (!argb) return NULL;
    image_t *img = image_new(w, h);
    if (img) memcpy(img->px, argb, (size_t)w * h * 4);
    free(argb); /* libwebp 1.0 has no WebPFree */
    return img;
}


/* ── EXIF orientation (phone and camera photos) ──────────────────────────── */

static int exif_orientation(const unsigned char *d, size_t size)
{
    size_t i = 2;
    while (i + 4 < size && d[i] == 0xFF) {
        int marker = d[i + 1];
        size_t len = d[i + 2] << 8 | d[i + 3];
        if (marker == 0xDA || len < 2) break;
        if (marker == 0xE1 && i + 4 + len <= size && len > 14 && !memcmp(d + i + 4, "Exif\0\0", 6)) {
            const unsigned char *t = d + i + 10;
            size_t tlen = len - 8;
            int le = t[0] == 'I';
#define U16(p) (le ? (p)[0] | (p)[1] << 8 : (p)[0] << 8 | (p)[1])
#define U32(p) (le ? (u32)((p)[0] | (p)[1] << 8 | (p)[2] << 16 | (u32)(p)[3] << 24) : (u32)((u32)(p)[0] << 24 | (p)[1] << 16 | (p)[2] << 8 | (p)[3]))
            u32 ifd = U32(t + 4);
            if (ifd + 2 > tlen) return 1;
            int n = U16(t + ifd);
            for (int e = 0; e < n && ifd + 2 + (e + 1) * 12 <= tlen; e++) {
                const unsigned char *ent = t + ifd + 2 + e * 12;
                if (U16(ent) == 0x0112) return U16(ent + 8);
            }
#undef U16
#undef U32
            return 1;
        }
        i += 2 + len;
    }
    return 1;
}

static image_t *orient(image_t *img, int o)
{
    if (!img || o <= 1 || o > 8) return img;
    int swap = o >= 5;
    image_t *out = image_new(swap ? img->h : img->w, swap ? img->w : img->h);
    if (!out) return img;
    for (int y = 0; y < img->h; y++)
        for (int x = 0; x < img->w; x++) {
            int nx, ny, W = img->w - 1, H = img->h - 1;
            switch (o) {
            case 2: nx = W - x; ny = y; break;
            case 3: nx = W - x; ny = H - y; break;
            case 4: nx = x; ny = H - y; break;
            case 5: nx = y; ny = x; break;
            case 6: nx = H - y; ny = x; break;
            case 7: nx = H - y; ny = W - x; break;
            default: nx = y; ny = W - x; break; /* 8 */
            }
            out->px[ny * out->w + nx] = img->px[y * img->w + x];
        }
    image_free(img);
    return out;
}

/* ── BMP ─────────────────────────────────────────────────────────────────── */

static u32 le32(const unsigned char *p) { return p[0] | p[1] << 8 | p[2] << 16 | (u32)p[3] << 24; }
static int le16(const unsigned char *p) { return p[0] | p[1] << 8; }

static image_t *decode_bmp(const unsigned char *d, size_t size)
{
    if (size < 54) return NULL;
    u32 off = le32(d + 10);
    int w = (int)le32(d + 18), h = (int)le32(d + 22), bpp = le16(d + 28);
    u32 comp = le32(d + 30);
    int top_down = h < 0;
    if (top_down) h = -h;
    if (w <= 0 || h <= 0 || w > 8000 || h > 8000 || (comp != 0 && comp != 3)) return NULL;
    if (bpp != 8 && bpp != 24 && bpp != 32) return NULL;
    size_t stride = ((size_t)w * bpp / 8 + 3) & ~3u;
    if (off + stride * h > size) return NULL;
    const unsigned char *pal = d + 14 + le32(d + 14);
    image_t *img = image_new(w, h);
    if (!img) return NULL;
    for (int y = 0; y < h; y++) {
        const unsigned char *row = d + off + stride * (top_down ? y : h - 1 - y);
        for (int x = 0; x < w; x++) {
            const unsigned char *c;
            u32 a = 0xFF;
            if (bpp == 8) c = pal + row[x] * 4;
            else c = row + x * (bpp / 8);
            if (bpp == 32 && comp == 0) a = 0xFF; /* alpha is unreliable in plain BMPs */
            img->px[y * w + x] = a << 24 | c[2] << 16 | c[1] << 8 | c[0];
        }
    }
    return img;
}

/* ── GIF (first frame) ───────────────────────────────────────────────────── */

static image_t *decode_gif(const unsigned char *d, size_t size)
{
    if (size < 13) return NULL;
    int w = le16(d + 6), h = le16(d + 8);
    if (w <= 0 || h <= 0 || w > 8000 || h > 8000) return NULL;
    size_t p = 13;
    u32 global[256] = { 0 }, local[256];
    if (d[10] & 0x80) {
        int n = 2 << (d[10] & 7);
        if (p + n * 3 > size) return NULL;
        for (int i = 0; i < n; i++) global[i] = 0xFF000000 | d[p + i * 3] << 16 | d[p + i * 3 + 1] << 8 | d[p + i * 3 + 2];
        p += n * 3;
    }
    int transparent = -1;
    while (p < size) {
        if (d[p] == 0x21) { /* extension */
            if (p + 2 < size && d[p + 1] == 0xF9 && p + 6 < size && (d[p + 3] & 1)) transparent = d[p + 6];
            p += 2;
            while (p < size && d[p]) p += d[p] + 1;
            p++;
            continue;
        }
        if (d[p] != 0x2C || p + 10 > size) return NULL;
        int fx = le16(d + p + 1), fy = le16(d + p + 3), fw = le16(d + p + 5), fh = le16(d + p + 7);
        int flags = d[p + 9];
        p += 10;
        u32 *pal = global;
        if (flags & 0x80) {
            int n = 2 << (flags & 7);
            if (p + n * 3 > size) return NULL;
            for (int i = 0; i < n; i++) local[i] = 0xFF000000 | d[p + i * 3] << 16 | d[p + i * 3 + 1] << 8 | d[p + i * 3 + 2];
            pal = local;
            p += n * 3;
        }
        int interlaced = flags & 0x40;
        if (p >= size) return NULL;
        int min_code = d[p++];
        if (min_code < 2 || min_code > 8) return NULL;
        /* gather the data sub-blocks */
        size_t total = 0, q = p;
        while (q < size && d[q]) { total += d[q]; q += d[q] + 1; }
        unsigned char *data = malloc(total + 4);
        if (!data) return NULL;
        size_t n = 0;
        for (q = p; q < size && d[q]; q += d[q] + 1) {
            size_t len = d[q];
            if (q + 1 + len > size) len = size - q - 1;
            memcpy(data + n, d + q + 1, len);
            n += len;
        }
        /* LZW */
        u8 *pixels = calloc((size_t)fw * fh, 1);
        u16 prefix[4096];
        u8 suffix[4096], stack[4097];
        int clear = 1 << min_code, eoi = clear + 1, next = clear + 2, csize = min_code + 1;
        int old = -1, first = 0;
        size_t out = 0, bitpos = 0, count = (size_t)fw * fh;
        for (int i = 0; i < clear; i++) { prefix[i] = 0xFFFF; suffix[i] = i; }
        while (pixels && out < count && bitpos + csize <= n * 8) {
            int code = 0;
            for (int b = 0; b < csize; b++, bitpos++) code |= ((data[bitpos >> 3] >> (bitpos & 7)) & 1) << b;
            if (code == clear) { csize = min_code + 1; next = clear + 2; old = -1; continue; }
            if (code == eoi) break;
            int c = code, sp = 0;
            if (old < 0) {
                if (code >= clear) break;
                pixels[out++] = first = code;
                old = code;
                continue;
            }
            if (code >= next) { stack[sp++] = first; c = old; }
            while (c >= clear && sp < 4096) { stack[sp++] = suffix[c]; c = prefix[c]; }
            stack[sp++] = first = suffix[c];
            while (sp && out < count) pixels[out++] = stack[--sp];
            if (next < 4096) { prefix[next] = old; suffix[next] = first; next++; if (next == (1 << csize) && csize < 12) csize++; }
            old = code;
        }
        free(data);
        if (!pixels) return NULL;
        image_t *img = image_new(w, h);
        if (img) {
            for (int i = 0; i < w * h; i++) img->px[i] = 0xFFFFFFFF;
            for (int r = 0; r < fh; r++) {
                int row = r;
                if (interlaced) { /* rows come in passes of 8, 8, 4, 2 */
                    int p1 = (fh + 7) / 8, p2 = (fh + 3) / 8, p3 = (fh + 1) / 4;
                    if (r < p1) row = r * 8;
                    else if (r < p1 + p2) row = (r - p1) * 8 + 4;
                    else if (r < p1 + p2 + p3) row = (r - p1 - p2) * 4 + 2;
                    else row = (r - p1 - p2 - p3) * 2 + 1;
                }
                int y = fy + row;
                if (y >= h) continue;
                for (int x = 0; x < fw && fx + x < w; x++) {
                    int idx = pixels[r * fw + x];
                    if (idx != transparent) img->px[y * w + fx + x] = pal[idx];
                }
            }
        }
        free(pixels);
        return img;
    }
    return NULL;
}

image_t *image_decode(const void *data, size_t size, int max_side)
{
    const unsigned char *d = data;
    image_t *img = NULL;
    if (size > 3 && d[0] == 0xFF && d[1] == 0xD8) img = orient(decode_jpeg(d, size, max_side), exif_orientation(d, size));
    else if (size > 8 && !memcmp(d, "\x89PNG", 4)) img = decode_png(d, size);
    else if (size > 12 && !memcmp(d, "RIFF", 4) && !memcmp(d + 8, "WEBP", 4)) img = decode_webp(d, size);
    else if (size > 54 && d[0] == 'B' && d[1] == 'M') img = decode_bmp(d, size);
    else if (size > 13 && !memcmp(d, "GIF8", 4)) img = decode_gif(d, size);
    if (img && (img->w > max_side || img->h > max_side)) {
        image_t *fit = image_fit(img, max_side, max_side);
        image_free(img);
        img = fit;
    }
    return img;
}

unsigned char *image_encode_jpeg(const image_t *img, int quality, size_t *out_size)
{
    /* flatten transparency onto white, as the server does */
    u32 *flat = malloc((size_t)img->w * img->h * 4);
    if (!flat) return NULL;
    for (int i = 0; i < img->w * img->h; i++) {
        u32 c = img->px[i];
        flat[i] = 0xFF000000 | gfx_mix(0xFFFFFF, c & 0xFFFFFF, c >> 24);
    }
    tjhandle tj = tjInitCompress();
    unsigned char *jpeg = NULL;
    unsigned long size = 0;
    if (tj) {
        if (tjCompress2(tj, (unsigned char *)flat, img->w, img->w * 4, img->h, TJPF_ARGB, &jpeg, &size, TJSAMP_420,
                        quality, 0) < 0) {
            tjFree(jpeg);
            jpeg = NULL;
        }
        tjDestroy(tj);
    }
    free(flat);
    if (!jpeg) return NULL;
    unsigned char *copy = malloc(size);
    if (copy) memcpy(copy, jpeg, size);
    tjFree(jpeg);
    *out_size = size;
    return copy;
}
