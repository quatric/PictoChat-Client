#pragma once
#include <stddef.h>

char *base64_encode(const unsigned char *data, size_t len);
unsigned char *base64_decode(const char *in, size_t *out_len);
/* Read a whole file; returns malloc'd data or NULL */
unsigned char *read_file(const char *path, size_t *len, size_t max);
