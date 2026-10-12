#!/usr/bin/env python3
"""Observable CPU contracts; optional live comparison against immutable MPP."""
import argparse
from pathlib import Path
import subprocess
import shutil
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('vpp', type=Path)
parser.add_argument('--reference', type=Path)
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
assets = root / 'docs/comparison/m01-basics/assets'
clients = [args.vpp.resolve()]
if args.reference:
    clients.append(args.reference.resolve())
count = 0


def check(kind, source, expected, output=None, numeric=None):
    global count
    observations = []
    for client in clients:
        if output and output.exists():
            output.unlink()
        command = [str(client), kind, str(source)]
        if output:
            command.append(str(output))
        result = subprocess.run(command, capture_output=True, timeout=30)
        assert result.returncode == expected, (command, result.returncode, result.stdout, result.stderr)
        if numeric:
            assert result.stdout.decode().strip() == numeric, (command, result.stdout)
        saved = output.read_bytes() if output and output.exists() else None
        if output:
            assert saved is not None, command
        observations.append((result.returncode, result.stdout, result.stderr, saved))
    assert all(value == observations[0] for value in observations), (source, observations)
    count += 1


with tempfile.TemporaryDirectory(prefix='vpp_cpu_comparison_') as directory:
    temporary = Path(directory)
    assets = shutil.copytree(assets, temporary / 'assets')
    for kind, source in [('scene', assets / 'DefaultPbrPreview.scene.yaml'),
                         ('pbr', assets / 'templates/Minimal.pipeline.yaml'),
                         ('particle', assets / 'steam.particle.yaml')]:
        check(kind, source, 0)
        # Compare exact serialized XML and YAML; then reopen each via the real parser.
        # Relative resource-library paths are retained by the writers, so place
        # pipeline round trips beside the source, not under a different root.
        for extension in ['.xml', '.yaml']:
            output = source.parent / ('cpu-test-roundtrip' + extension)
            try:
                check(kind, source, 0, output)
                check(kind, output, 0)
            finally:
                output.unlink(missing_ok=True)
    for kind, name in [('scene', 'InvalidCamera.scene.xml'),
                       ('scene', 'UndeclaredLayer.scene.xml'),
                       ('pbr', 'InvalidLocalSampler.pipeline.xml'),
                       ('pbr', 'UnknownCoreField.pipeline.xml')]:
        check(kind, assets / name, 1)
    # Every retained named texture format, including the two documented baseline
    # SNORM anomalies. Decimal results are the original legacy API vocabulary.
    formats = {'R8_SNORM': 36756, 'RG8_SNORM': 36757, 'RGB8_SNORM': 36758,
               'RGBA8_SNORM': 36759, 'R16_SNORM': 36761, 'RG16_SNORM': 36762,
               'RGB16_SNORM': 36762, 'RGBA16_SNORM': 36763,
               'R8': 33321, 'RG8': 33323, 'RGB8': 32849, 'RGBA8': 32856,
               'R16': 33322, 'RG16': 33324, 'RGB16': 32852, 'RGBA16': 32859,
               'R16F': 33325, 'RG16F': 33327, 'RGB16F': 34843, 'RGBA16F': 34842}
    for name, value in formats.items():
        source = temporary / 'texture.xml'
        source.write_text(f'<Texture><filename>unused.png</filename><target>2D</target><internalFormat>{name}</internalFormat><minFilter>LINEAR</minFilter><magFilter>NEAREST</magFilter><wrap>CLAMP_TO_EDGE</wrap></Texture>')
        check('texture', source, 0, numeric=f'{value} 3553 9729 9728 33071')
    for name, value in [('NEAREST', 9728), ('LINEAR', 9729),
                        ('NEAREST_MIPMAP_NEAREST', 9984), ('LINEAR_MIPMAP_NEAREST', 9985),
                        ('NEAREST_MIPMAP_LINEAR', 9986), ('LINEAR_MIPMAP_LINEAR', 9987)]:
        for wrap, wrapping in [('REPEAT', 10497), ('MIRRORED_REPEAT', 33648),
                                ('CLAMP_TO_EDGE', 33071), ('CLAMP_TO_BORDER', 33069)]:
            source = temporary / 'sampler.xml'
            source.write_text(f'<Sampler><minFilter>{name}</minFilter><magFilter>LINEAR</magFilter><wrap>{wrap}</wrap></Sampler>')
            check('sampler', source, 0, numeric=f'{value} 9729 {wrapping}')
    for name, value in [('1D', 3552), ('2D', 3553), ('3D', 32879), ('CUBEMAP', 34067)]:
        source = temporary / 'texture.xml'
        source.write_text(f'<Texture><filename>unused.png</filename><target>{name}</target><internalFormat>RGBA8</internalFormat><minFilter>LINEAR</minFilter><magFilter>NEAREST</magFilter><wrap>CLAMP_TO_EDGE</wrap></Texture>')
        check('texture', source, 0, numeric=f'32856 {value} 9729 9728 33071')
    for kind, text in [('texture', '<Texture><target>VULKAN</target></Texture>'),
                       ('sampler', '<Sampler><magFilter>LINEAR_MIPMAP_LINEAR</magFilter></Sampler>'),
                       ('sampler', '<Sampler><Quality/></Sampler>'),
                       ('texture', '<Texture><target>2D</target></Texture>'),
                       ('scene', '<Scene><Camera><position>not a vector</position></Camera></Scene>'),
                       ('particle', '<ParticleEffect><UnexpectedField/></ParticleEffect>')]:
        source = temporary / 'invalid.xml'
        source.write_text(text)
        check(kind, source, 1)
    check('render', assets / 'DefaultPbrPreview.scene.yaml', 2)
print(f'PASS {count} authored-resource observations' + (' match frozen MPP exactly' if args.reference else ' (local assertions)'))
