"""Generates assets/davedown.ico: the same red tile with a down-arrow that
src/main.cpp draws for the window icon, as a 16/32/48/256 px ICO so Explorer
shows it on .md files once davedown is their handler. Pure Python, no PIL.
Run from the project root: python tools/make_icon.py
"""
import struct


def tile(n):
    r = max(2, n * 12 // 64)
    px = [[(0, 0, 0, 0)] * n for _ in range(n)]
    bg = (200, 48, 48, 255)
    fg = (255, 255, 255, 255)
    for y in range(n):
        for x in range(n):
            dx = (r - x) if x < r else (x - (n - r - 1) if x >= n - r else 0)
            dy = (r - y) if y < r else (y - (n - r - 1) if y >= n - r else 0)
            if dx * dx + dy * dy <= r * r:
                px[y][x] = bg
    s = n / 64.0
    for y in range(int(14 * s), int(38 * s)):
        for x in range(int(28 * s), int(36 * s) + (1 if n < 32 else 0)):
            px[y][x] = fg
    for i in range(int(14 * s) + 1):
        y = int(36 * s) + i
        if y >= n:
            break
        for x in range(int(18 * s) + i, int(46 * s) - i):
            if 0 <= x < n:
                px[y][x] = fg
    return px


def bmp_entry(n):
    px = tile(n)
    # BITMAPINFOHEADER with doubled height (XOR + AND masks), 32bpp BGRA.
    header = struct.pack("<IiiHHIIiiII", 40, n, n * 2, 1, 32, 0, n * n * 4, 0, 0, 0, 0)
    body = bytearray()
    for y in range(n - 1, -1, -1):
        for x in range(n):
            r, g, b, a = px[y][x]
            body += bytes((b, g, r, a))
    row = ((n + 31) // 32) * 4
    for y in range(n - 1, -1, -1):
        bits = bytearray(row)
        for x in range(n):
            if px[y][x][3] == 0:
                bits[x // 8] |= 0x80 >> (x % 8)
        body += bits
    return header + body


def main():
    sizes = [16, 32, 48, 256]
    images = [bmp_entry(n) for n in sizes]
    out = bytearray(struct.pack("<HHH", 0, 1, len(sizes)))
    offset = 6 + 16 * len(sizes)
    for n, img in zip(sizes, images):
        out += struct.pack("<BBBBHHII", n if n < 256 else 0, n if n < 256 else 0, 0, 0, 1, 32, len(img), offset)
        offset += len(img)
    for img in images:
        out += img
    with open("assets/davedown.ico", "wb") as f:
        f.write(out)
    print("wrote assets/davedown.ico", len(out), "bytes")


if __name__ == "__main__":
    main()
