#!/usr/bin/env python3
"""Compare frozen MPP/VPP clear captures; references are never rewritten."""
from pathlib import Path
import sys

for suffix in ('', '.initial.ppm'):
    a = Path(sys.argv[1] + suffix).read_bytes().split(b'\n', 3)
    b = Path(sys.argv[2] + suffix).read_bytes().split(b'\n', 3)
    if a[:3] != b[:3] or a[0] != b'P6' or a[2] != b'255' or len(a[3]) != len(b[3]):
        sys.exit('Capture format/dimensions differ')
    difference = max(abs(x-y) for x, y in zip(a[3], b[3]))
    if difference > 1:
        sys.exit(f'Clear comparison exceeds the documented 1/255 tolerance: {difference}')
    print(f'PASS {a[1].decode()} max channel difference={difference}/255')
