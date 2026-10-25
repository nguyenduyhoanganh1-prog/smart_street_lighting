/**
 * @file gray_image.c
 * @brief Pure helpers for 8-bit grayscale images (see gray_image.h).
 */
#include "gray_image.h"

#include <stdio.h>

int gray_pgm_header(char *buf, size_t cap, uint32_t width, uint32_t height)
{
    if (buf == NULL || width == 0U || height == 0U) {
        return -1;
    }
    const int n = snprintf(buf, cap, "P5\n%u %u\n255\n", (unsigned)width, (unsigned)height);
    return (n < 0 || (size_t)n >= cap) ? -1 : n;
}

bool gray_box_downscale(const uint8_t *src, uint32_t src_w, uint32_t src_h, uint32_t factor,
                        uint8_t *dst, size_t dst_cap)
{
    if (src == NULL || dst == NULL || factor == 0U || src_w == 0U || src_h == 0U
        || src_w % factor != 0U || src_h % factor != 0U) {
        return false;
    }
    const uint32_t dst_w = src_w / factor;
    const uint32_t dst_h = src_h / factor;
    if ((size_t)dst_w * dst_h > dst_cap) {
        return false;
    }
    const uint32_t area = factor * factor;
    for (uint32_t y = 0; y < dst_h; y++) {
        for (uint32_t x = 0; x < dst_w; x++) {
            const uint8_t *block = src + (size_t)y * factor * src_w + (size_t)x * factor;
            uint32_t sum = 0;
            for (uint32_t by = 0; by < factor; by++) {
                const uint8_t *row = block + (size_t)by * src_w;
                for (uint32_t bx = 0; bx < factor; bx++) {
                    sum += row[bx];
                }
            }
            dst[(size_t)y * dst_w + x] = (uint8_t)((sum + area / 2U) / area); /* round to nearest */
        }
    }
    return true;
}
