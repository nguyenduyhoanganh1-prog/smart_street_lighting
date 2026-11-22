/**
 * @file road_zones.c
 * @brief Start-up road calibration, method D (see road_zones.h).
 *
 * Every step mirrors a function of tools/zone_calib.py with the same name, so a difference found
 * by tools/check_c_port.py can be traced to one function. Single precision on purpose: the
 * ESP32-S3 FPU has no double, and RANSAC tries ~13 000 lines.
 */
#include "road_zones.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

/* ---- Tuning: the same values as tools/zone_calib.py ---- */
#define SMOOTH_PX          3       /* box blur before looking for edges */
#define EDGE_SPAN_PX       3       /* edge strength = brightness 3 rows below - 3 rows above */
#define EDGE_MIN           30.0f   /* weaker edges are ignored (gray levels) */
#define KERB_CONTRAST      25.0f   /* road must be this much darker than the kerb on both sides */
#define MIN_ROAD_PX        12
#define MAX_ROAD_PX        220
#define GLARE_DELTA        100.0f  /* a road pixel this much above the road median counts as bright ... */
#define MAX_DASH_PX        25      /* ... and a longer bright run inside the road is a kerb, not a dash */
#define BAND_INSET_TOP     4       /* road band for the median: rows top+4 .. bottom-4 */
#define BAND_INSET_BOTTOM  3
#define KERB_PROBE_PX      8       /* kerb brightness = mean of the rows just outside the edge */
#define SNAP_PX            6.0f    /* an edge this close to the predicted road edge is the road edge */
#define SNAP_ROUNDS        3
#define MAX_GAP_COLUMNS    6       /* a hole of this many scanned columns inside the road is tolerated */
#define FIT_TOL_PX         3.0f
#define FIT_ITERATIONS     4
#define MIN_INLIER_COLUMNS 20
#define END_EDGE_MIN       12.0f   /* the road's left end is a weak step (dark background -> dim road) */
#define END_SEARCH_PX      30
#define END_ROW_STEP       4
#define END_FIRST_X        14      /* step_holds looks 12 columns back */
#define END_BACK_FAR       12
#define END_BACK_NEAR      2
#define MIN_END_POINTS     8
#define END_HOLD_PX        20      /* the step must hold for this many columns ... */
#define END_HOLD_MIN       4.0f    /* ... staying at least this much above the level before it */
#define END_FIT_TOL_PX     6.0f    /* the end is curved by the lens: a line fits it only this closely */
#define KERB_MIN_PX        6       /* a kerb strip is at least this wide ... */
#define KERB_MAX_PX        70      /* ... and at most this wide (rows, across the road) */
#define ROAD_MARGIN_PX     8.0f    /* an object centre this far outside an edge still counts as on the road */
#define MIN_FRAME_PX       32U
#define PARALLEL_EPS       1e-6f

typedef struct {
    float a; /* y = a * x + b (or x = a * y + b for the left end) */
    float b;
} line_t;

typedef struct {
    line_t top;
    line_t bottom;
} edges_t;

/* ================================ 1-D helpers ================================ */

/** Box blur of 3 with zero padding (numpy convolve "same"), then edge strength s[i] = m[i+3] - m[i-3]. */
static void smooth_and_strength(const uint8_t *v, int n, float *smooth, float *strength)
{
    for (int i = 0; i < n; i++) {
        const float left = (i > 0) ? (float)v[i - 1] : 0.0f;
        const float right = (i + 1 < n) ? (float)v[i + 1] : 0.0f;
        smooth[i] = (left + (float)v[i] + right) / (float)SMOOTH_PX;
    }
    for (int i = 0; i < n; i++) {
        const bool inside = i >= EDGE_SPAN_PX && i < n - EDGE_SPAN_PX;
        strength[i] = inside ? smooth[i + EDGE_SPAN_PX] - smooth[i - EDGE_SPAN_PX] : 0.0f;
    }
}

/** Median of v[first .. last_excl) (mean of the two middle values for an even count), via a histogram. */
static float band_median(const uint8_t *v, int first, int last_excl)
{
    uint16_t hist[256];
    memset(hist, 0, sizeof(hist));
    for (int i = first; i < last_excl; i++) {
        hist[v[i]]++;
    }
    const int n = last_excl - first;
    const int lower_rank = (n - 1) / 2; /* rank of the lower middle value */
    const int upper_rank = n / 2;
    int seen = 0;
    int lower = -1;
    for (int value = 0; value < 256; value++) {
        seen += hist[value];
        if (lower < 0 && seen > lower_rank) {
            lower = value;
        }
        if (seen > upper_rank) {
            return 0.5f * (float)(lower + value);
        }
    }
    return (float)lower;
}

static float mean_u8(const uint8_t *v, int first, int last_excl)
{
    float sum = 0.0f;
    for (int i = first; i < last_excl; i++) {
        sum += (float)v[i];
    }
    return (last_excl > first) ? sum / (float)(last_excl - first) : 0.0f;
}

/** Longest run of consecutive v[i] > limit. */
static int longest_run_above(const uint8_t *v, int first, int last_excl, float limit)
{
    int best = 0;
    int current = 0;
    for (int i = first; i < last_excl; i++) {
        current = ((float)v[i] > limit) ? current + 1 : 0;
        best = (current > best) ? current : best;
    }
    return best;
}

/* ================================ step 1: column edges ================================ */

/** Copy column x into work->line and list its falling / rising edges (column_edges in Python). */
static void column_edges(const uint8_t *gray, int width, int height, int x, int col, road_zones_work_t *w)
{
    for (int y = 0; y < height; y++) {
        w->line[y] = gray[(size_t)y * (size_t)width + (size_t)x];
    }
    smooth_and_strength(w->line, height, w->smooth, w->strength);
    const float *s = w->strength;
    uint8_t n_fall = 0;
    uint8_t n_rise = 0;
    for (int y = EDGE_SPAN_PX + 1; y < height - EDGE_SPAN_PX - 1; y++) {
        const float here = s[y];
        if (here <= -EDGE_MIN && here <= s[y - 1] && here <= s[y + 1]) {
            if (n_fall < ROAD_ZONES_MAX_EDGES) {
                w->falling[col][n_fall++] = (int16_t)y;
            }
        } else if (here >= EDGE_MIN && here >= s[y - 1] && here >= s[y + 1]) {
            if (n_rise < ROAD_ZONES_MAX_EDGES) {
                w->rising[col][n_rise++] = (int16_t)y;
            }
        }
    }
    w->n_falling[col] = n_fall;
    w->n_rising[col] = n_rise;
}

/** True if the rows top..bottom of the column in work->line look like road between two kerbs. */
static bool is_road_band(const uint8_t *v, int height, int top, int bottom)
{
    const int first = top + BAND_INSET_TOP;
    const int last_excl = bottom - BAND_INSET_BOTTOM;
    const float inside = band_median(v, first, last_excl);
    const int above_first = (top - KERB_PROBE_PX > 0) ? top - KERB_PROBE_PX : 0;
    const float kerb_above = mean_u8(v, above_first, top - 1);
    const int below_end = (bottom + KERB_PROBE_PX + 1 < height) ? bottom + KERB_PROBE_PX + 1 : height;
    const float kerb_below = mean_u8(v, bottom + 2, below_end);
    /* a lane dash is a short bright run, a kerb inside the span a long one */
    const bool no_kerb_inside = longest_run_above(v, first, last_excl, inside + GLARE_DELTA) <= MAX_DASH_PX;
    const float kerb = (kerb_below < kerb_above) ? kerb_below : kerb_above;
    return no_kerb_inside && inside <= kerb - KERB_CONTRAST;
}

/** Widest falling -> rising span of column `col` that is road (road_run_in_column in Python). */
static bool road_run_in_column(const road_zones_work_t *w, int col, int height, int *top_out, int *bottom_out)
{
    int best_top = -1;
    int best_bottom = -1;
    for (int i = 0; i < w->n_falling[col]; i++) {
        const int top = w->falling[col][i];
        for (int j = 0; j < w->n_rising[col]; j++) {
            const int bottom = w->rising[col][j];
            const int width = bottom - top;
            const bool wider = best_top < 0 || width > best_bottom - best_top;
            if (width < MIN_ROAD_PX || width > MAX_ROAD_PX || !wider) {
                continue;
            }
            if (is_road_band(w->line, height, top, bottom)) {
                best_top = top;
                best_bottom = bottom;
            }
        }
    }
    *top_out = best_top;
    *bottom_out = best_bottom;
    return best_top >= 0;
}

/* ================================ step 2: robust lines ================================ */

/** Line through the most points within tol, trying the line of every point pair (ransac_line). */
static bool ransac_line(const float *xs, const float *ys, int n, float tol, line_t *out)
{
    int best_count = -1;
    for (int i = 0; i < n; i++) {
        for (int j = i + 1; j < n; j++) {
            const float run = xs[j] - xs[i];
            if (run <= 0.0f) {
                continue;
            }
            const float a = (ys[j] - ys[i]) / run;
            const float b = ys[i] - a * xs[i];
            int count = 0;
            for (int k = 0; k < n; k++) {
                count += (fabsf(a * xs[k] + b - ys[k]) <= tol) ? 1 : 0;
            }
            if (count > best_count) { /* strictly greater: the first best pair wins, as np.argmax */
                best_count = count;
                out->a = a;
                out->b = b;
            }
        }
    }
    return best_count >= 0;
}

static int mark_inliers(const float *xs, const float *ys, int n, const line_t *line, float tol, bool *keep)
{
    int count = 0;
    for (int i = 0; i < n; i++) {
        keep[i] = fabsf(ys[i] - (line->a * xs[i] + line->b)) <= tol;
        count += keep[i] ? 1 : 0;
    }
    return count;
}

/** Least squares through the kept points (np.polyfit degree 1); sums in double, it runs only a few times. */
static void least_squares(const float *xs, const float *ys, int n, const bool *keep, line_t *line)
{
    double sx = 0.0, sy = 0.0, sxx = 0.0, sxy = 0.0;
    int m = 0;
    for (int i = 0; i < n; i++) {
        if (keep[i]) {
            sx += xs[i];
            sy += ys[i];
            sxx += (double)xs[i] * xs[i];
            sxy += (double)xs[i] * ys[i];
            m++;
        }
    }
    const double det = (double)m * sxx - sx * sx;
    if (m < 2 || fabs(det) < 1e-9) {
        return; /* all kept points in one column: keep the previous line */
    }
    const double a = ((double)m * sxy - sx * sy) / det;
    line->a = (float)a;
    line->b = (float)((sy - a * sx) / (double)m);
}

/** RANSAC start, then least squares refits without the outliers (fit_line_robust). */
static bool fit_line_robust(const float *xs, const float *ys, int n, line_t *line, bool *keep)
{
    if (n < 2 || !ransac_line(xs, ys, n, FIT_TOL_PX, line)) {
        return false;
    }
    int kept = mark_inliers(xs, ys, n, line, FIT_TOL_PX, keep);
    for (int it = 0; it < FIT_ITERATIONS && kept >= 2; it++) {
        least_squares(xs, ys, n, keep, line);
        kept = mark_inliers(xs, ys, n, line, FIT_TOL_PX, keep);
    }
    return true;
}

/** Fit both road edges to the samples in work; the columns that fit both go to inlier_x (fit_edges). */
static bool fit_edges(road_zones_work_t *w, int n, edges_t *edges, int16_t *inlier_x, int *n_inlier)
{
    bool keep_top[ROAD_ZONES_COLUMNS];
    bool keep_bottom[ROAD_ZONES_COLUMNS];
    if (n < MIN_INLIER_COLUMNS || !fit_line_robust(w->sample_x, w->sample_top, n, &edges->top, keep_top) ||
        !fit_line_robust(w->sample_x, w->sample_bottom, n, &edges->bottom, keep_bottom)) {
        return false;
    }
    int count = 0;
    for (int i = 0; i < n; i++) {
        if (keep_top[i] && keep_bottom[i]) {
            inlier_x[count++] = (int16_t)w->sample_x[i];
        }
    }
    *n_inlier = count;
    return count >= MIN_INLIER_COLUMNS;
}

/* ================================ step 3: grow the road ================================ */

/** Row of the list closest to `expected`, if within SNAP_PX (first one on a tie). */
static bool nearest_within(const int16_t *rows, int n, float expected, int *out)
{
    float best = SNAP_PX + 1.0f;
    for (int i = 0; i < n; i++) {
        const float d = fabsf((float)rows[i] - expected);
        if (d <= SNAP_PX && d < best) {
            best = d;
            *out = rows[i];
        }
    }
    return best <= SNAP_PX;
}

static bool snap_column(const road_zones_work_t *w, int col, float x, const edges_t *fit, int *top, int *bottom)
{
    return nearest_within(w->falling[col], w->n_falling[col], fit->top.a * x + fit->top.b, top) &&
           nearest_within(w->rising[col], w->n_rising[col], fit->bottom.a * x + fit->bottom.b, bottom) &&
           *bottom - *top >= MIN_ROAD_PX;
}

/** Longest run of columns with gaps of at most MAX_GAP_COLUMNS (longest_stretch). */
static bool longest_stretch(const int16_t *xs, int n, int *first, int *last)
{
    int start = -1;
    int previous = -1;
    *first = -1;
    *last = -1;
    for (int i = 0; i < n; i++) {
        const int x = xs[i];
        if (start < 0 || x - previous > MAX_GAP_COLUMNS * (int)ROAD_ZONES_COLUMN_STEP) {
            start = x;
        }
        previous = x;
        if (*first < 0 || previous - start > *last - *first) {
            *first = start;
            *last = previous;
        }
    }
    return *first >= 0;
}

static void add_sample(road_zones_work_t *w, int *n, int x, int top, int bottom)
{
    w->sample_x[*n] = (float)x;
    w->sample_top[*n] = (float)top;
    w->sample_bottom[*n] = (float)bottom;
    (*n)++;
}

/** Road edges and x range (find_road without the left end). */
static bool find_road(const uint8_t *gray, int width, int height, road_zones_work_t *w, road_zones_t *out)
{
    const int columns = (width + (int)ROAD_ZONES_COLUMN_STEP - 1) / (int)ROAD_ZONES_COLUMN_STEP;
    int16_t candidate_x[ROAD_ZONES_COLUMNS];
    int n_candidate = 0;
    int n = 0;
    for (int col = 0; col < columns; col++) {
        const int x = col * (int)ROAD_ZONES_COLUMN_STEP;
        column_edges(gray, width, height, x, col, w);
        int top = 0;
        int bottom = 0;
        if (road_run_in_column(w, col, height, &top, &bottom)) {
            add_sample(w, &n, x, top, bottom);
        }
    }
    edges_t fit;
    int n_accepted = 0;
    if (!fit_edges(w, n, &fit, w->inlier_x, &n_accepted)) {
        return false;
    }
    for (int round = 0; round < SNAP_ROUNDS; round++) {
        n = 0;
        for (int col = 0; col < columns; col++) {
            const int x = col * (int)ROAD_ZONES_COLUMN_STEP;
            int top = 0;
            int bottom = 0;
            if (snap_column(w, col, (float)x, &fit, &top, &bottom)) {
                add_sample(w, &n, x, top, bottom);
            }
        }
        edges_t refit;
        if (!fit_edges(w, n, &refit, candidate_x, &n_candidate)) {
            break; /* keep the previous fit and its inlier columns */
        }
        fit = refit;
        memcpy(w->inlier_x, candidate_x, sizeof(candidate_x[0]) * (size_t)n_candidate);
        n_accepted = n_candidate;
    }
    int first, last;
    if (!longest_stretch(w->inlier_x, n_accepted, &first, &last)) {
        return false;
    }
    out->top_a = fit.top.a;
    out->top_b = fit.top.b;
    out->bottom_a = fit.bottom.a;
    out->bottom_b = fit.bottom.b;
    out->x_min = (int16_t)first;
    out->x_max = (int16_t)last;
    return true;
}

/* ================================ step 4: left end ================================ */

/** Brightness steps up at x and STAYS up for END_HOLD_PX columns (step_holds): rejects bright specks. */
static bool step_holds(const float *row, int n, int x)
{
    float before = 0.0f;
    for (int i = x - END_BACK_FAR; i < x - END_BACK_NEAR; i++) {
        before += row[i];
    }
    before /= (float)(END_BACK_FAR - END_BACK_NEAR);
    const int first = x + 2;
    const int last_excl = (first + END_HOLD_PX < n) ? first + END_HOLD_PX : n;
    if (last_excl - first < END_HOLD_PX / 2) {
        return false;
    }
    float sum = 0.0f;
    float low = row[first];
    for (int i = first; i < last_excl; i++) {
        sum += row[i];
        low = (row[i] < low) ? row[i] : low;
    }
    return sum / (float)(last_excl - first) >= before + END_EDGE_MIN && low >= before + END_HOLD_MIN;
}

/** Slanted left end x = a*y + b from the first lasting dark -> road step of each row (find_left_end). */
static void find_left_end(const uint8_t *gray, int width, int height, road_zones_work_t *w, road_zones_t *road)
{
    const float x0 = (float)road->x_min;
    const int limit = (width - 4 < road->x_min + END_SEARCH_PX) ? width - 4 : road->x_min + END_SEARCH_PX;
    const int bottom_row = (int)(road->bottom_a * x0 + road->bottom_b);
    const int last_row = ((height - 1 < bottom_row) ? height - 1 : bottom_row) - 2; /* rows below the road are not road */
    int n = 0;
    for (int y = (int)(road->top_a * x0 + road->top_b) + 4; y < last_row && n < (int)ROAD_ZONES_END_ROWS; y += END_ROW_STEP) {
        if (y < 0) {
            continue;
        }
        memcpy(w->line, gray + (size_t)y * (size_t)width, (size_t)width);
        smooth_and_strength(w->line, width, w->smooth, w->strength);
        const float *s = w->strength;
        for (int x = END_FIRST_X; x < limit; x++) {
            const bool peak = s[x] >= END_EDGE_MIN && s[x] >= s[x - 1] && s[x] >= s[x + 1];
            if (peak && step_holds(w->smooth, width, x)) {
                w->end_y[n] = (float)y;
                w->end_x[n] = (float)x;
                n++;
                break;
            }
        }
    }
    line_t end;
    if (n < MIN_END_POINTS || !ransac_line(w->end_y, w->end_x, n, END_FIT_TOL_PX, &end)) {
        return;
    }
    int agree = 0;
    for (int i = 0; i < n; i++) {
        agree += (fabsf(w->end_x[i] - (end.a * w->end_y[i] + end.b)) <= END_FIT_TOL_PX) ? 1 : 0;
    }
    if (agree < MIN_END_POINTS) {
        return;
    }
    /* where the end line meets the top edge = first column of the road */
    const float x_on_top = (end.a * road->top_b + end.b) / (1.0f - end.a * road->top_a);
    const long rounded = lrintf(x_on_top); /* round half to even, like Python round() */
    const long x_min = (rounded < road->x_min) ? rounded : road->x_min;
    road->x_min = (int16_t)((x_min > 0) ? x_min : 0);
    road->has_left = true;
    road->left_a = end.a;
    road->left_b = end.b;
}

/* ================================ step 5: kerbs ================================ */

/**
 * Outer kerb edges of the column whose edges are in slot `col` (kerb_edges_in_column): above the road
 * the kerb ends at a rising edge, below it at a falling edge, KERB_MIN_PX..KERB_MAX_PX rows beyond the
 * road edge; the one nearest to the road wins. -1 = that side has no outer edge in this column.
 */
static void kerb_edges_in_column(const road_zones_work_t *w, int col, float top, float bottom, int *above,
                                 int *below)
{
    *above = -1;
    *below = -1;
    for (int i = 0; i < w->n_rising[col]; i++) {
        const int y = w->rising[col][i];
        if ((float)y >= top - (float)KERB_MAX_PX && (float)y <= top - (float)KERB_MIN_PX && y > *above) {
            *above = y;
        }
    }
    for (int i = 0; i < w->n_falling[col]; i++) {
        const int y = w->falling[col][i];
        const bool in_range = (float)y >= bottom + (float)KERB_MIN_PX && (float)y <= bottom + (float)KERB_MAX_PX;
        if (in_range && (*below < 0 || y < *below)) {
            *below = y;
        }
    }
}

/** Robust line through the outer-edge samples of one kerb side, if enough columns agree (fit_kerb_side). */
static bool fit_kerb_side(const float *xs, const float *ys, int n, line_t *line)
{
    bool keep[ROAD_ZONES_COLUMNS];
    if (n < MIN_INLIER_COLUMNS || !fit_line_robust(xs, ys, n, line, keep)) {
        return false;
    }
    int kept = 0;
    for (int i = 0; i < n; i++) {
        kept += keep[i] ? 1 : 0;
    }
    return kept >= MIN_INLIER_COLUMNS;
}

/** Outer edge of the kerb strip on each side of the found road (find_kerbs); each side may be missing. */
static void find_kerbs(const uint8_t *gray, int width, int height, road_zones_work_t *w, road_zones_t *road)
{
    const int scratch_col = 0; /* the per-column edges of find_road are not needed any more: reuse slot 0 */
    int n_top = 0;
    int n_bottom = 0;
    for (int x = road->x_min; x <= road->x_max && x < width; x += (int)ROAD_ZONES_COLUMN_STEP) {
        column_edges(gray, width, height, x, scratch_col, w);
        const float top = road->top_a * (float)x + road->top_b;
        const float bottom = road->bottom_a * (float)x + road->bottom_b;
        int above = -1;
        int below = -1;
        kerb_edges_in_column(w, scratch_col, top, bottom, &above, &below);
        if (above >= 0 && n_top < (int)ROAD_ZONES_COLUMNS) {
            w->kerb_top_x[n_top] = (float)x;
            w->kerb_top_y[n_top++] = (float)above;
        }
        if (below >= 0 && n_bottom < (int)ROAD_ZONES_COLUMNS) {
            w->kerb_bottom_x[n_bottom] = (float)x;
            w->kerb_bottom_y[n_bottom++] = (float)below;
        }
    }
    line_t fit;
    if (fit_kerb_side(w->kerb_top_x, w->kerb_top_y, n_top, &fit)) {
        road->has_kerb_top = true;
        road->kerb_top_a = fit.a;
        road->kerb_top_b = fit.b;
    }
    if (fit_kerb_side(w->kerb_bottom_x, w->kerb_bottom_y, n_bottom, &fit)) {
        road->has_kerb_bottom = true;
        road->kerb_bottom_a = fit.a;
        road->kerb_bottom_b = fit.b;
    }
}

/* ================================ step 6: zones ================================ */

/** x where the road starts on its centre line (road_start_x). */
static float road_start_x(const road_zones_t *road)
{
    if (!road->has_left) {
        return (float)road->x_min;
    }
    const float centre_a = 0.5f * (road->top_a + road->bottom_a);
    const float centre_b = 0.5f * (road->top_b + road->bottom_b);
    return (road->left_a * centre_b + road->left_b) / (1.0f - road->left_a * centre_a);
}

/** Equal stretches on the ground (equal_zones): equal steps of 1 / (V - x) towards the vanishing point V. */
static void equal_zones(road_zones_t *road)
{
    const float start = road_start_x(road);
    const float end = (float)road->x_max;
    const float gap = road->top_a - road->bottom_a;
    const float vx = (fabsf(gap) < PARALLEL_EPS) ? 0.0f : (road->bottom_b - road->top_b) / gap;
    const bool has_vanishing = fabsf(gap) >= PARALLEL_EPS && vx > end;
    const float near = has_vanishing ? 1.0f / (vx - start) : 0.0f;
    const float far = has_vanishing ? 1.0f / (vx - end) : 0.0f;
    for (unsigned k = 1; k < ROAD_ZONES_COUNT; k++) {
        const float t = (float)k / (float)ROAD_ZONES_COUNT;
        road->boundaries_x[k - 1U] = has_vanishing ? vx - 1.0f / (near + (far - near) * t) : start + (end - start) * t;
    }
}

/* ================================ public API ================================ */

bool road_zones_calibrate(const uint8_t *gray, uint16_t width, uint16_t height, road_zones_work_t *work,
                          road_zones_t *out)
{
    if (out == NULL) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    const bool size_ok = width >= MIN_FRAME_PX && height >= MIN_FRAME_PX && width <= ROAD_ZONES_MAX_WIDTH &&
                         height <= ROAD_ZONES_MAX_HEIGHT;
    if (gray == NULL || work == NULL || !size_ok) {
        return false;
    }
    if (!find_road(gray, (int)width, (int)height, work, out)) {
        return false;
    }
    find_left_end(gray, (int)width, (int)height, work, out);
    find_kerbs(gray, (int)width, (int)height, work, out); /* after the left end: it may move x_min */
    equal_zones(out);
    out->valid = true;
    return true;
}

bool road_zones_contains(const road_zones_t *road, float x, float y)
{
    if (road == NULL || !road->valid || x < (float)road->x_min || x > (float)road->x_max) {
        return false;
    }
    if (road->has_left && x < road->left_a * y + road->left_b - ROAD_MARGIN_PX) {
        return false;
    }
    /* Outer limits: the kerb edge when found, else the road edge. fminf/fmaxf keep a badly fitted kerb
     * line (crossing into the road) from ever making the area smaller than the road itself. */
    float top = road->top_a * x + road->top_b;
    float bottom = road->bottom_a * x + road->bottom_b;
    if (road->has_kerb_top) {
        top = fminf(top, road->kerb_top_a * x + road->kerb_top_b);
    }
    if (road->has_kerb_bottom) {
        bottom = fmaxf(bottom, road->kerb_bottom_a * x + road->kerb_bottom_b);
    }
    return y >= top - ROAD_MARGIN_PX && y <= bottom + ROAD_MARGIN_PX;
}

unsigned road_zones_zone_of(const road_zones_t *road, float x)
{
    unsigned zone = 0U;
    for (unsigned k = 0; road != NULL && k < ROAD_ZONES_COUNT - 1U; k++) {
        zone += (x >= road->boundaries_x[k]) ? 1U : 0U;
    }
    return zone;
}
