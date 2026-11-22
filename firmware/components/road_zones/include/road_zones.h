/**
 * @file road_zones.h
 * @brief Start-up road calibration, method D: find the road in a frame lit by every lamp and cut it
 *        into ROAD_ZONES_COUNT stretches of equal length on the ground.
 *
 * C port of ai_stream/tools/zone_calib.py (find_road, find_left_end, find_kerbs, equal_zones). Keep both in
 * step: tools/check_c_port.py runs the two on the same frames and compares the results.
 *
 * Pure C (no ESP-IDF), so the same file is unit-tested on the PC (test/host).
 *
 * How it works, in short:
 *   1. Each scanned column: brightness edges (bright -> dark = road top, dark -> bright = road
 *      bottom). A fixed threshold cannot work: the road is dark far from the lamps but almost as
 *      bright as the kerbs right under one.
 *   2. Straight lines through those edges by RANSAC (every point pair), so ~half wrong columns
 *      (clutter, road ends) are voted out; then "predict and snap" grows the road to its ends.
 *   3. Left end: rows scanned for the step from the dark background to the road (slanted by the
 *      camera angle / lens).
 *   4. Kerbs: in each road column, the nearest edge 6..70 rows beyond the road edge is the outer
 *      edge of the kerb strip; a robust line per side. Zones (below) are vertical cuts, so they
 *      cover the kerbs too.
 *   5. Zones: the two road edges meet at the vanishing point V; image distance V - x is
 *      proportional to 1 / depth, so equal steps on the ground are equal steps in 1 / (V - x).
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ROAD_ZONES_COUNT       4U   /**< one zone per lamp, lamp 1 = leftmost zone in the image */
#define ROAD_ZONES_MAX_WIDTH   320U /**< largest frame the work memory is sized for */
#define ROAD_ZONES_MAX_HEIGHT  240U
#define ROAD_ZONES_COLUMN_STEP 2U   /**< every 2nd column is scanned */
#define ROAD_ZONES_MAX_EDGES   24U  /**< edges kept per column and polarity (extra ones are dropped) */
#define ROAD_ZONES_COLUMNS     (ROAD_ZONES_MAX_WIDTH / ROAD_ZONES_COLUMN_STEP)
#define ROAD_ZONES_END_ROWS    (ROAD_ZONES_MAX_HEIGHT / 4U)

/** Result of the calibration, all in frame pixels. */
typedef struct {
    bool valid;                 /**< false: road not found, the caller keeps its fallback zones */
    float top_a, top_b;         /**< top road edge    y = a * x + b */
    float bottom_a, bottom_b;   /**< bottom road edge y = a * x + b */
    bool has_left;              /**< the slanted left end below was found */
    float left_a, left_b;       /**< left end of the road x = a * y + b */
    int16_t x_min, x_max;       /**< first / last column of the road */
    float boundaries_x[ROAD_ZONES_COUNT - 1U]; /**< zone k = between boundary k-1 and k */
    /* Outer edges of the kerb (pavement) strips, y = a * x + b over x_min..x_max (find_kerbs).
     * A side is false when its outer edge was not found (e.g. it leaves the frame). */
    bool has_kerb_top;
    float kerb_top_a, kerb_top_b;       /**< outer edge of the kerb above the road */
    bool has_kerb_bottom;
    float kerb_bottom_a, kerb_bottom_b; /**< outer edge of the kerb below the road */
} road_zones_t;

/**
 * Scratch memory of one calibration (~22 KB). Allocate it once (PSRAM is fine) and free it after
 * the calibration: nothing in it is needed later.
 */
typedef struct {
    int16_t falling[ROAD_ZONES_COLUMNS][ROAD_ZONES_MAX_EDGES];
    int16_t rising[ROAD_ZONES_COLUMNS][ROAD_ZONES_MAX_EDGES];
    uint8_t n_falling[ROAD_ZONES_COLUMNS];
    uint8_t n_rising[ROAD_ZONES_COLUMNS];
    float sample_x[ROAD_ZONES_COLUMNS];
    float sample_top[ROAD_ZONES_COLUMNS];
    float sample_bottom[ROAD_ZONES_COLUMNS];
    int16_t inlier_x[ROAD_ZONES_COLUMNS]; /**< columns that fit both edges, of the last accepted fit */
    float kerb_top_x[ROAD_ZONES_COLUMNS];    /**< outer kerb edge samples above the road (find_kerbs) */
    float kerb_top_y[ROAD_ZONES_COLUMNS];
    float kerb_bottom_x[ROAD_ZONES_COLUMNS]; /**< ... and below it */
    float kerb_bottom_y[ROAD_ZONES_COLUMNS];
    float end_y[ROAD_ZONES_END_ROWS];
    float end_x[ROAD_ZONES_END_ROWS];
    float smooth[ROAD_ZONES_MAX_WIDTH];
    float strength[ROAD_ZONES_MAX_WIDTH];
    uint8_t line[ROAD_ZONES_MAX_WIDTH]; /**< one column, copied out of the frame */
} road_zones_work_t;

/**
 * Find the road in an 8-bit gray frame taken with every lamp at full level.
 * @param gray    row-major, width * height bytes
 * @param work    scratch memory (see road_zones_work_t)
 * @param out     result; out->valid tells whether the road was found
 * @return out->valid
 */
bool road_zones_calibrate(const uint8_t *gray, uint16_t width, uint16_t height, road_zones_work_t *work,
                          road_zones_t *out);

/**
 * True if pixel (x, y) is on the road OR on a kerb strip next to it (people on the pavement count),
 * with a few pixels of tolerance. A kerb side that was not found falls back to the road edge.
 */
bool road_zones_contains(const road_zones_t *road, float x, float y);

/** Zone 0..ROAD_ZONES_COUNT-1 of pixel column x (only meaningful on the road). */
unsigned road_zones_zone_of(const road_zones_t *road, float x);

#ifdef __cplusplus
}
#endif
