#pragma once
#include <stddef.h>
#include "gfx.h"

/* Decode a PNG into an ARGB image, scaled down so that neither side exceeds max_side. */
image_t *image_decode(const void *data, size_t size, int max_side);
