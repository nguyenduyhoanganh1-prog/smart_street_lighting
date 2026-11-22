"""Save dataset frames from the thu_dataset board on the PC, over WiFi, instead of on the SD card.

The board's SD card gives write errors (CRC on the SDMMC bus), so this script downloads every
frame from ``http://192.168.4.1/frame.pgm`` and writes the same layout the firmware writes on
the card::

    <out>/phien_008/session.json
    <out>/phien_008/index.csv          file,label,lamp1,lamp2,lamp3,lamp4,exposure_locked,epoch
    <out>/phien_008/xe.0001.pgm        320x240 8-bit grayscale, binary PGM (P5)

so convert_raw.py and the rest of the pipeline work unchanged.

Default mode, keys in this terminal:
    c / p / n   label = xe (car) / nguoi (person) / nen (background)
    s           run one batch: ``--combos`` random lamp combinations (default 30, every lamp
                at most 70 %), one frame each; it beeps when done -> move the object, press s
    q           quit

The label lives in this script, not on the board: the board forgets its label when it reboots.
Every batch contains all the combinations the real control rules produce (night base, person,
vehicle, all-on fallback, off); the rest are variations (mid-fade, a dead lamp, free mixes),
so the model sees every real lighting case at every object position, plus the unusual ones.
When the WiFi drops (board reboot, laptop re-joining the AP) the script retries for a while
instead of skipping the combination.

Usage (laptop joined to the WiFi "StreetLight-Dataset")::

    python pull_dataset.py --label xe -o E:/TKHTN/dataset
    python pull_dataset.py --combos 20 --settle 1.5
    python pull_dataset.py --period 2 --count 200             # plain timer, lamps untouched
"""
from __future__ import annotations

import argparse
import dataclasses
import json
import random
import re
import sys
import time
import urllib.error
import urllib.request
from dataclasses import dataclass
from pathlib import Path
from typing import Callable, TypeVar

try:
    import msvcrt  # Windows: read single key presses without Enter
except ImportError:
    msvcrt = None

T = TypeVar("T")

DEFAULT_HOST = "192.168.4.1"
DEFAULT_OUT = Path("dataset")
DEFAULT_COMBOS = 30
DEFAULT_SETTLE_S = 1.0  # camera frame buffers + auto exposure need a moment after a lamp change
HTTP_TIMEOUT_S = 10.0
RETRY_WINDOW_S = 30.0   # long enough for a board reboot + the laptop re-joining the AP
RETRY_PAUSE_S = 2.0
NET_ERRORS = (urllib.error.URLError, ValueError, OSError)
FRAME_WIDTH = 320
FRAME_HEIGHT = 240
LAMP_COUNT = 4
LABELS = ("nguoi", "xe", "nen")
LABEL_KEYS = {"c": "xe", "p": "nguoi", "n": "nen"}  # c = car, p = person, n = nothing
KEY_SAVE = "s"
KEY_QUIT = "q"
KEY_CTRL_C = "\x03"  # msvcrt returns Ctrl+C as a character instead of raising KeyboardInterrupt
SESSION_RE = re.compile(r"^phien_(\d{3,4})$")
PGM_HEADER = f"P5\n{FRAME_WIDTH} {FRAME_HEIGHT}\n255\n".encode("ascii")
INDEX_HEADER = "file,label,lamp1,lamp2,lamp3,lamp4,exposure_locked,epoch\n"

# Lamp levels of the dataset: the control rules use 20 % at night and 100 % near an object,
# capped here at 70 % (brightest level wanted for these photos).
BASE_PCT = 20
MAX_PCT = 70
STEP_PCT = 5
MAX_ATTEMPTS_PER_COMBO = 50  # retries to find a combination not yet used in the batch

# Every batch holds all the combinations of the control rules (see rule_combos(): night base,
# person, vehicle, fallback, off; about 10 of them). The rest of the batch is drawn from these
# variations, with these relative weights:
EXTRA_WEIGHTS = {
    "chuyen_tiep": 3,  # caught in the middle of a fade between base and bright
    "bong_hong": 3,    # fault: a person/vehicle scene with one dead lamp
    "ngau_nhien": 2,   # any mix, also ones the rules never produce
}


@dataclass(frozen=True)
class Frame:
    """One frame downloaded from the board, with the facts index.csv needs."""

    pgm: bytes
    label: str
    lamps: tuple[int, ...]
    exposure_locked: bool


# ================================== Board access ==================================


def http_json(url: str, body: dict | None = None) -> dict:
    """GET (body None) or POST a JSON request and return the decoded JSON reply.

    Raises:
        urllib.error.URLError: The board cannot be reached or refused the request.
        ValueError: The reply is not JSON.
    """
    data = None if body is None else json.dumps(body).encode("utf-8")
    req = urllib.request.Request(url, data=data, headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=HTTP_TIMEOUT_S) as resp:
        return json.loads(resp.read().decode("utf-8"))


def set_lamps(host: str, levels: tuple[int, ...]) -> tuple[int, ...]:
    """Set the four lamps to ``levels`` (percent, lamp 1 first); the board applies them instantly.

    Returns:
        ``levels``, so retry() can tell success from failure.
    """
    for lamp, pct in enumerate(levels, start=1):
        http_json(f"http://{host}/api/lamp", {"lamp": lamp, "pct": pct})
    return levels


def push_label(host: str, label: str) -> None:
    """Show the label on the board's web page. Best effort: file names use the script's label."""
    try:
        http_json(f"http://{host}/api/label", {"label": label})
    except NET_ERRORS:
        pass


def fetch_frame(host: str) -> Frame:
    """Download one frame and check that it is a complete 320x240 PGM.

    Raises:
        urllib.error.URLError: Network error or HTTP error (e.g. 503 camera busy).
        ValueError: The board sent something that is not the expected frame.
    """
    with urllib.request.urlopen(f"http://{host}/frame.pgm", timeout=HTTP_TIMEOUT_S) as resp:
        pgm = resp.read()
        label = resp.headers.get("X-Label", "")
        lamps_text = resp.headers.get("X-Lamps", "")
        locked = resp.headers.get("X-Exposure-Locked", "0") == "1"
    if not pgm.startswith(PGM_HEADER) or len(pgm) != len(PGM_HEADER) + FRAME_WIDTH * FRAME_HEIGHT:
        raise ValueError(f"unexpected frame ({len(pgm)} bytes)")
    lamps = tuple(int(x) for x in lamps_text.split(","))
    if len(lamps) != LAMP_COUNT:
        raise ValueError(f"unexpected lamp list {lamps_text!r}")
    return Frame(pgm=pgm, label=label, lamps=lamps, exposure_locked=locked)


def retry(action: Callable[[], T], what: str) -> T | None:
    """Run ``action``; on a network error keep trying for RETRY_WINDOW_S.

    The WiFi link drops when the board reboots or the laptop re-joins the AP; waiting keeps the
    batch complete instead of skipping combinations.

    Returns:
        The action's result, or None when it still fails after RETRY_WINDOW_S.
    """
    deadline = time.monotonic() + RETRY_WINDOW_S
    while True:
        try:
            return action()
        except NET_ERRORS as e:
            if time.monotonic() >= deadline:
                print(f"  {what} failed, skipped: {e}")
                return None
            print(f"  {what}: no connection ({e}), retrying in {RETRY_PAUSE_S:.0f} s...")
            time.sleep(RETRY_PAUSE_S)


# ================================ Lamp combinations ================================


def bright_at(zones: set[int]) -> tuple[int, ...]:
    """Lamps in ``zones`` at MAX_PCT, the others at the night base. Zones off the road are ignored."""
    return tuple(MAX_PCT if i in zones else BASE_PCT for i in range(LAMP_COUNT))


def rule_combos() -> list[tuple[str, tuple[int, ...]]]:
    """Every distinct combination the control rules (plans/01) produce, bright level capped.

    Person in zone K -> lamps K-1..K+1; vehicle in zone K -> K and the next zone ahead (either
    driving direction). At the road ends some of these coincide, so only distinct ones are kept.
    """
    combos = {
        bright_at(set()): "nen_dem",              # night, nothing on the road
        (MAX_PCT,) * LAMP_COUNT: "du_phong",       # fault: camera/sensor broken -> all bright
        (0,) * LAMP_COUNT: "tat",                  # daytime: all off
    }
    for k in range(LAMP_COUNT):
        combos.setdefault(bright_at({k - 1, k, k + 1}), "nguoi")
        for ahead in (-1, 1):
            combos.setdefault(bright_at({k, k + ahead}), "xe")
    return [(kind, levels) for levels, kind in combos.items()]


def make_extra(rng: random.Random) -> tuple[str, tuple[int, ...]]:
    """Draw one variation (see EXTRA_WEIGHTS). Returns its kind and the four levels."""
    kind = rng.choices(list(EXTRA_WEIGHTS), weights=list(EXTRA_WEIGHTS.values()))[0]
    if kind == "bong_hong":
        scenes = [levels for k, levels in rule_combos() if k in ("nguoi", "xe")]
        levels = list(rng.choice(scenes))
        levels[rng.randrange(LAMP_COUNT)] = 0
    elif kind == "chuyen_tiep":
        levels = [rng.randrange(BASE_PCT, MAX_PCT + 1, STEP_PCT) for _ in range(LAMP_COUNT)]
    else:  # "ngau_nhien"
        levels = [rng.randrange(0, MAX_PCT + 1, STEP_PCT) for _ in range(LAMP_COUNT)]
    return kind, tuple(levels)


def make_batch(rng: random.Random, size: int) -> list[tuple[str, tuple[int, ...]]]:
    """All rule combinations plus random variations up to ``size``, shuffled, no duplicates.

    If ``size`` is smaller than the number of rule combinations, a random subset of them is used.
    """
    rules = rule_combos()
    batch = rng.sample(rules, min(size, len(rules)))
    seen = {levels for _, levels in batch}
    for _ in range(size * MAX_ATTEMPTS_PER_COMBO):
        if len(batch) == size:
            break
        kind, levels = make_extra(rng)
        if levels not in seen:
            seen.add(levels)
            batch.append((kind, levels))
    rng.shuffle(batch)
    return batch


# ================================== Local files ==================================


def next_session_dir(out: Path) -> tuple[int, Path]:
    """Return the number and folder of the next session (highest existing ``phien_NNN`` + 1)."""
    numbers = [int(m.group(1)) for p in out.glob("phien_*") if p.is_dir() and (m := SESSION_RE.match(p.name))]
    number = max(numbers, default=0) + 1
    return number, out / f"phien_{number:03d}"


def start_session(out: Path, exposure_locked: bool) -> tuple[int, Path]:
    """Create the session folder with session.json and the index.csv header (same as the firmware)."""
    number, folder = next_session_dir(out)
    folder.mkdir(parents=True, exist_ok=False)
    info = {
        "session": number,
        "epoch": int(time.time()),
        "width": FRAME_WIDTH,
        "height": FRAME_HEIGHT,
        "format": "PGM P5 8-bit grayscale",
        "source": "QVGA 320x240 gray, full frame, pulled over WiFi",
        "exposure_locked": exposure_locked,
        "labels": list(LABELS),
    }
    (folder / "session.json").write_text(json.dumps(info, ensure_ascii=False) + "\n", encoding="utf-8")
    (folder / "index.csv").write_text(INDEX_HEADER, encoding="utf-8")
    return number, folder


def save_frame(folder: Path, seq: int, frame: Frame) -> str:
    """Write the frame (via a temp file, so a crash never leaves half a frame) and its index line.

    Returns:
        The file name, e.g. ``xe.0007.pgm``.
    """
    name = f"{frame.label}.{seq:04d}.pgm"
    tmp = folder / (name + ".tmp")
    tmp.write_bytes(frame.pgm)
    tmp.replace(folder / name)
    lamps = ",".join(str(x) for x in frame.lamps)
    with (folder / "index.csv").open("a", encoding="utf-8") as f:
        f.write(f"{name},{frame.label},{lamps},{int(frame.exposure_locked)},{int(time.time())}\n")
    return name


# ================================== Capture modes ==================================


def pull_one(host: str, folder: Path, seq: int, label: str, note: str = "") -> bool:
    """Download one frame and save it as number ``seq`` under ``label``. Returns False on failure."""
    frame = retry(lambda: fetch_frame(host), f"{note} frame")
    if frame is None:
        return False
    name = save_frame(folder, seq, dataclasses.replace(frame, label=label))
    print(f"  {note} saved {name}  lamps={frame.lamps}")
    return True


def read_key() -> str:
    """Wait for one key: a single press on Windows, a typed line + Enter elsewhere."""
    if msvcrt is not None:
        return msvcrt.getwch().lower()
    return input().strip().lower()[:1]


def drop_pending_keys() -> None:
    """Forget keys pressed while a batch was running, so a stray 's' does not start the next one."""
    while msvcrt is not None and msvcrt.kbhit():
        msvcrt.getwch()


def run_batch(host: str, folder: Path, batch: list[tuple[str, tuple[int, ...]]],
              settle_s: float, first_seq: int, label: str) -> int:
    """Set each lamp combination, wait ``settle_s`` and save one frame. Returns frames saved."""
    saved = 0
    for i, (kind, levels) in enumerate(batch, start=1):
        note = f"[{i:2d}/{len(batch)}] {kind:<11}"
        if retry(lambda: set_lamps(host, levels), f"{note} lamps {levels}") is None:
            continue
        time.sleep(settle_s)
        if pull_one(host, folder, first_seq + saved, label, note):
            saved += 1
    return saved


def print_keys(label: str) -> None:
    """Show the current label and the keys."""
    keys = "  ".join(f"{k}={v}" for k, v in LABEL_KEYS.items())
    print(f"Label: {label.upper()}   [{keys}]   {KEY_SAVE}=save a batch   {KEY_QUIT}=quit")


def run_batches(host: str, folder: Path, combos: int, settle_s: float, rng: random.Random,
                label: str) -> int:
    """Read keys: label keys change the label, KEY_SAVE runs one batch, KEY_QUIT stops."""
    print_keys(label)
    saved = 0
    position = 0
    while True:
        key = read_key()
        if key in (KEY_QUIT, KEY_CTRL_C):
            return saved
        if key in LABEL_KEYS:
            label = LABEL_KEYS[key]
            push_label(host, label)
            print_keys(label)
            continue
        if key != KEY_SAVE:
            continue
        position += 1
        print(f"Position {position} ({label}):")
        push_label(host, label)  # a rebooted board starts with its default label again
        saved += run_batch(host, folder, make_batch(rng, combos), settle_s, saved + 1, label)
        try:
            set_lamps(host, (BASE_PCT,) * LAMP_COUNT)  # back to the night base while you move things
        except NET_ERRORS:
            pass  # only cosmetic; the next batch sets every lamp anyway
        drop_pending_keys()
        print(f"\a>>> Position {position} done ({saved} frames in total). Move the object, then press "
              f"'{KEY_SAVE}'.")
        print_keys(label)


def run_timer(host: str, folder: Path, period_s: float, count: int, label: str) -> int:
    """Save a frame every ``period_s`` seconds, lamps untouched, until ``count`` frames (0 = Ctrl+C)."""
    saved = 0
    next_at = time.monotonic()
    while count == 0 or saved < count:
        next_at += period_s
        if pull_one(host, folder, saved + 1, label):
            saved += 1
        time.sleep(max(0.0, next_at - time.monotonic()))
    return saved


# ===================================== Main =====================================


def parse_args(argv: list[str]) -> argparse.Namespace:
    """Command-line options."""
    p = argparse.ArgumentParser(description="Save thu_dataset frames on the PC over WiFi.")
    p.add_argument("--host", default=DEFAULT_HOST, help=f"board address (default {DEFAULT_HOST})")
    p.add_argument("-o", "--out", type=Path, default=DEFAULT_OUT, help="dataset folder (default ./dataset)")
    p.add_argument("--label", choices=LABELS, help="starting label (default: the board's label)")
    p.add_argument("--combos", type=int, default=DEFAULT_COMBOS,
                   help=f"lamp combinations per press of '{KEY_SAVE}' (default {DEFAULT_COMBOS})")
    p.add_argument("--settle", type=float, default=DEFAULT_SETTLE_S,
                   help=f"seconds to wait after changing the lamps (default {DEFAULT_SETTLE_S})")
    p.add_argument("--seed", type=int, help="random seed, to repeat the same combinations")
    p.add_argument("--period", type=float, help="timer mode: save every N seconds, lamps untouched")
    p.add_argument("--count", type=int, default=0, help="timer mode: stop after this many frames")
    args = p.parse_args(argv)
    if args.combos <= 0 or args.settle < 0 or args.count < 0 or (args.period is not None and args.period <= 0):
        p.error("--combos and --period must be > 0, --settle and --count >= 0")
    return args


def main(argv: list[str]) -> int:
    """Pull frames until q or Ctrl+C (or --count in timer mode). Returns the process exit code."""
    args = parse_args(argv)
    try:
        status = http_json(f"http://{args.host}/api/status")
    except NET_ERRORS as e:
        print(f"Cannot reach the board at {args.host}: {e}. Is the laptop on WiFi 'StreetLight-Dataset'?")
        return 1
    if not status.get("cam_ok", False):
        print("The board reports that its camera is not working.")
        return 1
    label = args.label or status.get("label", LABELS[0])
    if label not in LABELS:
        label = LABELS[0]
    push_label(args.host, label)

    number, folder = start_session(args.out, bool(status.get("locked", False)))
    print(f"Session {number} -> {folder.resolve()}")
    try:
        if args.period is None:
            saved = run_batches(args.host, folder, args.combos, args.settle, random.Random(args.seed), label)
        else:
            saved = run_timer(args.host, folder, args.period, args.count, label)
    except KeyboardInterrupt:
        saved = sum(1 for _ in folder.glob("*.pgm"))
    print(f"Done: {saved} frames in {folder}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
