#!/usr/bin/env python3
"""Frozen interface audit: exact bytes except explicitly listed private storage.

Do not regenerate the manifest: normalization reverses only the documented
Vulkan private representation changes, preserving every frozen public declaration.
Additive headers are compiled by the comparison client.
"""
import hashlib
import json
from pathlib import Path
import sys

root = Path(sys.argv[1])
manifest = json.loads((root / 'tests/frozen-public-headers.json').read_text())
def frozen_bytes(name):
    path = root / name
    if not path.is_file():
        return b''
    data = path.read_bytes()
    if name == 'mpp/include/mpp/RenderSystem.h':
        data = data.replace(b'\tnamespace detail { class VulkanWindow; }\n\tstruct BackendCapabilities;\n', b'')
        data = data.replace(
            b'\t\tstd::shared_ptr<detail::VulkanWindow> mVulkanWindow;\n'
            b'\t\tfriend _MPPAPI BackendCapabilities getBackendCapabilities(RenderSystem const&);\n'
            b'\t\tfriend _MPPAPI void captureNextPresent(RenderSystem&, std::string const&);',
            b'\t\tstd::unique_ptr<ParticleSystem> mParticleSystem;')
        data = data.replace(b'\t\t// TrailSystem ownership is introduced with the trails rendering slice.',
                            b'\t\tstd::unique_ptr<TrailSystem> mTrailSystem;')
    return data

errors = [name for name, digest in manifest.items()
          if hashlib.sha256(frozen_bytes(name)).hexdigest() != digest]
if errors:
    sys.exit('Changed/missing frozen public headers:\n' + '\n'.join(errors))
print(f'PASS {len(manifest)} frozen public headers (only enumerated private Vulkan storage normalized)')
