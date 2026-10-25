"""Convert frames saved by thu_dataset into PNG files.

The current firmware stores 640x360 8-bit grayscale frames as binary PGM (``.pgm``). Older
sessions stored raw RGB565 (``.rgb565``, big-endian: the first byte of every pixel is the high
byte); those are still converted. Edge Impulse cannot read either format, so this script writes
PNG images and keeps the file name, e.g. ``nguoi.0007.pgm`` becomes ``nguoi.0007.png`` (Edge
Impulse reads the label from the text before the first dot).

Usage:
    1. Paste the folder (or one file) into INPUT_PATH below, then run ``python convert_raw.py``.
    2. Or give the path on the command line, which overrides INPUT_PATH::

        python convert_raw.py D:/dataset/phien_001
        python convert_raw.py nguoi.0007.pgm -o out_folder
"""
from __future__ import annotations

import argparse
import sys
from pathlib import Path

import numpy as np
from PIL import Image

FRAME_WIDTH = 640
FRAME_HEIGHT = 360
BYTES_PER_PIXEL = 2
RAW_SUFFIX = ".rgb565"
PGM_SUFFIX = ".pgm"
INPUT_SUFFIXES = (PGM_SUFFIX, RAW_SUFFIX)

# ============================== PASTE YOUR PATHS HERE ==============================
# Folder (searched recursively) or single file to convert. Paste it between the quotes.
# Quotes pasted from "Copy as path" are fine. Do not end a path with a backslash.
INPUT_PATH = r"D:\dataset\phien_001"

# Optional: folder for the PNG files. Leave empty to write each PNG next to its source file.
OUTPUT_FOLDER = r"D:\dataset\phien_001"
# ===================================================================================


def rgb565_be_to_rgb888(raw: bytes, width: int, height: int) -> np.ndarray:
    """Decode big-endian RGB565 bytes into an ``(height, width, 3)`` uint8 RGB array.

    Args:
        raw: Pixel data, ``width * height * 2`` bytes.
        width: Image width in pixels.
        height: Image height in pixels.

    Returns:
        RGB image, 8 bits per channel. The 5/6-bit values are widened by repeating their top
        bits, so full white stays 255 instead of 248.

    Raises:
        ValueError: If ``raw`` does not have exactly the expected length.
    """
    expected = width * height * BYTES_PER_PIXEL
    if len(raw) != expected:
        raise ValueError(f"expected {expected} bytes, got {len(raw)}")
    pixels = np.frombuffer(raw, dtype=">u2").astype(np.uint16).reshape(height, width)
    r5 = (pixels >> 11) & 0x1F
    g6 = (pixels >> 5) & 0x3F
    b5 = pixels & 0x1F
    red = (r5 << 3) | (r5 >> 2)
    green = (g6 << 2) | (g6 >> 4)
    blue = (b5 << 3) | (b5 >> 2)
    return np.stack([red, green, blue], axis=-1).astype(np.uint8)


def load_pgm(source: Path, width: int, height: int) -> Image.Image:
    """Load a binary PGM frame and check that it is an 8-bit grayscale image of the expected size.

    Args:
        source: PGM file written by the board.
        width: Expected image width in pixels.
        height: Expected image height in pixels.

    Returns:
        The image in Pillow mode ``"L"`` (8-bit grayscale).

    Raises:
        ValueError: If the file is not a ``width`` x ``height`` grayscale PGM.
        OSError: If the file cannot be read.
    """
    with Image.open(source) as image:
        image.load()
        if image.format != "PPM" or image.mode != "L":
            raise ValueError(f"not an 8-bit grayscale PGM (format {image.format}, mode {image.mode})")
        if image.size != (width, height):
            raise ValueError(f"expected {width}x{height}, got {image.size[0]}x{image.size[1]}")
        return image.copy()


def convert_file(source: Path, destination: Path, width: int, height: int) -> None:
    """Convert one ``.pgm`` or ``.rgb565`` frame into a PNG file.

    Args:
        source: Frame file written by the board.
        destination: PNG file to write (its folder is created if needed).
        width: Image width in pixels.
        height: Image height in pixels.

    Raises:
        ValueError: If the file content does not match ``width`` x ``height``.
        OSError: If the file cannot be read or written.
    """
    if source.suffix.lower() == PGM_SUFFIX:
        image = load_pgm(source, width, height)
    else:
        image = Image.fromarray(rgb565_be_to_rgb888(source.read_bytes(), width, height), mode="RGB")
    destination.parent.mkdir(parents=True, exist_ok=True)
    image.save(destination)


def find_inputs(path: Path) -> list[Path]:
    """Return the frames to convert: the file itself, or every ``.pgm`` / ``.rgb565`` below a folder."""
    if path.is_file():
        return [path]
    return sorted(p for p in path.rglob("*") if p.suffix.lower() in INPUT_SUFFIXES)


def clean_path(text: str) -> Path | None:
    """Turn a pasted path into a ``Path``.

    Surrounding spaces and quotes are removed, so a path copied with "Copy as path" works.

    Args:
        text: The pasted text, possibly empty.

    Returns:
        The path, or ``None`` when ``text`` is empty.
    """
    cleaned = text.strip().strip('"').strip("'").strip()
    return Path(cleaned) if cleaned else None


def parse_args(argv: list[str]) -> argparse.Namespace:
    """Parse the command line."""
    parser = argparse.ArgumentParser(description="Convert thu_dataset .pgm / .rgb565 frames to PNG.")
    parser.add_argument("input", type=Path, nargs="?", default=None,
                        help="a .pgm or .rgb565 file, or a folder (searched recursively); "
                             "default: INPUT_PATH at the top of this file")
    parser.add_argument("-o", "--output", type=Path, default=None,
                        help="output folder (default: OUTPUT_FOLDER, else next to each input file)")
    parser.add_argument("--width", type=int, default=FRAME_WIDTH, help=f"frame width (default {FRAME_WIDTH})")
    parser.add_argument("--height", type=int, default=FRAME_HEIGHT, help=f"frame height (default {FRAME_HEIGHT})")
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    """Run the converter. Returns 0 when every file converted, 1 otherwise."""
    args = parse_args(sys.argv[1:] if argv is None else argv)
    input_path = args.input if args.input is not None else clean_path(INPUT_PATH)
    if input_path is None:
        print("error: paste the folder path into INPUT_PATH at the top of convert_raw.py, "
              "or give it on the command line", file=sys.stderr)
        return 1
    output_folder = args.output if args.output is not None else clean_path(OUTPUT_FOLDER)
    if not input_path.exists():
        print(f"error: {input_path} does not exist", file=sys.stderr)
        return 1
    inputs = find_inputs(input_path)
    if not inputs:
        print(f"error: no {PGM_SUFFIX} or {RAW_SUFFIX} files found in {input_path}", file=sys.stderr)
        return 1

    failures = 0
    for source in inputs:
        folder = output_folder if output_folder is not None else source.parent
        destination = folder / (source.stem + ".png")
        try:
            convert_file(source, destination, args.width, args.height)
        except (ValueError, OSError) as exc:
            failures += 1
            print(f"skipped {source}: {exc}", file=sys.stderr)
            continue
        print(f"{source} -> {destination}")
    print(f"done: {len(inputs) - failures} converted, {failures} skipped")
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
