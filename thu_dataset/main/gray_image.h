/**
 * @file gray_image.h
 * @brief Pure helpers for 8-bit grayscale images (no ESP-IDF calls, testable on a PC).
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Room for any header made by gray_pgm_header(): "P5\n" + two 10-digit numbers + "255\n". */
#define GRAY_PGM_HEADER_CAP 32U

/**
 * Write the header of a binary PGM (P5) image: "P5\n<width> <height>\n255\n".
 * The pixel bytes (width * height, row after row) follow it directly in the file.
 * @return header length (15 for 640x360), or -1 on a zero size or when buf is too small
 */
int gray_pgm_header(char *buf, size_t cap, uint32_t width, uint32_t height);

/**
 * Shrink an 8-bit image by averaging every factor x factor block into one pixel (rounded).
 * @param src_w, src_h  source size, both must be multiples of factor
 * @param dst           receives (src_w / factor) * (src_h / factor) bytes
 * @param dst_cap       size of dst in bytes
 * @return false on a NULL pointer, factor 0, sizes that are not multiples of factor, or a small dst
 */
bool gray_box_downscale(const uint8_t *src, uint32_t src_w, uint32_t src_h, uint32_t factor,
                        uint8_t *dst, size_t dst_cap);

#ifdef __cplusplus
}
#endif
