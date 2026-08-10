#!/usr/bin/env python3
"""
Encode any video into a Pioneer `.lkd` OEL clip.

This is the inverse of src/viz/lkd.cpp (format reverse-engineered from
carrozzeria head-unit data):

    0x00  "zLKD" magic
    0x04  uint32  version (=3)
    0x08  uint32  (=1)
    0x0C  uint32  (=7)
    0x10  uint32  frame count
    0x14  gzip stream -> TAR -> single 24-bit BMP (256 x 64*N, bottom-up)
          = N vertically stacked frames of 256 x 64, frame 0 at the top

The authentic clips use a 4-color cyan phosphor palette (2-bit panel),
so frames are scaled/cropped to 256x64, converted to luma, and
Floyd-Steinberg dithered onto those exact 4 colors. Requires only
ffmpeg + the Python stdlib.

Usage: lkd_encode.py INPUT [-o OUT.lkd] [--start SEC] [--frames N]
                             [--fps F]
"""
import argparse
import gzip
import os
import struct
import subprocess
import sys

W, H = 256, 64

# The exact BGR palette found in the stock clips (verified against
# movie8_f.lkd): black + three cyan phosphor drive levels.
PALETTE = [
    (0, 0, 0),        # BGR black
    (175, 158, 7),    # dim
    (220, 199, 5),    # mid
    (255, 255, 0),    # full cyan
]
# Luma of each palette entry (what the ditherer targets), evenly
# spaced input levels map onto the palette in order.
LEVELS = [0, 85, 170, 255]


def extract_frames(path, start, frames, fps):
    """Luma planes (bytes, row-major top-down) via ffmpeg."""
    vf = (f"scale={W}:{H}:force_original_aspect_ratio=increase,"
          f"crop={W}:{H},fps={fps}")
    cmd = [
        "ffmpeg", "-v", "error", "-ss", str(start), "-i", path,
        "-vf", vf, "-frames:v", str(frames),
        "-f", "rawvideo", "-pix_fmt", "gray", "-",
    ]
    raw = subprocess.run(cmd, check=True, capture_output=True).stdout
    plane = W * H
    got = len(raw) // plane
    if got == 0:
        sys.exit(f"{path}: no frames decoded (bad --start/too short?)")
    if got < frames:
        print(f"warning: only {got}/{frames} frames available",
              file=sys.stderr)
    return [raw[i * plane:(i + 1) * plane] for i in range(got)]


def dither(plane):
    """Floyd-Steinberg onto 4 levels; returns palette indices."""
    buf = [list(row) for row in
           zip(*[iter(plane)] * W)]  # rows of ints
    out = bytearray(W * H)
    for y in range(H):
        row = buf[y]
        nrow = buf[y + 1] if y + 1 < H else None
        for x in range(W):
            v = row[x]
            # nearest of the 4 levels
            best, bd = 0, 1e9
            for i, lv in enumerate(LEVELS):
                d = abs(v - lv)
                if d < bd:
                    best, bd = i, d
            out[y * W + x] = best
            err = v - LEVELS[best]
            if x + 1 < W:
                row[x + 1] += err * 7 / 16
            if nrow is not None:
                if x > 0:
                    nrow[x - 1] += err * 3 / 16
                nrow[x] += err * 5 / 16
                if x + 1 < W:
                    nrow[x + 1] += err * 1 / 16
    return out


def build_bmp(index_frames):
    """Stack frames vertically into one 24-bit bottom-up BMP."""
    n = len(index_frames)
    strip_h = H * n
    row_size = (W * 3 + 3) & ~3
    pad = row_size - W * 3
    img_size = row_size * strip_h

    header = bytearray(54)
    header[0:2] = b"BM"
    struct.pack_into("<I", header, 2, 54 + img_size)
    struct.pack_into("<I", header, 10, 54)          # data offset
    struct.pack_into("<I", header, 14, 40)          # DIB size
    struct.pack_into("<i", header, 18, W)
    struct.pack_into("<i", header, 22, strip_h)     # positive: bottom-up
    struct.pack_into("<H", header, 26, 1)           # planes
    struct.pack_into("<H", header, 28, 24)          # bpp
    struct.pack_into("<I", header, 34, img_size)

    body = bytearray()
    # BMP stores bottom row first: frame N-1's last row ... frame 0's first.
    for fi in range(n - 1, -1, -1):
        fr = index_frames[fi]
        for y in range(H - 1, -1, -1):
            for x in range(W):
                body += bytes(PALETTE[fr[y * W + x]])
            body += b"\0" * pad
    return bytes(header) + bytes(body)


def build_tar(name, payload):
    """Minimal tar with a single regular file member."""
    def field(s, length):
        return s.encode().ljust(length, b"\0")[:length]

    hdr = bytearray(512)
    hdr[0:100] = field(name, 100)
    hdr[100:108] = field("0000644", 8)
    hdr[108:116] = field("0000000", 8)
    hdr[116:124] = field("0000000", 8)
    hdr[124:136] = field("%o" % len(payload), 12)
    hdr[136:148] = field("14000000000", 12)  # mtime, octal
    hdr[148:156] = b"        "               # checksum: spaces first
    hdr[156:157] = b"0"                      # regular file
    chksum = sum(hdr)
    hdr[148:156] = ("%06o\0 " % chksum).encode()
    pad = (512 - len(payload) % 512) % 512
    return bytes(hdr) + payload + b"\0" * pad


def encode(path, out, start, frames, fps):
    print(f"extracting {frames} frames at {fps:.2f} fps from {path} ...")
    raw_frames = extract_frames(path, start, frames, fps)
    print(f"dithering {len(raw_frames)} frames onto the 4-color palette ...")
    index_frames = [dither(f) for f in raw_frames]
    bmp = build_bmp(index_frames)
    tar = build_tar("LKD000", bmp)
    blob = struct.pack("<4s4I", b"zLKD", 3, 1, 7, len(index_frames)) \
        + gzip.compress(tar)
    with open(out, "wb") as f:
        f.write(blob)
    print(f"{out}: {len(index_frames)} frames, {len(blob)} bytes")


def main():
    ap = argparse.ArgumentParser(description="Encode video to Pioneer .lkd")
    ap.add_argument("input")
    ap.add_argument("-o", "--output")
    ap.add_argument("--start", type=float, default=0.0,
                    help="start offset, seconds")
    ap.add_argument("--frames", type=int, default=60,
                    help="frame count (stock clips are 60 = ~3.6 s loop)")
    ap.add_argument("--fps", type=float, default=1000.0 / 60.0,
                    help="sample rate (stock: 16.67 = 60 ms/frame)")
    args = ap.parse_args()
    out = args.output or os.path.splitext(os.path.basename(args.input))[0] \
        + ".lkd"
    encode(args.input, out, args.start, args.frames, args.fps)


if __name__ == "__main__":
    main()
