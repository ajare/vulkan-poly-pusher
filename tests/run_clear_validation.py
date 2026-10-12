#!/usr/bin/env python3
"""Run the public window scenario with real validation; diagnostics fail the test."""
import os
from pathlib import Path
import subprocess
import sys

env = os.environ.copy()
env['VK_INSTANCE_LAYERS'] = 'VK_LAYER_KHRONOS_validation'
env['VK_LAYER_VALIDATE_SYNC'] = '1'
env['VPP_REQUIRE_VALIDATION'] = '1'
if sys.platform.startswith('linux'):
    # GNOME Wayland refuses programmatic restore on this host. X11 is the
    # qualified scripted window seam; do not misrepresent this as Wayland proof.
    env['SDL_VIDEODRIVER'] = 'x11'
run = subprocess.run(sys.argv[1:], env=env, text=True, stdout=subprocess.PIPE,
                     stderr=subprocess.STDOUT, timeout=110)
print(run.stdout, end='')
Path(sys.argv[2] + '.validation.log').write_text(run.stdout)
if run.returncode or any(token in run.stdout for token in
                         ('Validation Error', 'Validation Warning', 'VUID-', 'SYNC-HAZARD')):
    sys.exit(run.returncode or 1)
