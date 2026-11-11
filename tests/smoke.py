"""Smoke tests: cui-repl opens a window, draws a child's output, and exits with
the child's exit code. CUI_SCREENSHOT saves a PNG after N frames and quits.

Runs under pytest (CUI_REPL=path/to/cui-repl) or as a script: smoke.py CUI_REPL.
As a script it exits 77 (ctest: skipped) where no window can open.
"""
import os
import struct
import subprocess
import sys
import tempfile
import zlib
from pathlib import Path

APP = os.environ.get("CUI_REPL", "build/cui-repl")


def no_display():
    return sys.platform.startswith("linux") and not os.environ.get("DISPLAY")


def read_png(path):
    """(width, height, rows of RGBA tuples) for an 8-bit RGB or RGBA PNG."""
    data = Path(path).read_bytes()
    assert data[:8] == b"\x89PNG\r\n\x1a\n"
    pos, idat, w = 8, b"", 0
    while pos < len(data):
        n, kind = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + n]
        if kind == b"IHDR":
            w, h, depth, color = struct.unpack(">IIBB", body[:10])
            assert depth == 8 and color in (2, 6), (depth, color)
            bpp = 4 if color == 6 else 3
        elif kind == b"IDAT":
            idat += body
        pos += 12 + n
    raw, stride, rows, prev = zlib.decompress(idat), w * bpp, [], bytearray(w * bpp)
    for y in range(h):
        f, line = raw[y * (stride + 1)], bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for i in range(stride):  # undo the PNG row filter
            a = line[i - bpp] if i >= bpp else 0
            b, c = prev[i], prev[i - bpp] if i >= bpp else 0
            if f == 1:
                line[i] = (line[i] + a) & 255
            elif f == 2:
                line[i] = (line[i] + b) & 255
            elif f == 3:
                line[i] = (line[i] + (a + b) // 2) & 255
            elif f == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        rows.append([tuple(line[x * bpp:x * bpp + 3]) for x in range(w)])
        prev = line
    return w, h, rows


def run(args, shot, frames="60", timeout=60):
    env = {**os.environ, "CUI_SCREENSHOT": str(shot), "CUI_SCREENSHOT_FRAMES": frames}
    return subprocess.run([APP, *args], env=env, capture_output=True, text=True, timeout=timeout, check=False)


def longest_run(row, pred):
    best = cur = 0
    for px in row:
        cur = cur + 1 if pred(px) else 0
        best = max(best, cur)
    return best


def count(rows, pred):
    return sum(1 for row in rows for px in row if pred(px))


def test_draws_colors_and_box_lines():
    script = (r"printf '\033[1;31mRED RED RED\033[0m\r\n\033[44m          \033[0m\r\n';"
              r"printf '\342\224\214\342\224\200\342\224\200\342\224\220\r\n';"
              r"printf '\342\224\202\r\n\342\224\202\r\n\342\224\202\r\n'; sleep 30")
    with tempfile.TemporaryDirectory() as tmp:
        shot = Path(tmp, "shot.png")
        p = run(["sh", "-c", script], shot)
        assert p.returncode == 0, p.stderr
        _, _, rows = read_png(shot)
    red = count(rows, lambda px: px[0] > 150 and px[1] < 110 and px[2] < 110)
    blue = count(rows, lambda px: px[2] > 150 and px[2] - px[0] > 30)  # palette 4 is a light steel blue
    white_run = max(longest_run(row, lambda px: min(px) > 200) for row in rows)
    assert red > 200, f"red text: {red} pixels"
    assert blue > 2000, f"blue background: {blue} pixels"
    # The box's top edge joins centre to centre over 3 cells, about 2.6% of the
    # 900-point window; the cursor block is one cell, under 1%.
    assert white_run > 2 * len(rows[0]) / 100, f"box line: longest white run {white_run} pixels"
    # Three rows of a vertical line join into one run, which a font's glyphs do
    # not at a terminal's line height. A row is about 3.2% of the window's height.
    columns = list(zip(*rows))
    tall = max(longest_run(col, lambda px: min(px) > 200) for col in columns)
    assert tall > 5 * len(rows) / 100, f"vertical line: longest white run {tall} pixels"


def test_exit_code_is_the_childs():
    with tempfile.TemporaryDirectory() as tmp:
        p = run(["sh", "-c", "sleep 0.3; exit 3"], Path(tmp, "unused.png"), frames="100000")
    assert p.returncode == 3, p.stderr


def test_missing_command_exits_127():
    with tempfile.TemporaryDirectory() as tmp:
        p = run(["/nonexistent/cui-repl-no-such-program"], Path(tmp, "unused.png"), frames="100000")
    assert p.returncode == 127, p.stderr


if __name__ == "__main__":
    if len(sys.argv) > 1:
        APP = sys.argv[1]
    if no_display():
        sys.exit(77)
    for name, fn in list(globals().items()):
        if name.startswith("test_"):
            fn()
            print("ok  ", name)
