#!/usr/bin/env python3
"""Generate the FlipperOS desktop animation (128x64, 1-bit frames).

    python3 firmware/tools/make_animation.py

Writes frame_*.png and meta.txt into
firmware/overlay/assets/dolphin/external/L1_FlipperOS_128x64/.
The manifest entry that enables the animation lives in
firmware/patches/0001-flipperos-desktop-animation.patch.
Needs Pillow (pip install pillow).
"""

from pathlib import Path

from PIL import Image, ImageDraw

WIDTH, HEIGHT = 128, 64
NAME = "L1_FlipperOS_128x64"
OUT = (
    Path(__file__).resolve().parent.parent
    / "overlay/assets/dolphin/external"
    / NAME
)

# 5x7 pixel font, only the glyphs we need
FONT = {
    "A": ["01110", "10001", "10001", "11111", "10001", "10001", "10001"],
    "D": ["11110", "10001", "10001", "10001", "10001", "10001", "11110"],
    "E": ["11111", "10000", "10000", "11110", "10000", "10000", "11111"],
    "F": ["11111", "10000", "10000", "11110", "10000", "10000", "10000"],
    "H": ["10001", "10001", "10001", "11111", "10001", "10001", "10001"],
    "I": ["11111", "00100", "00100", "00100", "00100", "00100", "11111"],
    "L": ["10000", "10000", "10000", "10000", "10000", "10000", "11111"],
    "N": ["10001", "11001", "10101", "10011", "10001", "10001", "10001"],
    "O": ["01110", "10001", "10001", "10001", "10001", "10001", "01110"],
    "P": ["11110", "10001", "10001", "11110", "10000", "10000", "10000"],
    "R": ["11110", "10001", "10001", "11110", "10100", "10010", "10001"],
    "S": ["01111", "10000", "10000", "01110", "00001", "00001", "11110"],
    "U": ["10001", "10001", "10001", "10001", "10001", "10001", "01110"],
    " ": ["00000"] * 7,
}

TITLE = "FLIPPER OS"
SUBTITLE = "UNLEASHED INSIDE"


def text_width(text, scale):
    return len(text) * 6 * scale - scale


def draw_text(draw, x, y, text, scale, fill=1):
    for ch in text:
        for row, bits in enumerate(FONT[ch]):
            for col, bit in enumerate(bits):
                if bit == "1":
                    draw.rectangle(
                        [
                            x + col * scale,
                            y + row * scale,
                            x + (col + 1) * scale - 1,
                            y + (row + 1) * scale - 1,
                        ],
                        fill=fill,
                    )
        x += 6 * scale


def draw_chip(draw, cx, cy, lit):
    """The chip from the main menu icon: a die with pins on every side."""
    draw.rectangle([cx - 7, cy - 7, cx + 7, cy + 7], outline=1)
    if lit:
        draw.rectangle([cx - 4, cy - 4, cx + 4, cy + 4], fill=1)
    else:
        draw.rectangle([cx - 4, cy - 4, cx + 4, cy + 4], outline=1)
    for offset in (-4, 0, 4):
        draw.line([cx + offset, cy - 10, cx + offset, cy - 8], fill=1)
        draw.line([cx + offset, cy + 8, cx + offset, cy + 10], fill=1)
        draw.line([cx - 10, cy + offset, cx - 8, cy + offset], fill=1)
        draw.line([cx + 8, cy + offset, cx + 10, cy + offset], fill=1)


def make_frame(step, typed, cursor, subtitle, lit):
    image = Image.new("1", (WIDTH, HEIGHT), 0)
    draw = ImageDraw.Draw(image)

    # Chip in the top-left corner, blinking while "booting"
    draw_chip(draw, 14, 14, lit)

    # Boot progress along the top, next to the chip
    bar_x0, bar_x1 = 30, 124
    draw.rectangle([bar_x0, 10, bar_x1, 16], outline=1)
    filled = bar_x0 + 2 + (bar_x1 - bar_x0 - 4) * step // 11
    if filled > bar_x0 + 2:
        draw.rectangle([bar_x0 + 2, 12, min(filled, bar_x1 - 2), 14], fill=1)

    # Title, typed letter by letter at double size
    scale = 2
    x = (WIDTH - text_width(TITLE, scale)) // 2
    y = 28
    draw_text(draw, x, y, TITLE[:typed], scale)
    if cursor:
        cx = x + typed * 6 * scale
        if cx + 9 < WIDTH:
            draw.rectangle([cx, y, cx + 9, y + 13], fill=1)

    if subtitle:
        sx = (WIDTH - text_width(SUBTITLE, 1)) // 2
        draw_text(draw, sx, 52, SUBTITLE, 1)
        draw.line([sx, 61, sx + text_width(SUBTITLE, 1) - 1, 61], fill=1)
    return image


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    for old in OUT.glob("frame_*.png"):
        old.unlink()

    frames = []
    # 0-10: type the title while the boot bar fills up
    for step in range(len(TITLE) + 1):
        frames.append(make_frame(step, step, True, False, step % 2 == 0))
    # 11-12: booted - full title and subtitle, the chip keeps blinking
    frames.append(make_frame(11, len(TITLE), False, True, True))
    frames.append(make_frame(11, len(TITLE), False, True, False))

    for index, frame in enumerate(frames):
        frame.save(OUT / f"frame_{index}.png")

    # Type once, then keep blinking the chip for a while
    order = list(range(len(frames))) + [11, 12] * 8
    meta = [
        "Filetype: Flipper Animation",
        "Version: 1",
        "",
        f"Width: {WIDTH}",
        f"Height: {HEIGHT}",
        f"Passive frames: {len(order)}",
        "Active frames: 0",
        "Frames order: " + " ".join(str(i) for i in order),
        "Active cycles: 0",
        "Frame rate: 4",
        "Duration: 3600",
        "Active cooldown: 0",
        "",
        "Bubble slots: 0",
        "",
    ]
    (OUT / "meta.txt").write_text("\n".join(meta))
    print(f"Wrote {len(frames)} frames to {OUT}")


if __name__ == "__main__":
    main()
