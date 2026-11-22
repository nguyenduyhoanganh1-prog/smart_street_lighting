"""Check that the C port (firmware/components/road_zones/road_zones.c) finds the same road and zones as zone_calib.py.

Builds test/host/test_road_zones (sources: firmware/components/road_zones) with gcc, runs both versions on every given frame and compares:
road edges at both ends, x range, left end, zone boundaries, outer kerb edges. The C code works in single precision
and the Python prototype in double, so small differences are expected; the tolerances below are
"same result for the lamps", not "same bits".

Usage::

    python check_c_port.py E:/TKHTN/dataset/phien_008/nen.2151.pgm E:/TKHTN/dataset/phien_002/nen.0661.png

Exit code 0 = every frame matches.
"""
from __future__ import annotations

import argparse
import subprocess
import sys
import tempfile
from pathlib import Path

import numpy as np
from PIL import Image

import zone_calib as zc

HOST_DIR = Path(__file__).resolve().parent.parent / "test" / "host"
RZ_DIR = HOST_DIR.parent.parent.parent / "firmware" / "components" / "road_zones"
EDGE_TOL_PX = 2.0      # road edge y at the road ends
RANGE_TOL_PX = 4       # x_min / x_max
BOUNDARY_TOL_PX = 3.0  # zone boundaries


def build_host_test(out_dir: Path) -> Path:
    """Compile the host test with the same strict warnings as the firmware; return the executable."""
    exe = out_dir / "test_road_zones.exe"
    cmd = ["gcc", "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror", f"-I{RZ_DIR / 'include'}",
           str(HOST_DIR / "test_road_zones.c"), str(RZ_DIR / "road_zones.c"), "-lm", "-o", str(exe)]
    subprocess.run(cmd, check=True)
    return exe


def run_c(exe: Path, frame: np.ndarray, pgm: Path) -> dict[str, float]:
    """Write the frame as PGM, run the C calibration on it, parse its key=value line."""
    Image.fromarray(frame).save(pgm)
    line = subprocess.run([str(exe), str(pgm)], check=True, capture_output=True, text=True).stdout.strip()
    return {key: float(value) for key, value in (item.split("=") for item in line.split())}


def kerb_checks(frame: np.ndarray, road: zc.RoadModel, c: dict[str, float]) -> list[tuple[str, float, float, float]]:
    """Outer kerb edges: found on the same sides, and at the same rows at both road ends."""
    kerbs = zc.find_kerbs(frame, road)
    checks: list[tuple[str, float, float, float]] = []
    for side, key in (("top", "kt"), ("bottom", "kb")):
        edge = getattr(kerbs, side) if kerbs is not None else None
        py_has = edge is not None and edge(0.0) is not None
        checks.append((f"has kerb {side}", float(py_has), c[f"has_{key}"], 0.0))
        if py_has and c[f"has_{key}"] == 1:
            for x in (road.x_min, road.x_max):
                checks.append((f"kerb {side} @x={x}", edge(x), c[f"{key}_a"] * x + c[f"{key}_b"], EDGE_TOL_PX))
    return checks


def compare(name: str, frame: np.ndarray, c: dict[str, float]) -> bool:
    """Print C vs Python for one frame; True if they agree within the tolerances."""
    road = zc.find_road(frame)
    if road is None or c["valid"] == 0:
        same = road is None and c["valid"] == 0
        print(f"{name}: python {'no road' if road is None else 'road'}, C {'no road' if c['valid'] == 0 else 'road'} -> {'OK' if same else 'MISMATCH'}")
        return same
    zones = zc.equal_zones(road)
    checks: list[tuple[str, float, float, float]] = []
    for x in (road.x_min, road.x_max):
        checks.append((f"top y @x={x}", road.top(x), c["top_a"] * x + c["top_b"], EDGE_TOL_PX))
        checks.append((f"bottom y @x={x}", road.bottom(x), c["bottom_a"] * x + c["bottom_b"], EDGE_TOL_PX))
    checks.append(("x_min", road.x_min, c["x_min"], RANGE_TOL_PX))
    checks.append(("x_max", road.x_max, c["x_max"], RANGE_TOL_PX))
    checks.append(("has left end", float(road.left_a is not None), c["has_left"], 0.0))
    for k, boundary in enumerate(zones.boundaries_x, start=1):
        checks.append((f"boundary {k}", boundary, c[f"b{k}"], BOUNDARY_TOL_PX))
    checks.extend(kerb_checks(frame, road, c))
    ok = True
    print(f"{name}:")
    for label, py, cv, tol in checks:
        good = abs(py - cv) <= tol
        ok &= good
        print(f"   {label:<18} python {py:8.2f}   C {cv:8.2f}   {'ok' if good else 'MISMATCH'}")
    return ok


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("frames", nargs="+", type=Path, help="frames (PGM/PNG/JPEG), resized to 320x240 like the tool does")
    args = parser.parse_args()
    with tempfile.TemporaryDirectory() as tmp:
        tmp_dir = Path(tmp)
        exe = build_host_test(tmp_dir)
        results = [compare(path.name, frame, run_c(exe, frame, tmp_dir / "frame.pgm"))
                   for path in args.frames for frame in [zc.load_gray(path)]]
    print(f"\n{sum(results)}/{len(results)} frame(s) match")
    return 0 if all(results) else 1


if __name__ == "__main__":
    sys.exit(main())
