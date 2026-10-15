#!/usr/bin/env python3
"""Observable M01 triangle pixels: top-down RGB8, analytic barycentric colours.

Ignore a 0.02 barycentric band at edges (raster coverage differs by backend).
Interior channels must be within 2/255; background within 1/255. The expected
vertices come from the shared authored asset, not from Vulkan internals.
"""
from pathlib import Path
import sys


def image(path):
    with open(path, 'rb') as f:
        assert f.readline() == b'P6\n', path
        w, h = map(int, f.readline().split())
        assert f.readline() == b'255\n', path
        pixels = f.read()
    assert len(pixels) == w * h * 3, path
    return w, h, pixels


def verify(path, size, split=False, clear=False):
    w, h, pixels = image(path)
    assert (w, h) == size, (path, w, h, size)
    root = Path(__file__).resolve().parents[1]
    vertices = [list(map(float, line.split())) for line in
                (root / 'docs/comparison/m01-basics/assets/Triangle.vertices').read_text().splitlines()]
    draws = [] if clear else ([(-0.5, 0.5, (1, .25, .25)), (.5, .5, (.25, 1, 1))]
                              if split else [(0, 1, (1, 1, 1))])
    background = (0, 0, 0) if clear else (.05, .075, .1)
    interior = [0] * len(draws)
    for y in range(h):
        for x in range(w):
            px, py = (x + .5) / w * 2 - 1, 1 - (y + .5) / h * 2
            expected, tolerance, edge = background, 1, False
            for draw, (offset, scale, tint) in enumerate(draws):
                a, b, c = [(v[0] * scale + offset, v[1] * scale) for v in vertices]
                denominator = (b[1]-c[1])*(a[0]-c[0]) + (c[0]-b[0])*(a[1]-c[1])
                u = ((b[1]-c[1])*(px-c[0]) + (c[0]-b[0])*(py-c[1])) / denominator
                v = ((c[1]-a[1])*(px-c[0]) + (a[0]-c[0])*(py-c[1])) / denominator
                weights = (u, v, 1-u-v)
                if min(weights) >= -.02:
                    if min(weights) < .02:
                        edge = True
                    else:
                        interior[draw] += 1
                        expected = tuple(sum(weights[i] * vertices[i][3+channel] for i in range(3)) * tint[channel]
                                         for channel in range(3))
                        tolerance = 2
            if edge:
                continue
            actual = pixels[(y*w+x)*3:(y*w+x)*3+3]
            assert all(abs(actual[i] - round(expected[i]*255)) <= tolerance for i in range(3)), (
                str(path), (x, y), tuple(actual), expected)
    assert all(count > w*h*.04 for count in interior), (path, interior)
    print(f'PASS {path}: {w}x{h}, interior samples={interior}, colour/orientation/background')


def verify_bundle(path, extended=True):
    verify(path + '.initial.ppm', (320, 240))
    verify(path + '.resized.ppm', (400, 280))
    verify(path, (400, 280), split=True)
    if extended:
        verify(path + '.clear.ppm', (400, 280), clear=True)
        verify(path + '.queued.ppm', (400, 280))
        verify(path + '.updated.ppm', (400, 280), clear=True)
        verify(path + '.range.ppm', (400, 280))
        verify(path + '.range-indexed.ppm', (400, 280))
        for width in (16, 32):
            verify(path + f'.indexed{width}.ppm', (400, 280))


if __name__ == '__main__':
    verify_bundle(sys.argv[1], '--reference' not in sys.argv)
