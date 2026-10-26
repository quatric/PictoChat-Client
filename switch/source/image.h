#pragma once
#include <stddef.h>
#include "gfx.h"

/* Decode a JPEG, PNG or WebP into an ARGB image. Large photos are decoded at
 * a reduced scale so that neither side exceeds roughly max_side pixels. */
image_t *image_decode(const void *data, size_t size, int max_side);
/* Encode as a baseline JPEG; returns a malloc'd buffer */
unsigned char *image_encode_jpeg(const image_t *img, int quality, size_t *out_size);
