/**
 * @file test_gray_image.c
 * @brief Off-target test of the grayscale helpers. Build and run on a PC:
 *
 *   gcc -Wall -Wextra -Werror -I../main test_gray_image.c ../main/gray_image.c -o test_gray
 *   ./test_gray
 */
#include <stdio.h>
#include <string.h>

#include "gray_image.h"

static int s_failures;

#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            printf("FAIL line %d: %s\n", __LINE__, #cond);                       \
            s_failures++;                                                        \
        }                                                                        \
    } while (0)

static void test_pgm_header(void)
{
    char buf[GRAY_PGM_HEADER_CAP];
    CHECK(gray_pgm_header(buf, sizeof(buf), 640U, 360U) == 15);
    CHECK(strcmp(buf, "P5\n640 360\n255\n") == 0);
    CHECK(gray_pgm_header(buf, 8U, 640U, 360U) == -1); /* buffer too small */
    CHECK(gray_pgm_header(buf, sizeof(buf), 0U, 360U) == -1);
    CHECK(gray_pgm_header(buf, sizeof(buf), 640U, 0U) == -1);
    CHECK(gray_pgm_header(NULL, sizeof(buf), 640U, 360U) == -1);
}

static void test_downscale_values(void)
{
    /* 4x4 -> 2x2 with factor 2 */
    const uint8_t src[16] = {
        10, 10, 20, 20,
        10, 10, 20, 20,
        30, 30, 40, 41,
        30, 30, 40, 41,
    };
    uint8_t dst[4] = { 0 };
    CHECK(gray_box_downscale(src, 4U, 4U, 2U, dst, sizeof(dst)));
    CHECK(dst[0] == 10 && dst[1] == 20 && dst[2] == 30);
    CHECK(dst[3] == 41); /* (40+41+40+41) / 4 = 40.5 rounds up to 41 */

    const uint8_t spike[4] = { 0, 0, 0, 255 };
    uint8_t one = 0;
    CHECK(gray_box_downscale(spike, 2U, 2U, 2U, &one, 1U));
    CHECK(one == 64); /* 63.75 rounds to 64 */

    const uint8_t white[4] = { 255, 255, 255, 255 };
    CHECK(gray_box_downscale(white, 2U, 2U, 2U, &one, 1U));
    CHECK(one == 255); /* full white must stay 255 */
}

static void test_downscale_frame_size(void)
{
    static uint8_t frame[640U * 360U];
    static uint8_t small[160U * 90U];
    memset(frame, 100, sizeof(frame));
    CHECK(gray_box_downscale(frame, 640U, 360U, 4U, small, sizeof(small)));
    CHECK(small[0] == 100 && small[sizeof(small) - 1U] == 100);
}

static void test_downscale_rejects_bad_input(void)
{
    uint8_t src[16] = { 0 };
    uint8_t dst[16] = { 0 };
    CHECK(!gray_box_downscale(src, 4U, 4U, 3U, dst, sizeof(dst))); /* 4 is not a multiple of 3 */
    CHECK(!gray_box_downscale(src, 4U, 4U, 0U, dst, sizeof(dst)));
    CHECK(!gray_box_downscale(src, 4U, 4U, 2U, dst, 3U));          /* dst needs 4 bytes */
    CHECK(!gray_box_downscale(NULL, 4U, 4U, 2U, dst, sizeof(dst)));
    CHECK(!gray_box_downscale(src, 4U, 4U, 2U, NULL, sizeof(dst)));
}

int main(void)
{
    test_pgm_header();
    test_downscale_values();
    test_downscale_frame_size();
    test_downscale_rejects_bad_input();
    if (s_failures == 0) {
        printf("all gray_image tests passed\n");
        return 0;
    }
    printf("%d check(s) failed\n", s_failures);
    return 1;
}
