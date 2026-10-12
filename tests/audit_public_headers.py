#!/usr/bin/env python3
"""Strict include/declaration preservation against frozen 21b1bcff (no exceptions)."""
import hashlib
import json
from pathlib import Path
import sys

root = Path(sys.argv[1])
manifest = json.loads((root / 'tests/frozen-public-headers.json').read_text())
errors = [name for name, digest in manifest.items()
          if not (root / name).is_file()
          or hashlib.sha256((root / name).read_bytes()).hexdigest() != digest]
if errors:
    sys.exit('Changed/missing frozen public headers:\n' + '\n'.join(errors))
print(f'PASS {len(manifest)} frozen public headers byte-identical (all eight modules)')
