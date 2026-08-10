#!/usr/bin/env python3
"""Extract penguin sprite frames from the video -> penguin_sprites.h

Background removal = flood fill from the borders toward 'background'
prototypes (sky/water/sand, sampled from the left edge); the dark
outline contains the flood, so beak/feet survive even though their
orange is close to the sky. Each kept pixel is classified to a tone:
1=outline(dark) 2=body 3=bright(belly) 4=orange(beak/feet)
5=headband(red); 0=transparent. Sprites are RLE'd as (len<<3)|tone
bytes, tone 0 runs included. Flicker control: isolated-pixel
despeckle per frame, then a 3-frame temporal median over the
anchored canvases — single-frame specks and flood nibbles vanish.
"""
import subprocess, sys, collections

W, H = 69, 64
SRC = "/home/buku/Downloads/Gameboy Seiyu Paradise Draft.mp4"
# Actual scene inside the letterboxed 1920x1080 video (cropdetect).
CROP = "crop=1136:1056:410:2"
MAX_SPRITES = 32

raw = subprocess.run([
    "ffmpeg", "-v", "error", "-i", SRC,
    "-vf", f"fps=50/3,{CROP},scale={W}:{H}",
    "-f", "rawvideo", "-pix_fmt", "rgb24", "-",
], check=True, capture_output=True).stdout
plane = W * H * 3
nframes = len(raw) // plane
print(f"{nframes} frames")

def px(f, x, y):
    o = f * plane + (y * W + x) * 3
    return raw[o], raw[o + 1], raw[o + 2]

# background prototypes from frame 0 left edge (penguin never goes
# there): sky, water, sand. The horizon's darker oranges are caught by
# the generous threshold — the penguin's dark outline always contains
# the flood, so a loose threshold is safe.
protos = [px(0, 4, 6), px(0, 4, 30), px(0, 4, 50)]
print("bg protos:", protos)

def dist2(a, b):
    return sum((a[i] - b[i]) ** 2 for i in range(3))

T2 = 4000  # tight enough that anti-aliased outline pixels (dist²~7000
           # to the water proto) still block the flood; loose enough
           # that sky/water/sand shading passes

def mask_frame(f):
    """flood fill bg from borders; return bytearray mask (1 = penguin)."""
    bg = bytearray(W * H)
    q = collections.deque()
    for x in range(W):
        for y in (0, H - 1):
            q.append((x, y))
    for y in range(H):
        for x in (0, W - 1):
            q.append((x, y))
    while q:
        x, y = q.popleft()
        i = y * W + x
        if bg[i]:
            continue
        c = px(f, x, y)
        if min(dist2(c, p) for p in protos) > T2:
            continue
        bg[i] = 1
        if x > 0: q.append((x - 1, y))
        if x < W - 1: q.append((x + 1, y))
        if y > 0: q.append((x, y - 1))
        if y < H - 1: q.append((x, y + 1))
    return bg

def tones(f, bg):
    """tone map over the whole frame (0 outside mask)."""
    t = bytearray(W * H)
    for i in range(W * H):
        if bg[i]:
            continue
        o = i * 3
        r, g, b = raw[o], raw[o + 1], raw[o + 2]
        lum = (r * 77 + g * 150 + b * 29) >> 8
        if lum < 70:
            t[i] = 1        # navy outline
        elif r > b + 40 and g > b + 25:
            t[i] = 4        # orange beak/feet (green clearly above blue)
        elif r > b + 40:
            t[i] = 5        # red headband stripes (green ~= blue)
        elif lum > 190:
            t[i] = 3        # white belly/face
        elif b > r + 40:
            t[i] = 2        # royal blue body
        else:
            t[i] = 3
    return t

def largest_component(t):
    """Zero out everything but the largest 4-connected component.
    (4-conn on purpose: the horizon-line remnants only touch the
    penguin diagonally; the body itself is solid.)"""
    seen = bytearray(W * H)
    best = None
    for i in range(W * H):
        if not t[i] or seen[i]:
            continue
        comp = []
        q = collections.deque([i])
        seen[i] = 1
        while q:
            j = q.popleft()
            comp.append(j)
            x, y = j % W, j // W
            for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                nx, ny = x + dx, y + dy
                if 0 <= nx < W and 0 <= ny < H:
                    k = ny * W + nx
                    if t[k] and not seen[k]:
                        seen[k] = 1
                        q.append(k)
        if best is None or len(comp) > len(best):
            best = comp
    out = bytearray(W * H)
    for j in best:
        out[j] = t[j]
    return out


def fill_holes(t):
    """Flood the transparent pixels from the frame borders; zeros that
    stay unmarked are interior holes (body eaten by the water-colored
    flood) — fill them with body tone."""
    reach = bytearray(W * H)
    q = collections.deque()
    for x in range(W):
        for y in (0, H - 1):
            q.append((x, y))
    for y in range(H):
        for x in (0, W - 1):
            q.append((x, y))
    while q:
        x, y = q.popleft()
        i = y * W + x
        if reach[i] or t[i]:
            continue
        reach[i] = 1
        if x > 0: q.append((x - 1, y))
        if x < W - 1: q.append((x + 1, y))
        if y > 0: q.append((x, y - 1))
        if y < H - 1: q.append((x, y + 1))
    for i in range(W * H):
        if not t[i] and not reach[i]:
            t[i] = 2
    return t

# Fixed canvas, anchored by the component's bottom-center (feet), so
# the animation doesn't jitter as pose bounding boxes flap around.
CW, CH = 44, 32

def open_vertical(data, w, h):
    """Morphological opening with a vertical 2px kernel: 1px-tall
    horizontal lines (horizon remnants) vanish; blob edges erode then
    restore. Line segments crossing the body survive invisibly inside
    the mass, which is fine."""
    er = bytearray(w * h)
    for y in range(1, h):
        for x in range(w):
            if data[y * w + x] and data[(y - 1) * w + x]:
                er[y * w + x] = 1
    out = bytearray(w * h)
    for y in range(h):
        for x in range(w):
            if er[y * w + x] or (y + 1 < h and er[(y + 1) * w + x]):
                out[y * w + x] = data[y * w + x]  # restore tone
    return out

def keep_largest(data, w, h):
    """Zero all but the largest 4-connected component (drops speckles
    and stubs left behind by the opening)."""
    seen = bytearray(w * h)
    best = None
    for i in range(w * h):
        if not data[i] or seen[i]:
            continue
        comp = []
        q = collections.deque([i])
        seen[i] = 1
        while q:
            j = q.popleft()
            comp.append(j)
            x, y = j % w, j // w
            for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                nx, ny = x + dx, y + dy
                if 0 <= nx < w and 0 <= ny < h:
                    k = ny * w + nx
                    if data[k] and not seen[k]:
                        seen[k] = 1
                        q.append(k)
        if best is None or len(comp) > len(best):
            best = comp
    out = bytearray(w * h)
    for j in best:
        out[j] = data[j]
    return out

def despeckle(data, w, h, passes=2):
    """Drop fully isolated pixels (no 4-connected lit neighbor) —
    single-frame flood nibbles that read as flicker on the bar."""
    for _ in range(passes):
        kill = []
        for y in range(h):
            for x in range(w):
                i = y * w + x
                if not data[i]:
                    continue
                n = 0
                for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                    nx, ny = x + dx, y + dy
                    if 0 <= nx < w and 0 <= ny < h and data[ny * w + nx]:
                        n += 1
                if n == 0:
                    kill.append(i)
        for i in kill:
            data[i] = 0
    return data

def temporal_median(canvases):
    """Majority vote over (prev, cur, next) anchored canvases — a
    pixel present in only one of three frames is flicker, not pose.
    Poses hold for several source frames, so real motion survives."""
    out = []
    n = len(canvases)
    for i in range(n):
        a = canvases[max(0, i - 1)]
        b = canvases[i]
        c = canvases[min(n - 1, i + 1)]
        m = bytearray(len(b))
        for p in range(len(b)):
            if b[p] == a[p] or b[p] == c[p]:
                m[p] = b[p]
            elif a[p] == c[p]:
                m[p] = a[p]
            else:
                m[p] = b[p]  # all differ: genuine fast motion
        out.append(m)
    return out

canvases = []
for f in range(nframes):
    bg = mask_frame(f)
    t = fill_holes(largest_component(tones(f, bg)))
    xs, ys = [], []
    for y in range(H):
        for x in range(W):
            if t[y * W + x]:
                xs.append(x); ys.append(y)
    if not xs:
        canvases.append(bytearray(CW * CH))
        continue
    x0, x1, y1 = min(xs), max(xs), max(ys)
    # Anchor on the feet (bottom rows of the component): they stay
    # planted through the dance, while bbox edges flap with flippers.
    foot_xs = [x for y in range(H) for x in range(W)
               if t[y * W + x] and y >= y1 - 2]
    bcx = (min(foot_xs) + max(foot_xs)) // 2
    data = bytearray(CW * CH)
    for y in range(H):
        for x in range(W):
            if t[y * W + x]:
                dx = x - bcx + CW // 2
                dy = y - y1 + CH - 1
                if 0 <= dx < CW and 0 <= dy < CH:
                    data[dy * CW + dx] = t[y * W + x]
    data = open_vertical(data, CW, CH)
    data = keep_largest(data, CW, CH)
    canvases.append(despeckle(data, CW, CH))

canvases = temporal_median(canvases)

sprites = []
for data in canvases:
    if not any(data):
        continue
    # Leak guard: a frame whose surviving component is implausibly
    # small (flood breached the outline and ate the body) reuses the
    # previous good sprite instead of glitching.
    lit = sum(1 for b in data if b)
    if sprites and lit < 0.45 * sprites[-1][4]:
        sprites.append((CW, CH, CW // 2, sprites[-1][3],
                        sprites[-1][4]))
        continue
    # dedupe against last kept sprite
    if sprites:
        pd = sprites[-1][3]
        diff = sum(1 for a, b in zip(pd, data) if a != b)
        if diff < 0.02 * CW * CH:
            continue
    sprites.append((CW, CH, CW // 2, data, lit))

print(f"{len(sprites)} distinct sprites")

# cap count: thin evenly
if len(sprites) > MAX_SPRITES:
    step = len(sprites) / MAX_SPRITES
    sprites = [sprites[int(i * step)] for i in range(MAX_SPRITES)]
    print(f"thinned to {len(sprites)}")

# belly-slide pose: the dance frame with the strongest forward lean
# (widest content relative to height) — frozen, it reads as a lunge
# at slide speed. No rotation: a 90-degree turn of this sprite is an
# unreadable blob, tried it.
best_i, best_ratio = 0, 0.0
for i, (w, h, _, d, _lit) in enumerate(sprites):
    xs = [j % w for j in range(w * h) if d[j]]
    ys = [j // w for j in range(w * h) if d[j]]
    ratio = (max(xs) - min(xs) + 1) / (max(ys) - min(ys) + 1)
    if ratio > best_ratio:
        best_i, best_ratio = i, ratio
sprites_slide = sprites[best_i]
print(f"slide pose: frame {best_i} (w/h {best_ratio:.2f})")

def rle(data):
    out = bytearray()
    i, n = 0, len(data)
    while i < n:
        v = data[i]
        ln = 1
        while i + ln < n and data[i + ln] == v and ln < 31:
            ln += 1
        out.append((ln << 3) | v)
        i += ln
    return out

with open("/home/buku/Documents/touchbar-fx/src/viz/presets/penguin_sprites.h", "w") as fh:
    fh.write("#pragma once\n#include <cstdint>\n\n")
    fh.write("// Generated by tools/penguin_extract.py from the Gameboy\n")
    fh.write("// penguin video. 3-bit tones (0=transparent 1=outline\n")
    fh.write("// 2=body 3=bright 4=orange-beak/feet 5=red-headband),\n")
    fh.write("// RLE as (len<<3)|tone bytes.\n")
    fh.write("struct PenguinSprite { int w, h; int rle_len; const uint8_t* rle; };\n\n")
    names = []
    for i, (w, h, _, d, _lit) in enumerate(sprites):
        r = rle(d)
        nm = f"peng_{i}"
        names.append((nm, w, h, len(r)))
        fh.write(f"static const uint8_t {nm}_rle[{len(r)}] = {{")
        fh.write(",".join(str(b) for b in r))
        fh.write("};\n")
    fh.write(f"\nstatic const int PENGUIN_FRAME_COUNT = {len(names)};\n")
    fh.write("static const PenguinSprite PENGUIN_FRAMES[] = {\n")
    for nm, w, h, ln in names:
        fh.write(f"    {{{w}, {h}, {ln}, {nm}_rle}},\n")
    fh.write("};\n")
    sw, sh, _, _, _ = sprites_slide
    fh.write(f"static const PenguinSprite PENGUIN_SLIDE = "
             f"{{{sw}, {sh}, {names[best_i][3]}, peng_{best_i}_rle}};\n")
print("header written")
