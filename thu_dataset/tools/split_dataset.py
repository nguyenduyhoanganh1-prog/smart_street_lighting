"""Split the frames pulled by pull_dataset.py into train / val / test without leakage.

Frames of one object position differ only by the lamp levels, so they are near-duplicates. The
split is therefore done per *position* (a run of frames of one label with no pause of GAP_S
seconds), never per frame: all frames of a position land in the same set.

Classes come from CLASS_RULES below, which was filled in by looking at the pictures (the file
label alone is not enough: "nguoi" frames from 1651 on also contain cars). Frames that are not
usable go to ``khong_dat/<reason>/`` instead of a set.

Output (PNG, originals are only read)::

    <out>/train|val|test/car|person|car_person|person_mixed|background/car.p008_0001.png
    <out>/khong_dat/<reason>/p008_xe.0045.png
    <out>/manifest.csv      one line per source frame: where it went and why

Usage::

    python split_dataset.py E:/TKHTN/dataset -o E:/TKHTN/dataset_split
"""
from __future__ import annotations

import argparse
import csv
import random
import sys
from collections import Counter, defaultdict
from dataclasses import dataclass
from pathlib import Path

import numpy as np
from PIL import Image

GAP_S = 5                # a pause this long between two frames starts a new position
VAL_FRACTION = 0.15
TEST_FRACTION = 0.15
SEED = 20261113
MIN_MEAN = 26.0          # darker than this: lamps off, the objects cannot be seen
MAX_LINE_GLITCH = 20.0   # a full-width row this far from its neighbours is a camera sync error
FRAME_SIZE = (320, 240)  # every output image has this size
# "person_mixed": frames labelled nguoi where some also show cars; sort them by hand
CLASSES = ("car", "person", "car_person", "person_mixed", "background")
# Early sessions at 640x360 with another camera angle. Their frames are scaled to FRAME_SIZE and
# used for training only, so val/test measure the final camera setup.
TRAIN_ONLY_SESSIONS = ("phien_001", "phien_002")
SETS = ("train", "val", "test")

REJECT_UNKNOWN = "chua_phan_lop"            # no rule in CLASS_RULES covers this frame yet
REJECT_LABEL = "nhan_khong_tin_cay"         # early tests: the labelled object is not in the picture
REJECT_CAMERA_MOVED = "camera_bi_xe_dich"   # the camera was being moved during the batch
REJECT_DARK = "qua_toi"
REJECT_GLITCH = "soc_ngang"


@dataclass(frozen=True)
class Rule:
    """Frames ``first..last`` (sequence numbers) of ``label`` in ``session`` -> class or reject reason."""

    session: str
    label: str
    first: int
    last: int
    target: str


# Checked by eye on contact sheets (one row per run of positions). Anything not listed is rejected.
CLASS_RULES = (
    Rule("phien_001", "nen", 1, 9999, "background"),
    Rule("phien_001", "xe", 1, 9999, "car"),
    Rule("phien_002", "nen", 1, 9999, "background"),
    Rule("phien_002", "xe", 1, 9999, "car"),
    Rule("phien_002", "nguoi", 1, 9999, "person_mixed"),
    Rule("phien_003", "nen", 1, 9999, "background"),
    Rule("phien_004", "nen", 1, 9999, "background"),
    Rule("phien_005", "nen", 1, 9999, "background"),
    Rule("phien_003", "nguoi", 1, 9999, REJECT_LABEL),
    Rule("phien_004", "nguoi", 1, 9999, REJECT_LABEL),
    Rule("phien_005", "nguoi", 1, 9999, REJECT_LABEL),
    Rule("phien_006", "xe", 1, 9999, "car"),
    Rule("phien_007", "xe", 1, 9999, "car"),
    Rule("phien_007", "nguoi", 1, 9999, "car"),          # board rebooted, label fell back to nguoi
    Rule("phien_008", "xe", 1, 1080, "car"),
    Rule("phien_008", "nguoi", 1081, 1650, "person"),
    Rule("phien_008", "nguoi", 1651, 2130, "car_person"),
    Rule("phien_008", "nen", 2131, 2160, REJECT_CAMERA_MOVED),
    Rule("phien_008", "nen", 2161, 2220, "background"),
)


@dataclass(frozen=True)
class Item:
    """One source frame."""

    path: Path
    session: str
    label: str
    seq: int
    epoch: int
    lamps: str


def load_session(folder: Path) -> list[Item]:
    """Read the frames of one session in capture order (index.csv, or file time for old sessions)."""
    index = folder / "index.csv"
    items: list[Item] = []
    if index.exists():
        with index.open(encoding="utf-8") as f:
            for row in csv.DictReader(f):
                path = folder / row["file"]
                if path.exists():
                    lamps = "-".join(row[f"lamp{i}"] for i in range(1, 5))
                    items.append(Item(path, folder.name, row["label"], int(row["file"].split(".")[1]),
                                      int(row["epoch"]), lamps))
        return items
    for path in sorted(folder.glob("*.png"), key=lambda p: (p.stat().st_mtime, p.name)):
        label, seq = path.name.split(".")[:2]
        items.append(Item(path, folder.name, label, int(seq), int(path.stat().st_mtime), ""))
    return items


def split_positions(items: list[Item]) -> list[list[Item]]:
    """Cut a session into positions: a new one starts after a pause or when the label changes."""
    positions: list[list[Item]] = []
    for item in items:
        last = positions[-1][-1] if positions else None
        if last is None or item.epoch - last.epoch >= GAP_S or item.label != last.label:
            positions.append([])
        positions[-1].append(item)
    return positions


def target_of(item: Item) -> str:
    """Class of a frame, or the reason it is rejected."""
    for rule in CLASS_RULES:
        if rule.session == item.session and rule.label == item.label and rule.first <= item.seq <= rule.last:
            return rule.target
    return REJECT_UNKNOWN


def quality_problem(gray: np.ndarray) -> str | None:
    """Return the reject reason of a frame that is too dark or has a line glitch, else None."""
    pixels = gray.astype(np.float32)
    if pixels.mean() < MIN_MEAN:
        return REJECT_DARK
    # A sync error shows as a full-width row unlike both neighbours; real edges are slanted
    rows = np.median(np.abs(pixels[1:-1] - (pixels[:-2] + pixels[2:]) / 2), axis=1)
    return REJECT_GLITCH if rows.max() > MAX_LINE_GLITCH else None


def fit_frame(image: Image.Image) -> Image.Image:
    """Return the image at FRAME_SIZE: scaled to the frame width, black bars above and below.

    The old 640x360 frames are the middle rows of the sensor at twice the scale, so halving them
    keeps the objects the same shape and size as in the 320x240 frames (no stretching).
    """
    if image.size == FRAME_SIZE:
        return image
    height = round(image.height * FRAME_SIZE[0] / image.width)
    canvas = Image.new("L", FRAME_SIZE, 0)
    canvas.paste(image.resize((FRAME_SIZE[0], height), Image.LANCZOS), (0, (FRAME_SIZE[1] - height) // 2))
    return canvas


def assign_sets(positions_by_class: dict[str, list[str]], rng: random.Random) -> dict[str, str]:
    """Give every position id a set, per class, so each class is spread over train/val/test."""
    assigned: dict[str, str] = {}
    for cls in CLASSES:
        ids = sorted(positions_by_class.get(cls, []))
        for pid in ids:
            if pid.split(":")[0] in TRAIN_ONLY_SESSIONS:
                assigned[pid] = "train"
        ids = [pid for pid in ids if pid not in assigned]
        rng.shuffle(ids)
        n_val = max(1, round(len(ids) * VAL_FRACTION)) if len(ids) >= 3 else 0
        n_test = max(1, round(len(ids) * TEST_FRACTION)) if len(ids) >= 2 else 0
        for i, pid in enumerate(ids):
            assigned[pid] = "test" if i < n_test else "val" if i < n_test + n_val else "train"
    return assigned


def main(argv: list[str]) -> int:
    """Split the dataset. Returns the process exit code."""
    p = argparse.ArgumentParser(description="Split thu_dataset frames into train/val/test by position.")
    p.add_argument("dataset", type=Path, help="folder with the phien_NNN sessions")
    p.add_argument("-o", "--out", type=Path, required=True, help="new folder for the split (must not exist)")
    args = p.parse_args(argv)
    if args.out.exists():
        print(f"{args.out} already exists: choose a new folder so nothing is overwritten.")
        return 1

    # 1) positions and their class
    usable: dict[str, list[Item]] = {}        # position id -> frames
    position_class: dict[str, str] = {}
    rejected: list[tuple[Item, str]] = []
    for folder in sorted(args.dataset.glob("phien_*")):
        for frames in split_positions(load_session(folder)):
            target = target_of(frames[0])
            if target not in CLASSES:
                rejected += [(item, target) for item in frames]
                continue
            pid = f"{frames[0].session}:{frames[0].label}.{frames[0].seq:04d}"
            usable[pid] = frames
            position_class[pid] = target

    by_class: dict[str, list[str]] = defaultdict(list)
    for pid, cls in position_class.items():
        by_class[cls].append(pid)
    set_of = assign_sets(by_class, random.Random(SEED))

    # 2) copy as PNG, frame-level quality check
    args.out.mkdir(parents=True)
    counts: Counter[tuple[str, str]] = Counter()
    with (args.out / "manifest.csv").open("w", newline="", encoding="utf-8") as f:
        log = csv.writer(f)
        log.writerow(["source", "class", "position", "set", "reject_reason", "lamps", "output"])

        def reject(item: Item, reason: str, cls: str = "", pid: str = "") -> None:
            dest = args.out / "khong_dat" / reason / f"p{item.session[-3:]}_{item.path.stem}.png"
            dest.parent.mkdir(parents=True, exist_ok=True)
            fit_frame(Image.open(item.path).convert("L")).save(dest)
            log.writerow([item.path, cls, pid, "", reason, item.lamps, dest.relative_to(args.out)])
            counts[("khong_dat", reason)] += 1

        for item, reason in rejected:
            reject(item, reason)
        for pid, frames in usable.items():
            cls, subset = position_class[pid], set_of[pid]
            for item in frames:
                image = Image.open(item.path).convert("L")
                problem = quality_problem(np.asarray(image))
                if problem is not None:
                    reject(item, problem, cls, pid)
                    continue
                dest = args.out / subset / cls / f"{cls}.p{item.session[-3:]}_{item.seq:04d}.png"
                dest.parent.mkdir(parents=True, exist_ok=True)
                fit_frame(image).save(dest)
                log.writerow([item.path, cls, pid, subset, "", item.lamps, dest.relative_to(args.out)])
                counts[(subset, cls)] += 1

    # 3) report
    print(f"{'':12}" + "".join(f"{s:>8}" for s in SETS) + "   positions (train/val/test)")
    for cls in CLASSES:
        pos = Counter(set_of[pid] for pid in by_class.get(cls, []))
        print(f"{cls:12}" + "".join(f"{counts[(s, cls)]:8d}" for s in SETS)
              + f"   {pos['train']}/{pos['val']}/{pos['test']}")
    print("khong_dat:", ", ".join(f"{r}={n}" for (s, r), n in sorted(counts.items()) if s == "khong_dat"))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
