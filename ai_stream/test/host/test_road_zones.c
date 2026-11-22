/**
 * @file test_road_zones.c
 * @brief Host (PC) test of firmware/components/road_zones/road_zones.c, no ESP-IDF needed.
 *
 * Build (from ai_stream/test/host):
 *   gcc -std=c11 -O2 -Wall -Wextra -Werror -I../../../firmware/components/road_zones/include test_road_zones.c ../../../firmware/components/road_zones/road_zones.c -lm -o test_road_zones
 * Run:
 *   ./test_road_zones             unit tests on a synthetic road, exit code 0 = all passed
 *   ./test_road_zones frame.pgm   calibrate one 320x240 binary PGM and print the result as key=value
 *                                 (tools/check_c_port.py compares that with the Python prototype)
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "road_zones.h"

#define W 320U
#define H 240U

/* Synthetic scene: dark background, bright kerb strips, a gray road between them */
#define BACKGROUND    30U
#define KERB          230U
#define ROAD          80U
#define ROAD_FIRST_X  20U
#define ROAD_LAST_X   300U
#define KERB_TOP_Y    30U  /* rows 30..49 = top kerb */
#define ROAD_TOP_Y    50U  /* rows 50..150 = road */
#define ROAD_BOTTOM_Y 150U
#define KERB_BOTTOM_Y 170U /* rows 151..170 = bottom kerb */
#define EDGE_TOL_PX   3.0f

static uint8_t s_frame[W * H];
static road_zones_work_t s_work;
static int s_failures;

#define CHECK(cond)                                                                                                    \
    do {                                                                                                               \
        if (!(cond)) {                                                                                                 \
            printf("FAIL line %d: %s\n", __LINE__, #cond);                                                           \
            s_failures++;                                                                                              \
        }                                                                                                              \
    } while (0)

static void draw_synthetic_road(void)
{
    for (unsigned y = 0; y < H; y++) {
        for (unsigned x = 0; x < W; x++) {
            uint8_t v = BACKGROUND;
            if (x >= ROAD_FIRST_X && x <= ROAD_LAST_X) {
                if (y >= KERB_TOP_Y && y < ROAD_TOP_Y) {
                    v = KERB;
                } else if (y >= ROAD_TOP_Y && y <= ROAD_BOTTOM_Y) {
                    v = ROAD;
                } else if (y > ROAD_BOTTOM_Y && y <= KERB_BOTTOM_Y) {
                    v = KERB;
                }
            }
            s_frame[y * W + x] = v;
        }
    }
}

static void test_synthetic_road(void)
{
    draw_synthetic_road();
    road_zones_t road;
    CHECK(road_zones_calibrate(s_frame, W, H, &s_work, &road));
    CHECK(road.valid);
    const float mid = 160.0f;
    CHECK(fabsf(road.top_a * mid + road.top_b - (float)ROAD_TOP_Y) <= EDGE_TOL_PX);
    CHECK(fabsf(road.bottom_a * mid + road.bottom_b - (float)ROAD_BOTTOM_Y) <= EDGE_TOL_PX);
    CHECK(abs(road.x_min - (int)ROAD_FIRST_X) <= 3);
    CHECK(abs(road.x_max - (int)ROAD_LAST_X) <= 3);
    CHECK(road.has_left);
    /* parallel edges: no vanishing point, so the road is cut into equal image widths */
    CHECK(road.boundaries_x[0] < road.boundaries_x[1] && road.boundaries_x[1] < road.boundaries_x[2]);
    const float quarter = ((float)road.x_max - (float)road.x_min) / 4.0f;
    CHECK(fabsf(road.boundaries_x[1] - ((float)road.x_min + 2.0f * quarter)) <= 4.0f);

    CHECK(road_zones_contains(&road, mid, 100.0f));   /* middle of the road */
    CHECK(!road_zones_contains(&road, mid, 10.0f));   /* far above the top edge */
    CHECK(!road_zones_contains(&road, mid, 200.0f));  /* far below the bottom edge */
    CHECK(!road_zones_contains(&road, 5.0f, 100.0f)); /* left of the road */
    CHECK(road_zones_contains(&road, mid, 45.0f));    /* within the margin above the edge (car body) */

    CHECK(road_zones_zone_of(&road, (float)road.x_min + 1.0f) == 0U);
    CHECK(road_zones_zone_of(&road, (float)road.x_max - 1.0f) == 3U);
    CHECK(road_zones_zone_of(&road, road.boundaries_x[0] + 0.5f) == 1U);
}

static void test_kerbs_of_synthetic_road(void)
{
    draw_synthetic_road();
    road_zones_t road;
    CHECK(road_zones_calibrate(s_frame, W, H, &s_work, &road));
    const float mid = 160.0f;
    /* outer kerb edges: background -> kerb at row 30 (top), kerb -> background after row 170 (bottom) */
    CHECK(road.has_kerb_top && road.has_kerb_bottom);
    CHECK(fabsf(road.kerb_top_a * mid + road.kerb_top_b - (float)KERB_TOP_Y) <= EDGE_TOL_PX);
    CHECK(fabsf(road.kerb_bottom_a * mid + road.kerb_bottom_b - (float)KERB_BOTTOM_Y) <= EDGE_TOL_PX);

    /* the kerb strips now count as "on the road" (people on the pavement light the lamps) ... */
    CHECK(road_zones_contains(&road, mid, 33.0f));
    CHECK(road_zones_contains(&road, mid, 165.0f));
    /* ... the dark background beyond them still does not */
    CHECK(!road_zones_contains(&road, mid, 15.0f));
    CHECK(!road_zones_contains(&road, mid, 190.0f));
    CHECK(!road_zones_contains(&road, 5.0f, 35.0f)); /* left of the road, kerb row */
}

static void test_missing_kerb_side_keeps_the_road_edge(void)
{
    draw_synthetic_road();
    /* bottom kerb runs out of the frame: no outer edge below the road */
    for (unsigned y = KERB_BOTTOM_Y + 1U; y < H; y++) {
        for (unsigned x = ROAD_FIRST_X; x <= ROAD_LAST_X; x++) {
            s_frame[y * W + x] = KERB;
        }
    }
    road_zones_t road;
    CHECK(road_zones_calibrate(s_frame, W, H, &s_work, &road));
    CHECK(road.has_kerb_top);
    CHECK(!road.has_kerb_bottom);
    CHECK(road_zones_contains(&road, 160.0f, 33.0f));   /* top kerb still counts */
    CHECK(!road_zones_contains(&road, 160.0f, 175.0f)); /* below: only the road edge + margin, as before */
}

static void test_empty_frame_is_not_a_road(void)
{
    memset(s_frame, BACKGROUND, sizeof(s_frame));
    road_zones_t road;
    CHECK(!road_zones_calibrate(s_frame, W, H, &s_work, &road));
    CHECK(!road.valid);
    CHECK(!road_zones_contains(&road, 160.0f, 100.0f));
}

static void test_bad_arguments(void)
{
    road_zones_t road;
    CHECK(!road_zones_calibrate(NULL, W, H, &s_work, &road));
    CHECK(!road_zones_calibrate(s_frame, W, H, NULL, &road));
    CHECK(!road_zones_calibrate(s_frame, 640U, 480U, &s_work, &road)); /* larger than the work memory */
    CHECK(!road_zones_calibrate(s_frame, W, H, &s_work, NULL));
}

/** Read a binary PGM (P5, 8-bit) of exactly W x H into s_frame. */
static int read_pgm(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        return -1;
    }
    char magic[3] = { 0 };
    unsigned w = 0, h = 0, maxval = 0;
    const int ok = fscanf(f, "%2s %u %u %u", magic, &w, &h, &maxval) == 4 && strcmp(magic, "P5") == 0 &&
                   w == W && h == H && maxval == 255U;
    if (!ok || fgetc(f) == EOF || fread(s_frame, 1, sizeof(s_frame), f) != sizeof(s_frame)) {
        fclose(f);
        return -1;
    }
    fclose(f);
    return 0;
}

static int run_on_file(const char *path)
{
    if (read_pgm(path) != 0) {
        fprintf(stderr, "cannot read %s (need a %ux%u 8-bit binary PGM)\n", path, W, H);
        return 2;
    }
    road_zones_t road;
    const bool ok = road_zones_calibrate(s_frame, W, H, &s_work, &road);
    printf("valid=%d top_a=%.6f top_b=%.4f bottom_a=%.6f bottom_b=%.4f x_min=%d x_max=%d has_left=%d "
           "left_a=%.6f left_b=%.4f b1=%.3f b2=%.3f b3=%.3f "
           "has_kt=%d kt_a=%.6f kt_b=%.4f has_kb=%d kb_a=%.6f kb_b=%.4f\n",
           ok ? 1 : 0, road.top_a, road.top_b, road.bottom_a, road.bottom_b, road.x_min, road.x_max,
           road.has_left ? 1 : 0, road.left_a, road.left_b, road.boundaries_x[0], road.boundaries_x[1],
           road.boundaries_x[2], road.has_kerb_top ? 1 : 0, road.kerb_top_a, road.kerb_top_b,
           road.has_kerb_bottom ? 1 : 0, road.kerb_bottom_a, road.kerb_bottom_b);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc > 1) {
        return run_on_file(argv[1]);
    }
    test_synthetic_road();
    test_kerbs_of_synthetic_road();
    test_missing_kerb_side_keeps_the_road_edge();
    test_empty_frame_is_not_a_road();
    test_bad_arguments();
    printf(s_failures == 0 ? "all road_zones tests passed\n" : "%d check(s) failed\n", s_failures);
    return s_failures == 0 ? 0 : 1;
}
