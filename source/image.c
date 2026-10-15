#include <stdlib.h>
#include <string.h>
#include <png.h>
#include "image.h"

static image_t *decode_png(const unsigned char *data, size_t size)
{
    png_image png;
    memset(&png, 0, sizeof(png));
    png.version = PNG_IMAGE_VERSION;
    if (!png_image_begin_read_from_memory(&png, data, size)) return NULL;
    png.format = PNG_FORMAT_BGRA; /* B,G,R,A bytes: 0xAARRGGBB words on the little-endian ARM */
    if ((size_t)png.width * png.height > 4u * 1000 * 1000) {
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

image_t *image_decode(const void *data, size_t size, int max_side)
{
    const unsigned char *d = data;
    image_t *img = NULL;
    if (size > 8 && !memcmp(d, "\x89PNG", 4)) img = decode_png(d, size);
    if (img && (img->w > max_side || img->h > max_side)) {
        image_t *fit = image_fit(img, max_side, max_side);
        image_free(img);
        img = fit;
    }
    return img;
}
