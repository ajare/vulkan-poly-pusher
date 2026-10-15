#!/usr/bin/env python3
"""CPU-only negative controls for the shared comparison scene's pixel oracle."""
import contextlib
import io
from pathlib import Path
import tempfile
import unittest

from compare_triangle import compare_pair, image, samples, verify


class TriangleComparisonTests(unittest.TestCase):
    size = (80, 60)

    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.reference = Path(self.directory.name) / 'reference.ppm'
        self.candidate = Path(self.directory.name) / 'candidate.ppm'
        self.output = contextlib.redirect_stdout(io.StringIO())
        self.output.__enter__()
        self.addCleanup(self.output.__exit__, None, None, None)

    def write(self, path, pixels, size=None):
        w, h = size or self.size
        path.write_bytes(f'P6\n{w} {h}\n255\n'.encode() + pixels)

    def scene(self, split=False):
        pixels = bytearray(self.size[0] * self.size[1] * 3)
        for offset, rgb, _, _ in samples(self.size, split):
            pixels[offset:offset+3] = bytes(rgb)
        return pixels

    def test_identical_and_quantized_pair(self):
        pixels = self.scene()
        self.write(self.reference, pixels)
        for offset, _, tolerance, _ in samples(self.size):
            pixels[offset] += tolerance
        self.write(self.candidate, pixels)
        compare_pair(self.reference, self.candidate, self.size)

    def test_pair_catches_opposite_rounding_errors(self):
        # Both can pass the analytic +/-2 bound yet disagree by 4. The direct
        # pair check must NOT implicitly double the tolerance to 4/255.
        left, right = self.scene(), self.scene()
        offset = next(o for o, rgb, _, draw in samples(self.size)
                      if draw == 0 and 10 < rgb[0] < 245)
        left[offset] -= 2
        right[offset] += 2
        self.write(self.reference, left)
        self.write(self.candidate, right)
        verify(self.reference, self.size)
        verify(self.candidate, self.size)
        with self.assertRaises(AssertionError):
            compare_pair(self.reference, self.candidate, self.size)

    def test_flips_and_swapped_attributes_rejected(self):
        pixels = self.scene()
        w, h = self.size
        variants = [b''.join(pixels[y*w*3:(y+1)*w*3] for y in reversed(range(h))),
                    b''.join(pixels[(y*w+x)*3:(y*w+x+1)*3]
                             for y in range(h) for x in reversed(range(w))),
                    b''.join(bytes((pixels[i+2], pixels[i+1], pixels[i]))
                             for i in range(0, len(pixels), 3))]
        for data in variants:
            with self.subTest(variant=variants.index(data)):
                self.write(self.candidate, data)
                with self.assertRaises(AssertionError):
                    verify(self.candidate, self.size)

    def test_missing_geometry_and_wrong_tint_rejected(self):
        for data, split in ((bytes((13, 19, 26)) * (80*60), False),
                            (self.scene(), True)):
            self.write(self.candidate, data)
            with self.assertRaises(AssertionError):
                verify(self.candidate, self.size, split)
        self.write(self.candidate, self.scene(split=True))
        verify(self.candidate, self.size, split=True)

    def test_fixed_bounds_and_edge_exclusion(self):
        for draw in (None, 0):
            pixels = self.scene()
            offset, _, tolerance, _ = next(s for s in samples(self.size) if s[3] == draw)
            pixels[offset] += tolerance + 1
            self.write(self.candidate, pixels)
            with self.assertRaises(AssertionError):
                verify(self.candidate, self.size)
        pixels = self.scene()
        included = {o for o, *_ in samples(self.size)}
        for offset in range(0, len(pixels), 3):
            if offset not in included:
                pixels[offset:offset+3] = b'\xff\xff\xff'
        self.write(self.candidate, pixels)
        verify(self.candidate, self.size)

    def test_wrong_size_and_truncated_capture_rejected(self):
        self.write(self.candidate, bytes(3), (1, 1))
        with self.assertRaises(AssertionError):
            verify(self.candidate, self.size)
        self.write(self.candidate, bytes(3))
        with self.assertRaises(AssertionError):
            image(self.candidate)


if __name__ == '__main__':
    unittest.main()
