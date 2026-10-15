#!/usr/bin/env python3
"""Run the public window scenario with real validation; diagnostics fail the test."""
import os
from pathlib import Path
import subprocess
import sys
import threading

env = os.environ.copy()
env['VK_INSTANCE_LAYERS'] = 'VK_LAYER_KHRONOS_validation'
env['VK_LAYER_VALIDATE_SYNC'] = '1'
env['VPP_REQUIRE_VALIDATION'] = '1'
if sys.platform.startswith('linux'):
    # Keep the default X11 seam, but allow explicit Wayland qualification.
    # The client logs the actual driver; X11 on a Wayland session is Xwayland.
    env.setdefault('SDL_VIDEODRIVER', 'x11')

# Stream prompts while preserving diagnostics, including on timeout.
lines = []
with subprocess.Popen(sys.argv[1:], env=env, text=True, stdout=subprocess.PIPE,
                      stderr=subprocess.STDOUT) as run:
    def forward():
        for line in run.stdout:
            lines.append(line)
            print(line, end='', flush=True)

    reader = threading.Thread(target=forward)
    reader.start()
    try:
        run.wait(timeout=240 if '--manual-minimize' in sys.argv else 110)
    except subprocess.TimeoutExpired:
        run.kill()
        run.wait()
        print('window scenario timed out', file=sys.stderr)
    finally:
        reader.join()
        output = ''.join(lines)
        Path(sys.argv[2] + '.validation.log').write_text(output)
if run.returncode or any(token in output for token in
                         ('Validation Error', 'Validation Warning', 'VUID-', 'SYNC-HAZARD')):
    sys.exit(run.returncode or 1)
