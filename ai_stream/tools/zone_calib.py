"""Prototype of the start-up zone calibration of ai_stream, run on the PC before it is ported to C.

What the firmware will do at boot (this script does the same steps with numpy so the maths can be
checked on real frames first):

1. All lamps at max -> find the road: a dark band between two bright kerbs, one column at a time.
2. One lamp at a time (others off) -> the part of the road that lamp lights = its light map.
3. Zones = the road cut between lamps. A lamp's zone boundary is the middle between its light
   centre and the next lamp's. Objects outside the road are ignored.

Fallback order decided for the firmware: A (light of each lamp) -> if a lamp cannot be located, ask
for the 4 positions on the web page (not in this script) -> if nobody answers by night, D = equal
split of the road with perspective (``equal_zones``, used automatically here when A fails).

Only simple integer-friendly operations are used (threshold, column scan, line fit, centroid), no
OpenCV, so each step maps to a small C function.

Two ways to get the frames::

    # 1) Offline: frames of a dataset session (``nen`` rows of index.csv, random lamp levels).
    #    The light of each lamp is then solved by least squares over all those frames.
    python zone_calib.py offline E:/TKHTN/dataset/phien_008 -o calib_out

    # 2) Live: the board running ai_stream (laptop joined to WiFi "StreetLight-AI"). Lamps are
    #    driven one by one through /api/lamp, frames come from /frame.jpg.
    python zone_calib.py live --host 192.168.4.1 -o calib_out

Output folder: ``overlay.png`` (road + zones on the all-lamps frame), ``lamp_maps.png`` (light of
each lamp), ``calib.json`` (numbers the C code has to reproduce), ``frames/`` (live mode only, so
the run can be repeated with ``from-dir``).
"""
from __future__ import annotations

import argparse
import csv
import io
import json
import logging
import sys
import time
import urllib.request
from dataclasses import asdict, dataclass, replace
from pathlib import Path
from typing import Sequence

import numpy as np
from PIL import Image, ImageDraw

LOG = logging.getLogger("zone_calib")

FRAME_W = 320
FRAME_H = 240
LAMP_COUNT = 4
COLUMN_STEP = 2            # scan every 2nd column
SMOOTH_PX = 3              # box blur along a column before looking for edges (kills sensor noise)
EDGE_SPAN_PX = 3           # edge strength = brightness 3 rows below minus 3 rows above
EDGE_MIN = 30              # weaker edges are ignored (gray levels)
KERB_CONTRAST = 25         # the road must be this much darker than the kerb on both sides
KERB_MIN_PX = 6            # a kerb strip is at least this wide ...
KERB_MAX_PX = 70           # ... and at most this wide (rows, measured across the road direction)
END_EDGE_MIN = 12          # the road's left end is a weak step (dark background -> dim road), weaker than EDGE_MIN
END_SEARCH_PX = 30         # look for it up to this far right of the road's first scanned column
END_ROW_STEP = 4           # scan every 4th row
MIN_END_POINTS = 8         # rows that must agree on the left end line
END_HOLD_PX = 20           # the step up must hold for this many columns ...
END_HOLD_MIN = 4           # ... staying at least this much above the level before it (gray levels)
END_FIT_TOL_PX = 6.0       # the end is curved by lens distortion: a straight line fits it only this closely
KERB_RATIO_PLAIN = 6.0 / 13.0  # real model: kerb without lamps is 6/13 of the road width
KERB_RATIO_LAMP = 5.0 / 13.0   # ... and the kerb that carries the lamps is 5/13 of it
LAMP_SIDES = ("top", "bottom")  # which kerb in the image carries the lamps
MIN_ROAD_PX = 12           # a road band thinner than this is not the road
MAX_ROAD_PX = 220          # ... and wider than this is not either
GLARE_DELTA = 100          # a road pixel this much brighter than the column's road median counts as "bright" ...
MAX_DASH_PX = 25           # ... and a bright run longer than this (rows) inside the road is a kerb, not a lane dash
SNAP_PX = 6                # an edge this close to the predicted road edge is taken as the road edge
SNAP_ROUNDS = 3            # predict-and-refit passes that grow the road to its real ends
MAX_GAP_COLUMNS = 6        # a hole of this many scanned columns inside the road is tolerated
FIT_TOL_PX = 3.0           # column edges further than this from the fitted line are outliers
FIT_ITERATIONS = 4
MIN_INLIER_COLUMNS = 20
CELL = 4                   # light maps are computed on CELL x CELL blocks (80 x 60 grid)
PEAK_FRACTION = 0.5        # light centre uses only the part of a lamp's light above this x its peak
DASH_HALF_PX = 6           # rows within this distance of the road centre line are left out of the light maps
MIN_REL_PEAK = 0.15        # a lamp whose light peak on the road is below this x the best lamp's is "not seen"
ROAD_MARGIN_PX = 8        # an object centre this far above/below the road edge still counts as on it
LIVE_SETTLE_S = 1.5        # camera auto exposure needs a moment after every lamp change
LIVE_FRESH_FRAMES = 3      # frames to wait for after a lamp change before using one
HTTP_TIMEOUT_S = 10.0
OVERLAY_SCALE = 2


# ================================ data ================================


@dataclass(frozen=True)
class RoadModel:
    """Road = area between two straight edges ``y = a*x + b`` over ``x_min..x_max`` (pixels)."""

    top_a: float
    top_b: float
    bottom_a: float
    bottom_b: float
    x_min: int
    x_max: int
    left_a: float | None = None  # slanted left end of the road: x = left_a * y + left_b (None = vertical at x_min)
    left_b: float | None = None

    def top(self, x: float) -> float:
        return self.top_a * x + self.top_b

    def bottom(self, x: float) -> float:
        return self.bottom_a * x + self.bottom_b

    def contains(self, x: float, y: float, margin: float = ROAD_MARGIN_PX) -> bool:
        """True if (x, y) is on the road, with ``margin`` pixels of tolerance above and below."""
        if x < self.x_min or x > self.x_max:
            return False
        if self.left_a is not None and self.left_b is not None and x < self.left_a * y + self.left_b - margin:
            return False
        return self.top(x) - margin <= y <= self.bottom(x) + margin


@dataclass(frozen=True)
class KerbModel:
    """Outer edge of the kerb strip above and below the road: ``y = a*x + b``, valid over the road's x range.

    The kerb strip is the band between the road edge (RoadModel) and this outer edge. Which strip is
    the "left" or "right" kerb depends on the driving direction; here they are only top and bottom.
    A side is None when its outer edge was not found (e.g. it lies outside the frame).
    """

    top_a: float | None = None
    top_b: float | None = None
    bottom_a: float | None = None
    bottom_b: float | None = None

    def top(self, x: float) -> float | None:
        return None if self.top_a is None or self.top_b is None else self.top_a * x + self.top_b

    def bottom(self, x: float) -> float | None:
        return None if self.bottom_a is None or self.bottom_b is None else self.bottom_a * x + self.bottom_b


@dataclass(frozen=True)
class ZoneModel:
    """Road cut in ``len(lamp_of_zone)`` zones by vertical lines at ``boundaries_x``.

    ``lamp_of_zone[k]`` is the lamp index (0-based) whose light centre lies in zone ``k``, so the
    wiring order of the lamps does not matter.
    """

    boundaries_x: tuple[float, ...]
    lamp_of_zone: tuple[int, ...]
    centres: tuple[tuple[float, float], ...]  # light centre (x, y) of every lamp, indexed by lamp
    source: str = "light"  # "light" = from each lamp's light (method A), "equal" = equal split (method D)

    def zone_of(self, x: float) -> int:
        zone = 0
        for boundary in self.boundaries_x:
            if x >= boundary:
                zone += 1
        return zone


# ================================ image input ================================


def load_gray(path: Path) -> np.ndarray:
    """Load a PGM/PNG/JPEG file as a ``(FRAME_H, FRAME_W)`` uint8 array."""
    with Image.open(path) as img:
        return to_frame(img)


def to_frame(img: Image.Image) -> np.ndarray:
    """Convert any image to the 320x240 8-bit gray frame the firmware works on."""
    gray = img.convert("L")
    if gray.size != (FRAME_W, FRAME_H):
        gray = gray.resize((FRAME_W, FRAME_H), Image.BILINEAR)
    return np.asarray(gray, dtype=np.uint8)


# ================================ step 1: road ================================


def column_edges(column: np.ndarray) -> tuple[list[int], list[int]]:
    """Rows of the falling (bright -> dark, going down) and rising (dark -> bright) edges of a column.

    A fixed brightness threshold cannot be used: the road is dark far from the lamps but turns as
    bright as 150 right under one, while the kerbs stay brighter still. Edges (changes of
    brightness) are stable under that, so the road is found as "falling edge ... rising edge".
    """
    smooth = np.convolve(column.astype(np.float32), np.ones(SMOOTH_PX, dtype=np.float32) / SMOOTH_PX, mode="same")
    span = EDGE_SPAN_PX
    strength = np.zeros(len(column), dtype=np.float32)
    strength[span:-span] = smooth[2 * span :] - smooth[: -2 * span]
    falling: list[int] = []
    rising: list[int] = []
    for y in range(span + 1, len(column) - span - 1):
        here = strength[y]
        if here <= -EDGE_MIN and here <= strength[y - 1] and here <= strength[y + 1]:
            falling.append(y)
        elif here >= EDGE_MIN and here >= strength[y - 1] and here >= strength[y + 1]:
            rising.append(y)
    return falling, rising


def longest_run(flags: np.ndarray) -> int:
    """Length of the longest run of consecutive True values."""
    best = current = 0
    for flag in flags:
        current = current + 1 if flag else 0
        best = max(best, current)
    return best


def road_run_in_column(column: np.ndarray) -> tuple[int, int] | None:
    """Rows ``(top, bottom)`` of the road in one column, or None.

    The road is the widest span from a falling edge down to a rising edge whose inside is clearly
    darker than the kerb above and the kerb below. A lane dash is bright inside the road, so it
    only makes extra edges; the widest span still reaches from kerb to kerb.
    """
    falling, rising = column_edges(column)
    best: tuple[int, int] | None = None
    for top in falling:
        for bottom in rising:
            width = bottom - top
            if width < MIN_ROAD_PX or width > MAX_ROAD_PX or (best is not None and width <= best[1] - best[0]):
                continue
            band = column[top + 4 : bottom - 3].astype(np.float32)
            inside = float(np.median(band))
            kerb_above = float(column[max(0, top - 8) : top - 1].mean())
            kerb_below = float(column[bottom + 2 : bottom + 9].mean())
            no_kerb_inside = longest_run(band > inside + GLARE_DELTA) <= MAX_DASH_PX  # a lane dash is short, a kerb is long
            if no_kerb_inside and inside <= min(kerb_above, kerb_below) - KERB_CONTRAST:
                best = (top, bottom)
    return best


def ransac_line(xs: np.ndarray, ys: np.ndarray, tol: float = FIT_TOL_PX) -> tuple[float, float]:
    """Line ``y = a*x + b`` through the most points within FIT_TOL_PX, trying the line of every point pair.

    About half of the scanned columns can be wrong (road ends, clutter), so a least-squares fit is
    dragged away before outliers can be rejected, and even a median-slope fit breaks down. Trying
    every pair is exhaustive, deterministic and cheap for ~150 points (~1M operations).
    """
    first, second = np.triu_indices(len(xs), k=1)
    run = xs[second] - xs[first]
    usable = run > 0
    first, second, run = first[usable], second[usable], run[usable]
    slope = (ys[second] - ys[first]) / run
    offset = ys[first] - slope * xs[first]
    residual = np.abs(slope[:, None] * xs[None, :] + offset[:, None] - ys[None, :])
    best = int(np.argmax((residual <= tol).sum(axis=1)))
    return float(slope[best]), float(offset[best])


def fit_line_robust(xs: np.ndarray, ys: np.ndarray) -> tuple[float, float, np.ndarray]:
    """Least-squares line ``y = a*x + b`` refitted without outliers; returns ``(a, b, inlier_mask)``."""
    if len(xs) < 2:
        return 0.0, float(ys.mean()) if len(ys) else 0.0, np.ones(len(xs), dtype=bool)
    a, b = ransac_line(xs, ys)
    keep = np.abs(ys - (a * xs + b)) <= FIT_TOL_PX
    for _ in range(FIT_ITERATIONS):
        if keep.sum() < 2:
            break
        a, b = np.polyfit(xs[keep], ys[keep], 1)
        keep = np.abs(ys - (a * xs + b)) <= FIT_TOL_PX
    return float(a), float(b), keep


def find_road(lit: np.ndarray, steps: StepSaver | None = None) -> RoadModel | None:
    """Find the road in a frame taken with all lamps at max; None if it is not recognisable."""
    steps = steps or StepSaver(None)
    columns = list(range(0, FRAME_W, COLUMN_STEP))
    edges = {x: column_edges(lit[:, x]) for x in columns}
    steps.save("column_edges", draw_edge_candidates(lit, edges))
    seeds = [(x, road_run_in_column(lit[:, x])) for x in columns]
    seed_samples = [(x, found[0], found[1]) for x, found in seeds if found is not None]
    fit = fit_edges(seed_samples)
    steps.save("seed_fit", draw_samples(lit, seed_samples, fit[0] if fit else None))
    if fit is None:
        return None
    for round_no in range(1, SNAP_ROUNDS + 1):
        snapped = [(x, *pair) for x in columns if (pair := snap_column(x, edges[x], fit[0])) is not None]
        refit = fit_edges(snapped)
        steps.save(f"snap_round_{round_no}", draw_samples(lit, snapped, refit[0] if refit else None))
        if refit is None:
            break
        fit = refit
    line_model, inlier_columns = fit
    span = longest_stretch(inlier_columns)
    if span is None:
        return None
    top_a, top_b, bottom_a, bottom_b = line_model
    model = RoadModel(top_a, top_b, bottom_a, bottom_b, span[0], span[1])
    end = find_left_end(lit, model)
    steps.save("left_end", draw_left_end(lit, model, end))
    if end is None:
        return model
    x_on_top = (end[0] * model.top_b + end[1]) / (1.0 - end[0] * model.top_a)  # where the end line meets the top edge
    return replace(model, x_min=max(0, min(model.x_min, int(round(x_on_top)))), left_a=end[0], left_b=end[1])


EdgeLines = tuple[float, float, float, float]  # top_a, top_b, bottom_a, bottom_b


def fit_edges(samples: Sequence[tuple[int, int, int]]) -> tuple[EdgeLines, list[int]] | None:
    """Fit the top and bottom road edge to ``(x, top_row, bottom_row)`` samples; also the inlier columns."""
    if len(samples) < MIN_INLIER_COLUMNS:
        return None
    cols = np.asarray([s[0] for s in samples], dtype=np.float64)
    top_a, top_b, top_ok = fit_line_robust(cols, np.asarray([s[1] for s in samples], dtype=np.float64))
    bottom_a, bottom_b, bottom_ok = fit_line_robust(cols, np.asarray([s[2] for s in samples], dtype=np.float64))
    both = top_ok & bottom_ok
    if both.sum() < MIN_INLIER_COLUMNS:
        return None
    return (top_a, top_b, bottom_a, bottom_b), [int(x) for x in cols[both]]


def nearest_within(rows: Sequence[int], expected: float) -> int | None:
    """The row closest to ``expected``, if it is within SNAP_PX."""
    close = [y for y in rows if abs(y - expected) <= SNAP_PX]
    return min(close, key=lambda y: abs(y - expected)) if close else None


def snap_column(x: int, found: tuple[list[int], list[int]], lines: EdgeLines) -> tuple[int, int] | None:
    """Edges of column ``x`` nearest to where the current lines predict them, or None if none is close."""
    falling, rising = found
    top = nearest_within(falling, lines[0] * x + lines[1])
    bottom = nearest_within(rising, lines[2] * x + lines[3])
    if top is None or bottom is None or bottom - top < MIN_ROAD_PX:
        return None
    return top, bottom


def longest_stretch(columns: Sequence[int]) -> tuple[int, int] | None:
    """First and last column of the longest run of columns with gaps of at most MAX_GAP_COLUMNS."""
    best: tuple[int, int] | None = None
    start = previous = None
    for x in sorted(columns):
        if start is None or x - previous > MAX_GAP_COLUMNS * COLUMN_STEP:
            start = x
        previous = x
        if best is None or previous - start > best[1] - best[0]:
            best = (start, previous)
    return best


def step_holds(row: np.ndarray, x: int) -> bool:
    """True if brightness steps up at ``x`` and STAYS up for the next END_HOLD_PX columns.

    A bright speck or a thin object at the frame border steps up too, but falls back at once; the
    road keeps its level, so only the road's end passes.
    """
    before = float(row[max(0, x - 12) : x - 2].mean())
    after = row[x + 2 : x + 2 + END_HOLD_PX]
    return len(after) >= END_HOLD_PX // 2 and float(after.mean()) >= before + END_EDGE_MIN and float(after.min()) >= before + END_HOLD_MIN


def find_left_end(lit: np.ndarray, road: RoadModel) -> tuple[float, float] | None:
    """Slanted left end of the road, ``x = a*y + b``, or None.

    The columns left of the road's bottom edge have no bottom edge (it leaves the frame) and the road
    ends along a slanted line, so it is found on rows instead: in each row below the top edge the first
    step from the dark background to the dimly lit road. Wrong rows (objects at the border) are
    voted out by the same RANSAC line fit.
    """
    points: list[tuple[int, int]] = []
    limit = min(FRAME_W - 4, road.x_min + END_SEARCH_PX)
    last_row = min(FRAME_H - 1, int(road.bottom(road.x_min))) - 2  # rows below the road's bottom edge are not road
    for y in range(int(road.top(road.x_min)) + 4, last_row, END_ROW_STEP):
        smooth = np.convolve(lit[y].astype(np.float32), np.ones(SMOOTH_PX, dtype=np.float32) / SMOOTH_PX, mode="same")
        strength = np.zeros(FRAME_W, dtype=np.float32)
        strength[EDGE_SPAN_PX:-EDGE_SPAN_PX] = smooth[2 * EDGE_SPAN_PX :] - smooth[: -2 * EDGE_SPAN_PX]
        for x in range(14, limit):
            is_peak = strength[x] >= END_EDGE_MIN and strength[x] >= strength[x - 1] and strength[x] >= strength[x + 1]
            if is_peak and step_holds(smooth, x):
                points.append((y, x))
                break
    if len(points) < MIN_END_POINTS:
        return None
    ys = np.asarray([p[0] for p in points], dtype=np.float64)
    xs = np.asarray([p[1] for p in points], dtype=np.float64)
    a, b = ransac_line(ys, xs, END_FIT_TOL_PX)
    inliers = int((np.abs(xs - (a * ys + b)) <= END_FIT_TOL_PX).sum())
    return (a, b) if inliers >= MIN_END_POINTS else None


def road_start_x(road: RoadModel) -> float:
    """x where the road starts on its centre line: the slanted left end meets it (else simply x_min)."""
    if road.left_a is None or road.left_b is None:
        return float(road.x_min)
    centre_a = (road.top_a + road.bottom_a) / 2.0
    centre_b = (road.top_b + road.bottom_b) / 2.0
    return (road.left_a * centre_b + road.left_b) / (1.0 - road.left_a * centre_a)


def left_end_segment(road: RoadModel) -> tuple[tuple[float, float], tuple[float, float]]:
    """Two points of the road's left end for drawing: from the top edge down to the bottom edge or the frame bottom."""
    if road.left_a is None or road.left_b is None:
        return (road.x_min, road.top(road.x_min)), (road.x_min, road.bottom(road.x_min))
    start = (road.left_a * road.top_b + road.left_b) / (1.0 - road.left_a * road.top_a)
    x_end = (road.left_a * road.bottom_b + road.left_b) / (1.0 - road.left_a * road.bottom_a)
    y_end = road.bottom(x_end)
    if y_end > FRAME_H - 1:
        y_end = FRAME_H - 1.0
        x_end = road.left_a * y_end + road.left_b
    return (start, road.top(start)), (x_end, y_end)


def draw_left_end(lit: np.ndarray, road: RoadModel, end: tuple[float, float] | None) -> Image.Image:
    """The road found so far (green edges) and its slanted left end (yellow), if found."""
    img, draw = frame_canvas(lit)
    s = OVERLAY_SCALE
    for fn in (road.top, road.bottom):
        draw.line([(road.x_min * s, fn(road.x_min) * s), (road.x_max * s, fn(road.x_max) * s)], fill=(0, 255, 0), width=1)
    if end is not None:
        a, b = end
        draw.line([(b * s, 0), ((a * (FRAME_H - 1) + b) * s, (FRAME_H - 1) * s)], fill=COLOR_LINE, width=2)
    return img


def kerb_edges_in_column(x: int, found: tuple[list[int], list[int]], road: RoadModel) -> tuple[int | None, int | None]:
    """Outer kerb edges ``(top_row, bottom_row)`` of one column, nearest to the road; None per missing side.

    Above the road the kerb is bright and ends where the brightness steps up from what lies beyond
    (a rising edge); below the road it ends at a falling edge. The two sides are independent: a kerb
    that leaves the frame must not cost the other one.
    """
    falling, rising = found
    top, bottom = road.top(x), road.bottom(x)
    above = [y for y in rising if top - KERB_MAX_PX <= y <= top - KERB_MIN_PX]
    below = [y for y in falling if bottom + KERB_MIN_PX <= y <= bottom + KERB_MAX_PX]
    return (max(above) if above else None), (min(below) if below else None)


def fit_kerb_side(points: Sequence[tuple[int, int]]) -> tuple[float, float] | None:
    """Line ``y = a*x + b`` through the outer-edge points of one kerb, or None if too few agree."""
    if len(points) < MIN_INLIER_COLUMNS:
        return None
    xs = np.asarray([p[0] for p in points], dtype=np.float64)
    ys = np.asarray([p[1] for p in points], dtype=np.float64)
    a, b, keep = fit_line_robust(xs, ys)
    return (a, b) if keep.sum() >= MIN_INLIER_COLUMNS else None


def find_kerbs(lit: np.ndarray, road: RoadModel, steps: StepSaver | None = None) -> KerbModel | None:
    """Fit the outer edge of the kerb strip on each side of a known road; None if neither side is found."""
    steps = steps or StepSaver(None)
    top_points: list[tuple[int, int]] = []
    bottom_points: list[tuple[int, int]] = []
    for x in range(road.x_min, road.x_max + 1, COLUMN_STEP):
        top_row, bottom_row = kerb_edges_in_column(x, column_edges(lit[:, x]), road)
        if top_row is not None:
            top_points.append((x, top_row))
        if bottom_row is not None:
            bottom_points.append((x, bottom_row))
    top_fit, bottom_fit = fit_kerb_side(top_points), fit_kerb_side(bottom_points)
    img, draw = frame_canvas(lit)
    draw_dots(draw, top_points + bottom_points, COLOR_SAMPLE)
    steps.save("kerb_samples", img)
    if top_fit is None and bottom_fit is None:
        return None
    return KerbModel(*(top_fit or (None, None)), *(bottom_fit or (None, None)))


def kerbs_from_ratio(road: RoadModel, lamp_side: str) -> KerbModel:
    """Expected outer kerb edges from the real proportions (6/13 plain kerb, 5/13 lamp kerb, of the road width).

    Along a column the road is ``h(x) = bottom(x) - top(x)`` rows tall, so each kerb is taken as its
    ratio times ``h(x)`` beyond the road edge. Both edges are straight lines, so the result is again
    a line pair. Exact only when the vertical scale is the same across the kerb (a good approximation
    for a narrow view), so it is drawn next to the detected kerb, not instead of it.
    """
    top_ratio = KERB_RATIO_LAMP if lamp_side == "top" else KERB_RATIO_PLAIN
    bottom_ratio = KERB_RATIO_LAMP if lamp_side == "bottom" else KERB_RATIO_PLAIN
    h_a, h_b = road.bottom_a - road.top_a, road.bottom_b - road.top_b
    return KerbModel(road.top_a - top_ratio * h_a, road.top_b - top_ratio * h_b, road.bottom_a + bottom_ratio * h_a, road.bottom_b + bottom_ratio * h_b)


def kerb_ratios(road: RoadModel, kerbs: KerbModel) -> tuple[float | None, float | None]:
    """Measured ``(top, bottom)`` kerb width as a share of the road width, taken in the middle of the road; None if a side is missing."""
    x = (road.x_min + road.x_max) / 2.0
    width = road.bottom(x) - road.top(x)
    outer_top, outer_bottom = kerbs.top(x), kerbs.bottom(x)
    return (None if outer_top is None else (road.top(x) - outer_top) / width), (None if outer_bottom is None else (outer_bottom - road.bottom(x)) / width)


def suggest_lamp_side(top: float | None, bottom: float | None) -> str | None:
    """Which kerb carries the lamps: the narrower one (5/13 vs 6/13); with one side only, the one nearer 5/13."""
    if top is not None and bottom is not None:
        return "top" if top < bottom else "bottom"
    only, side, other = (top, "top", "bottom") if top is not None else (bottom, "bottom", "top")
    if only is None:
        return None
    return side if abs(only - KERB_RATIO_LAMP) < abs(only - KERB_RATIO_PLAIN) else other


# ================================ step 2: light of each lamp ================================


def downsample(gray: np.ndarray) -> np.ndarray:
    """Average CELL x CELL blocks: ``(FRAME_H, FRAME_W)`` -> ``(FRAME_H/CELL, FRAME_W/CELL)`` float32."""
    h, w = FRAME_H // CELL, FRAME_W // CELL
    return gray.astype(np.float32).reshape(h, CELL, w, CELL).mean(axis=(1, 3))


def maps_from_single_lamps(off: np.ndarray, singles: Sequence[np.ndarray]) -> np.ndarray:
    """Light map of lamp i = (frame with only lamp i on) - (all off), clipped at 0. Shape (4, h, w)."""
    base = downsample(off)
    return np.stack([np.clip(downsample(frame) - base, 0.0, None) for frame in singles])


def maps_from_regression(frames: Sequence[np.ndarray], levels: np.ndarray) -> np.ndarray:
    """Solve ``pixel = ambient + sum(gain_i * level_i)`` over frames with known lamp levels (0..1).

    ``levels`` has shape (N, LAMP_COUNT). Returns the gain maps (LAMP_COUNT, h, w), clipped at 0.
    """
    design = np.hstack([np.ones((len(frames), 1)), levels]).astype(np.float64)
    pixels = np.stack([downsample(f).ravel() for f in frames]).astype(np.float64)
    coef, *_ = np.linalg.lstsq(design, pixels, rcond=None)
    h, w = FRAME_H // CELL, FRAME_W // CELL
    return np.clip(coef[1:], 0.0, None).reshape(LAMP_COUNT, h, w).astype(np.float32)


# ================================ step 3: zones ================================


def road_mask_cells(road: RoadModel) -> np.ndarray:
    """Boolean (h, w) mask of the light-map cells whose centre is on the road (no margin)."""
    h, w = FRAME_H // CELL, FRAME_W // CELL
    ys, xs = np.mgrid[0:h, 0:w]
    px = xs * CELL + CELL / 2.0
    py = ys * CELL + CELL / 2.0
    top = road.top_a * px + road.top_b
    bottom = road.bottom_a * px + road.bottom_b
    inside = (px >= road.x_min) & (px <= road.x_max) & (py >= top) & (py <= bottom)
    if road.left_a is not None and road.left_b is not None:
        inside &= px >= road.left_a * py + road.left_b
    inside &= np.abs(py - (top + bottom) / 2.0) > DASH_HALF_PX  # lane dashes are white and would pose as a light pool
    return inside


def light_weights(gain: np.ndarray, mask: np.ndarray) -> np.ndarray:
    """Only the brightest part (above PEAK_FRACTION x peak) of one lamp's light on the road; all 0 if dark."""
    on_road = np.where(mask, gain, 0.0)
    return np.clip(on_road - PEAK_FRACTION * float(on_road.max()), 0.0, None)


def weighted_centre(weight: np.ndarray) -> tuple[float, float] | None:
    """Centre (x, y) in pixels of a weight map; None if it is empty."""
    total = float(weight.sum())
    if total <= 0.0:
        return None
    ys, xs = np.mgrid[0 : weight.shape[0], 0 : weight.shape[1]]
    cx = float((weight * (xs * CELL + CELL / 2.0)).sum() / total)
    cy = float((weight * (ys * CELL + CELL / 2.0)).sum() / total)
    return cx, cy


def build_zones(maps: np.ndarray, road: RoadModel, steps: StepSaver | None = None) -> ZoneModel | None:
    """Cut the road between the lamps' light centres; None if some lamp lights nothing on the road."""
    steps = steps or StepSaver(None)
    mask = road_mask_cells(road)
    steps.save("road_mask", Image.fromarray((mask * 255).astype(np.uint8)).resize((FRAME_W, FRAME_H), Image.NEAREST))
    peaks = [float(np.where(mask, maps[i], 0.0).max()) for i in range(LAMP_COUNT)]
    # A lamp whose light on the road is far weaker than the best lamp's has no pool to locate.
    weights = [light_weights(maps[i], mask) if peaks[i] >= MIN_REL_PEAK * max(peaks) else np.zeros_like(mask, dtype=np.float32) for i in range(LAMP_COUNT)]
    centres = [weighted_centre(w) for w in weights]
    steps.save("light_centres", draw_light_weights(weights, mask, centres))
    if any(c is None for c in centres):
        dark = [i + 1 for i, c in enumerate(centres) if c is None]
        LOG.error("lamp(s) %s light nothing on the road", dark)
        return None
    found = [c for c in centres if c is not None]
    order = sorted(range(LAMP_COUNT), key=lambda i: found[i][0])
    boundaries = tuple((found[order[k]][0] + found[order[k + 1]][0]) / 2.0 for k in range(LAMP_COUNT - 1))
    return ZoneModel(boundaries, tuple(order), tuple(found))


def vanishing_x(road: RoadModel) -> float | None:
    """x where the two road edges meet (the vanishing point), or None if they do not converge ahead."""
    slope_gap = road.top_a - road.bottom_a
    if abs(slope_gap) < 1e-6:
        return None
    x = (road.bottom_b - road.top_b) / slope_gap
    return x if x > road.x_max else None


def equal_zones(road: RoadModel) -> ZoneModel:
    """Method D: cut the road in LAMP_COUNT stretches of equal length on the ground, no lamp light needed.

    On a flat road running towards the vanishing point V, the image distance u = V - x is
    proportional to 1 / depth, so equal steps in depth are equal steps in 1 / u. Without a usable
    vanishing point the road is cut into equal stretches of image width instead.
    """
    vx = vanishing_x(road)
    start = road_start_x(road)
    if vx is None:
        step = (road.x_max - start) / LAMP_COUNT
        boundaries = tuple(start + step * k for k in range(1, LAMP_COUNT))
    else:
        near, far = 1.0 / (vx - start), 1.0 / (vx - road.x_max)
        boundaries = tuple(vx - 1.0 / (near + (far - near) * k / LAMP_COUNT) for k in range(1, LAMP_COUNT))
    return ZoneModel(boundaries, tuple(range(LAMP_COUNT)), (), "equal")


# ================================ step pictures ================================

COLOR_FALLING = (80, 160, 255)
COLOR_RISING = (255, 80, 80)
COLOR_SAMPLE = (0, 255, 0)
COLOR_LINE = (255, 255, 0)


class StepSaver:
    """Writes the picture of every processing step as ``steps/NN_name.png`` (does nothing without a folder)."""

    def __init__(self, folder: Path | None) -> None:
        self._folder = folder
        self._count = 0
        if folder is not None:
            folder.mkdir(parents=True, exist_ok=True)
            if any(folder.glob("*.png")):
                LOG.warning("%s already holds step images: older ones with a higher number may be stale", folder)

    def save(self, name: str, image: Image.Image) -> None:
        if self._folder is None:
            return
        self._count += 1
        path = self._folder / f"{self._count:02d}_{name}.png"
        image.save(path)
        LOG.info("step %02d  %s", self._count, path.name)


def frame_canvas(gray: np.ndarray) -> tuple[Image.Image, ImageDraw.ImageDraw]:
    """A colour copy of a frame, enlarged OVERLAY_SCALE times, ready to draw on."""
    s = OVERLAY_SCALE
    img = Image.fromarray(gray).convert("RGB").resize((FRAME_W * s, FRAME_H * s), Image.NEAREST)
    return img, ImageDraw.Draw(img)


def draw_dots(draw: ImageDraw.ImageDraw, points: Sequence[tuple[float, float]], color: tuple[int, int, int]) -> None:
    s = OVERLAY_SCALE
    for x, y in points:
        draw.ellipse([x * s - 2, y * s - 2, x * s + 2, y * s + 2], fill=color)


def draw_edge_candidates(lit: np.ndarray, edges: dict[int, tuple[list[int], list[int]]]) -> Image.Image:
    """Every edge found by the column scan: falling (bright -> dark) in blue, rising in red."""
    img, draw = frame_canvas(lit)
    draw_dots(draw, [(x, y) for x, (falling, _) in edges.items() for y in falling], COLOR_FALLING)
    draw_dots(draw, [(x, y) for x, (_, rising) in edges.items() for y in rising], COLOR_RISING)
    return img


def draw_samples(lit: np.ndarray, samples: Sequence[tuple[int, int, int]], lines: EdgeLines | None) -> Image.Image:
    """Road edge points of each column (green) and the lines fitted through them (yellow)."""
    img, draw = frame_canvas(lit)
    draw_dots(draw, [(x, top) for x, top, _ in samples] + [(x, bottom) for x, _, bottom in samples], COLOR_SAMPLE)
    if lines is not None:
        s = OVERLAY_SCALE
        for a, b in ((lines[0], lines[1]), (lines[2], lines[3])):
            draw.line([(0, b * s), (FRAME_W * s, (a * FRAME_W + b) * s)], fill=COLOR_LINE, width=1)
    return img


def draw_light_weights(weights: Sequence[np.ndarray], mask: np.ndarray, centres: Sequence[tuple[float, float] | None]) -> Image.Image:
    """Per lamp: the part of its light used for the centre (white), the road area (blue tint) and the centre (cross)."""
    h, w = mask.shape
    sheet = Image.new("RGB", (w * CELL * LAMP_COUNT, h * CELL))
    for i, weight in enumerate(weights):
        peak = float(weight.max()) or 1.0
        gray = (np.clip(weight / peak, 0.0, 1.0) * 255.0).astype(np.uint8)
        rgb = np.stack([gray, gray, np.maximum(gray, np.where(mask, 70, 0).astype(np.uint8))], axis=-1)
        tile = Image.fromarray(rgb).resize((w * CELL, h * CELL), Image.NEAREST)
        draw = ImageDraw.Draw(tile)
        centre = centres[i]
        if centre is not None:
            cx, cy = centre
            draw.line([(cx - 6, cy), (cx + 6, cy)], fill=(255, 40, 40), width=2)
            draw.line([(cx, cy - 6), (cx, cy + 6)], fill=(255, 40, 40), width=2)
        draw.text((4, 4), f"L{i + 1}", fill=(255, 255, 0))
        sheet.paste(tile, (i * w * CELL, 0))
    return sheet


def draw_frame_sheet(frames: Sequence[np.ndarray], names: Sequence[str]) -> Image.Image:
    """Captured frames side by side at half size, each with its name (live / from-dir modes)."""
    tile_w, tile_h = FRAME_W // 2, FRAME_H // 2
    sheet = Image.new("L", (tile_w * len(frames), tile_h))
    for i, (frame, name) in enumerate(zip(frames, names)):
        tile = Image.fromarray(frame).resize((tile_w, tile_h), Image.BILINEAR)
        ImageDraw.Draw(tile).text((4, 4), name, fill=255)
        sheet.paste(tile, (i * tile_w, 0))
    return sheet


# ================================ drawing ================================


def draw_overlay(lit: np.ndarray, road: RoadModel | None, zones: ZoneModel | None, kerbs: KerbModel | None = None, expected: KerbModel | None = None) -> Image.Image:
    """All-lamps frame with kerb outer edges (detected orange, expected from 6:13 / 5:13 cyan), road edges (green), zones (red), light centres (yellow)."""
    s = OVERLAY_SCALE
    img = Image.fromarray(lit).convert("RGB").resize((FRAME_W * s, FRAME_H * s), Image.NEAREST)
    draw = ImageDraw.Draw(img)
    if road is not None:
        for model, color in ((expected, (0, 220, 255)), (kerbs, (255, 140, 0))):
            for fn in ((model.top, model.bottom) if model is not None else ()):
                y0, y1 = fn(road.x_min), fn(road.x_max)
                if y0 is not None and y1 is not None:
                    draw.line([(road.x_min * s, y0 * s), (road.x_max * s, y1 * s)], fill=color, width=2)
    if road is not None:
        for fn in (road.top, road.bottom):
            draw.line([(road.x_min * s, fn(road.x_min) * s), (road.x_max * s, fn(road.x_max) * s)], fill=(0, 255, 0), width=2)
        draw.line([(road.x_max * s, road.top(road.x_max) * s), (road.x_max * s, road.bottom(road.x_max) * s)], fill=(0, 255, 0), width=2)
        (x0, y0), (x1, y1) = left_end_segment(road)
        draw.line([(x0 * s, y0 * s), (x1 * s, y1 * s)], fill=(0, 255, 0), width=2)
    if road is not None and zones is not None:
        edges = (road.x_min, *zones.boundaries_x, road.x_max)
        for x in zones.boundaries_x:
            draw.line([(x * s, road.top(x) * s), (x * s, road.bottom(x) * s)], fill=(255, 40, 40), width=2)
        for k in range(len(edges) - 1):
            mid = (edges[k] + edges[k + 1]) / 2.0
            label = f"Z{k + 1}" if zones.source == "equal" else f"Z{k + 1}:L{zones.lamp_of_zone[k] + 1}"
            draw.text((mid * s - 10, (road.top(mid) + road.bottom(mid)) / 2.0 * s - 28), label, fill=(255, 255, 0))
        for i, (cx, cy) in enumerate(zones.centres):
            draw.ellipse([cx * s - 4, cy * s - 4, cx * s + 4, cy * s + 4], outline=(255, 255, 0), width=2)
            draw.text((cx * s + 6, cy * s - 6), f"L{i + 1}", fill=(255, 255, 0))
    return img


def draw_lamp_maps(maps: np.ndarray) -> Image.Image:
    """The four light maps side by side, every map scaled to its own peak, so weak lamps stay visible."""
    h, w = maps.shape[1:]
    tiles = []
    for gain in maps:
        peak = float(gain.max()) or 1.0
        tile = Image.fromarray((np.clip(gain / peak, 0.0, 1.0) * 255.0).astype(np.uint8)).resize((w * CELL, h * CELL), Image.NEAREST)
        tiles.append(tile)
    sheet = Image.new("L", (w * CELL * LAMP_COUNT, h * CELL))
    for i, tile in enumerate(tiles):
        sheet.paste(tile, (i * w * CELL, 0))
    return sheet


# ================================ offline source ================================


def collect_session_frames(session: Path) -> tuple[list[np.ndarray], np.ndarray]:
    """``nen`` frames of a dataset session and their lamp levels (0..1) from index.csv."""
    frames: list[np.ndarray] = []
    levels: list[list[float]] = []
    with (session / "index.csv").open(newline="", encoding="utf-8") as fh:
        for row in csv.DictReader(fh):
            path = session / row["file"]
            if row["label"] != "nen" or not path.exists():
                continue
            frames.append(load_gray(path))
            levels.append([int(row[f"lamp{i}"]) / 100.0 for i in range(1, LAMP_COUNT + 1)])
    if len(frames) < LAMP_COUNT + 2:
        raise SystemExit(f"{session}: need at least {LAMP_COUNT + 2} 'nen' frames, found {len(frames)}")
    return frames, np.asarray(levels)


def pick_lit_frame(frames: Sequence[np.ndarray], levels: np.ndarray) -> np.ndarray:
    """The frame with the most light on (highest summed level) = the 'all lamps max' frame."""
    return frames[int(np.argmax(levels.sum(axis=1)))]


# ================================ live source ================================


class Board:
    """Minimal client of the ai_stream web server."""

    def __init__(self, host: str) -> None:
        self._base = f"http://{host}"

    def _post(self, path: str, body: dict[str, object]) -> None:
        req = urllib.request.Request(self._base + path, data=json.dumps(body).encode(), headers={"Content-Type": "application/json"}, method="POST")
        with urllib.request.urlopen(req, timeout=HTTP_TIMEOUT_S):  # noqa: S310 (fixed http board URL)
            pass

    def set_lamp(self, lamp: int, pct: int) -> None:
        """``lamp`` 0 = every lamp, 1..4 = that lamp."""
        self._post("/api/lamp", {"lamp": lamp, "pct": pct})

    def lock_exposure(self, lock: bool) -> None:
        self._post("/api/exposure", {"lock": lock})

    def frame(self) -> tuple[np.ndarray, int]:
        """``(gray frame, sequence number)`` of the newest frame."""
        with urllib.request.urlopen(self._base + "/frame.jpg", timeout=HTTP_TIMEOUT_S) as resp:  # noqa: S310
            seq = int(resp.headers.get("X-Info", "0").split(",")[0])
            return to_frame(Image.open(io.BytesIO(resp.read()))), seq

    def fresh_frame(self) -> np.ndarray:
        """Frame taken after the lamps changed: wait for a few new sequence numbers first."""
        time.sleep(LIVE_SETTLE_S)
        _, first = self.frame()
        while True:
            frame, seq = self.frame()
            if seq - first >= LIVE_FRESH_FRAMES:
                return frame
            time.sleep(0.1)


def capture_live(host: str, frames_dir: Path) -> tuple[np.ndarray, np.ndarray, list[np.ndarray]]:
    """Run the boot sequence on the board: all max, all off, then each lamp alone at 100 %."""
    board = Board(host)
    board.set_lamp(0, 100)
    lit = board.fresh_frame()
    board.lock_exposure(True)  # same exposure for every frame, so the differences are only the lamps
    try:
        board.set_lamp(0, 0)
        off = board.fresh_frame()
        singles = []
        for lamp in range(1, LAMP_COUNT + 1):
            board.set_lamp(0, 0)
            board.set_lamp(lamp, 100)
            singles.append(board.fresh_frame())
            LOG.info("lamp %d captured", lamp)
    finally:
        board.set_lamp(0, 0)
        board.lock_exposure(False)
    frames_dir.mkdir(parents=True, exist_ok=True)
    Image.fromarray(lit).save(frames_dir / "lit.png")
    Image.fromarray(off).save(frames_dir / "off.png")
    for i, frame in enumerate(singles, start=1):
        Image.fromarray(frame).save(frames_dir / f"lamp{i}.png")
    return lit, off, singles


def load_from_dir(frames_dir: Path) -> tuple[np.ndarray, np.ndarray, list[np.ndarray]]:
    """Reload frames saved by a live run (lit.png, off.png, lamp1..4.png)."""
    singles = [load_gray(frames_dir / f"lamp{i}.png") for i in range(1, LAMP_COUNT + 1)]
    return load_gray(frames_dir / "lit.png"), load_gray(frames_dir / "off.png"), singles


# ================================ output ================================


def save_results(out: Path, lit: np.ndarray, maps: np.ndarray | None, road: RoadModel | None, zones: ZoneModel | None, probes: Sequence[tuple[float, float]], kerbs: KerbModel | None = None, expected: KerbModel | None = None) -> None:
    """Write overlay.png, lamp_maps.png and calib.json."""
    out.mkdir(parents=True, exist_ok=True)
    draw_overlay(lit, road, zones, kerbs, expected).save(out / "overlay.png")
    if maps is not None:
        draw_lamp_maps(maps).save(out / "lamp_maps.png")
    result: dict[str, object] = {"frame": [FRAME_W, FRAME_H], "road": asdict(road) if road else None, "kerbs": asdict(kerbs) if kerbs else None, "kerbs_expected": asdict(expected) if expected else None, "zones": asdict(zones) if zones else None}
    if road and zones:
        result["probes"] = [{"x": x, "y": y, "on_road": road.contains(x, y), "zone": zones.zone_of(x) + 1 if road.contains(x, y) else None} for x, y in probes]
    (out / "calib.json").write_text(json.dumps(result, indent=2), encoding="utf-8")
    LOG.info("wrote %s", out)


def report(road: RoadModel | None, zones: ZoneModel | None, kerbs: KerbModel | None = None, expected: KerbModel | None = None, lamp_side: str = "bottom") -> None:
    """Log the result in a form that is quick to check by eye."""
    if road is None:
        LOG.error("road NOT found (check the all-lamps frame: kerbs must be brighter than the road)")
        return
    LOG.info("road: x %d..%d  top y=%.3fx%+.1f  bottom y=%.3fx%+.1f", road.x_min, road.x_max, road.top_a, road.top_b, road.bottom_a, road.bottom_b)
    if expected is not None:
        want_top, want_bottom = kerb_ratios(road, expected)
        LOG.info("kerb / road width expected (lamps on %s): top %.2f  bottom %.2f", lamp_side, want_top, want_bottom)
    if kerbs is not None:
        top_ratio, bottom_ratio = kerb_ratios(road, kerbs)
        show = lambda value: "not found" if value is None else f"{value:.2f}"  # noqa: E731
        LOG.info("kerb / road width measured: top %s  bottom %s", show(top_ratio), show(bottom_ratio))
        LOG.info("lamp side suggested by the measured ratios: %s", suggest_lamp_side(top_ratio, bottom_ratio))
    else:
        LOG.warning("kerbs NOT found by edges (road is still valid): use the cyan expected edges")
    if zones is None:
        return
    LOG.info("zone boundaries x = %s", ", ".join(f"{b:.0f}" for b in zones.boundaries_x))
    LOG.info("zone -> lamp: %s", ", ".join(f"Z{k + 1}=L{lamp + 1}" for k, lamp in enumerate(zones.lamp_of_zone)))


def parse_probe(text: str) -> tuple[float, float]:
    """``"x,y"`` in frame pixels."""
    x, y = text.split(",")
    return float(x), float(y)


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("-o", "--out", type=Path, default=Path("calib_out"), help="output folder")
    parser.add_argument("--probe", action="append", type=parse_probe, default=[], metavar="X,Y", help="object centre (pixels) to test: on the road? which zone? (repeatable)")
    parser.add_argument("--lamp-side", choices=LAMP_SIDES, default="bottom", help="which kerb in the image carries the lamps (5/13 of the road width; the other is 6/13)")
    sub = parser.add_subparsers(dest="mode", required=True)
    image = sub.add_parser("image", help="one frame (PGM/PNG/JPEG): road, kerbs and equal-split zones only")
    image.add_argument("image", type=Path)
    off = sub.add_parser("offline", help="use 'nen' frames of a dataset session")
    off.add_argument("session", type=Path)
    live = sub.add_parser("live", help="drive the board lamps and grab frames")
    live.add_argument("--host", default="192.168.4.1")
    from_dir = sub.add_parser("from-dir", help="re-run on frames saved by a live run")
    from_dir.add_argument("frames", type=Path)
    return parser


def run(args: argparse.Namespace) -> int:
    steps = StepSaver(args.out / "steps")
    maps: np.ndarray | None = None  # stays None in "image" mode: one frame carries no per-lamp light
    if args.mode == "image":
        lit = load_gray(args.image)
        steps.save("lit_frame", draw_frame_sheet([lit], ["input"]))
    elif args.mode == "offline":
        frames, levels = collect_session_frames(args.session)
        lit = pick_lit_frame(frames, levels)
        steps.save("lit_frame", draw_frame_sheet([lit], ["all lamps"]))
        maps = maps_from_regression(frames, levels)
    else:
        if args.mode == "live":
            lit, off, singles = capture_live(args.host, args.out / "frames")
        else:
            lit, off, singles = load_from_dir(args.frames)
        steps.save("captured_frames", draw_frame_sheet([lit, off, *singles], ["all on", "all off", "L1", "L2", "L3", "L4"]))
        maps = maps_from_single_lamps(off, singles)
    if maps is not None:
        steps.save("lamp_maps", draw_lamp_maps(maps))
    road = find_road(lit, steps)
    kerbs = expected = None
    if road is not None:
        steps.save("road", draw_overlay(lit, road, None))
        kerbs = find_kerbs(lit, road, steps)
        expected = kerbs_from_ratio(road, args.lamp_side)
        steps.save("road_and_kerbs", draw_overlay(lit, road, None, kerbs, expected))
    zones = build_zones(maps, road, steps) if road is not None and maps is not None else None
    if road is not None and zones is None:
        LOG.warning("lamps not located from their light (or single image) -> equal split along the road (method D)")
        zones = equal_zones(road)
    steps.save("zones", draw_overlay(lit, road, zones, kerbs, expected))
    report(road, zones, kerbs, expected, args.lamp_side)
    save_results(args.out, lit, maps, road, zones, args.probe, kerbs, expected)
    return 0 if road and zones else 1


def main() -> int:
    logging.basicConfig(level=logging.INFO, format="%(message)s", stream=sys.stdout)
    return run(build_parser().parse_args())


if __name__ == "__main__":
    sys.exit(main())
