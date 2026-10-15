#!/usr/bin/env python3
"""Require real Vulkan + synchronization validation and verify captured pixels."""
import os
from pathlib import Path
import subprocess
import sys
from compare_triangle import verify_bundle

env = os.environ.copy()
env.update(VK_INSTANCE_LAYERS='VK_LAYER_KHRONOS_validation', VK_LAYER_VALIDATE_SYNC='1',
           VPP_REQUIRE_VALIDATION='1')
if sys.platform.startswith('linux'):
    env['SDL_VIDEODRIVER'] = 'x11'
run = subprocess.run(sys.argv[1:], env=env, text=True, stdout=subprocess.PIPE,
                     stderr=subprocess.STDOUT, timeout=110)
print(run.stdout, end='')
Path(sys.argv[2] + '.validation.log').write_text(run.stdout)
if run.returncode or any(token in run.stdout for token in
                         ('Validation Error', 'Validation Warning', 'VUID-', 'SYNC-HAZARD')):
    sys.exit(run.returncode or 1)
verify_bundle(sys.argv[2])
