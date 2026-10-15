#!/usr/bin/env python3
"""M01 triangle: top-down RGB8 analytic and paired frozen-MPP/VPP checks.

Fixed tolerances: 2/255 interior, 1/255 background; exclude only the same
0.02 barycentric edge band for both checks. Never regenerate a reference here.
"""
import argparse
from pathlib import Path


def image(path):
    with open(path, 'rb') as f:
        assert f.readline() == b'P6\n', path
        w, h = map(int, f.readline().split())
        assert f.readline() == b'255\n', path
        pixels = f.read()
    assert len(pixels) == w * h * 3, path
    return w, h, pixels


def samples(size, split=False, clear=False):
    """Yield non-edge (byte offset, expected RGB8, tolerance, draw index)."""
    w, h = size
    root = Path(__file__).resolve().parents[1]
    vertices = [list(map(float, line.split())) for line in
                (root / 'docs/comparison/m01-basics/assets/Triangle.vertices').read_text().splitlines()]
    draws = [] if clear else ([(-0.5, 0.5, (1, .25, .25)), (.5, .5, (.25, 1, 1))]
                              if split else [(0, 1, (1, 1, 1))])
    background = (0, 0, 0) if clear else (.05, .075, .1)
    for y in range(h):
        for x in range(w):
            px, py = (x + .5) / w * 2 - 1, 1 - (y + .5) / h * 2
            expected, tolerance, edge, selected = background, 1, False, None
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
                        selected = draw
                        expected = tuple(sum(weights[i] * vertices[i][3+channel] for i in range(3)) * tint[channel]
                                         for channel in range(3))
                        tolerance = 2
            if not edge:
                yield (y*w+x)*3, tuple(round(c*255) for c in expected), tolerance, selected


def verify(path, size, split=False, clear=False):
    w, h, pixels = image(path)
    assert (w, h) == size, (path, w, h, size)
    interior = [0] * (0 if clear else 2 if split else 1)
    for offset, expected, tolerance, draw in samples(size, split, clear):
        if draw is not None:
            interior[draw] += 1
        actual = pixels[offset:offset+3]
        assert all(abs(actual[i] - expected[i]) <= tolerance for i in range(3)), (
            str(path), (offset//3 % w, offset//3//w), tuple(actual), expected)
    assert all(count > w*h*.04 for count in interior), (path, interior)
    print(f'PASS {path}: {w}x{h}, interior samples={interior}, colour/orientation/background')


def compare_pair(reference, candidate, size, split=False):
    """Check each against the authored scene AND directly against one another."""
    verify(reference, size, split)
    verify(candidate, size, split)
    _, _, expected = image(reference)
    w, _, actual = image(candidate)
    maximum = 0
    checked = 0
    for offset, _, tolerance, _ in samples(size, split):
        difference = max(abs(actual[offset+i] - expected[offset+i]) for i in range(3))
        assert difference <= tolerance, (
            str(reference), str(candidate), (offset//3 % w, offset//3//w), difference, tolerance)
        maximum = max(maximum, difference)
        checked += 1
    print(f'PASS paired {reference} / {candidate}: samples={checked}, max channel difference={maximum}/255')


def compare_bundles(reference, candidate):
    for suffix, size, split in (('.initial.ppm', (320, 240), False),
                                ('.resized.ppm', (400, 280), False),
                                ('', (400, 280), True)):
        compare_pair(reference + suffix, candidate + suffix, size, split)


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
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('capture', help='capture bundle base path (frozen MPP with --against)')
    parser.add_argument('--reference', action='store_true', help='check only shared captures')
    parser.add_argument('--against', help='VPP bundle to compare directly with frozen MPP')
    args = parser.parse_args()
    if args.against:
        compare_bundles(args.capture, args.against)
    else:
        verify_bundle(args.capture, not args.reference)
