# M01 — CPU authored assets without OpenGL (#3)

This is an intermediate subset, **not Milestone 1 qualification**. No renderer,
window, device, swapchain or graphics context exists in this build.

## Build and run

Linux (verified GCC 15.2, CMake, Python 3.14):

```sh
git submodule update --init --recursive
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
build/bin/VppAssetValidate scene docs/comparison/m01-basics/assets/DefaultPbrPreview.scene.yaml
build/bin/VppAssetValidate pbr docs/comparison/m01-basics/assets/templates/Minimal.pipeline.yaml
build/bin/VppAssetValidate particle docs/comparison/m01-basics/assets/steam.particle.yaml /tmp/steam.xml
```

Requires C++20, Python (tests only), pinned Utils and recursive yaml-cpp. Utils
fetches hash-pinned Valijson 1.1.2 and nlohmann/json 3.11.3 at configure time.
There is **no OpenGL/GLEW, SDL, Assimp or Vulkan configure/link dependency**.
The vendored GLM/half/STB headers retain their upstream license notices.

Windows build recipe (**unexecuted; no Windows runner**):

```bat
git submodule update --init --recursive
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
build\bin\Release\VppAssetValidate.exe scene docs\comparison\m01-basics\assets\DefaultPbrPreview.scene.yaml
```

## Supported targets and operations

All eight original module directories, include identities and target names remain:

| Target | Intermediate implementation |
| --- | --- |
| `MppData` | Original structured data |
| `MppMesh` | Original mesh definitions/layouts |
| `MppProgram` | Original shader markup parser, not GPU compilation |
| `MassivePolyPusher` | Documents, diagnostics, graph compiler/metadata, uniform data, CPU resource streams, camera/geometry helpers, serialization and CPU tests |
| `MppResourceParsers` | Scene, PBR/legacy pipeline, graph, particle XML/YAML parsers/writers; file material/program/texture/sampler/particle/string streams; embedded/external resource-definition validation |
| `MppMeshSpecificationParser` | Original specification parser and ModelspecStream |
| `MppHelper` | Original FPS/free/orthographic cameras |
| `MppAppSupport` | Original background work, command stack, document file/ID, package manifest and ZIP code; no window/input/ImGui platform layer |
| `ProgramBuilder` | Original context-free command-line tool |
| `VppAssetValidate` | Shared comparison client, document validation and optional round-trip writing |
| `VppCpuTests`, `VppPublicInterfaces` | CPU regressions and source-interface compilation |

Validator operations: `scene`, `legacy`, `pbr`, `particle`, `texture`, `sampler`.
Exit 0 means document/definition validation succeeded, **not** that rendering or
GPU resource creation succeeded. Exit 1 means parser/validation failure; exit 2
means usage or unsupported operation. Optional output writes XML/YAML for the four
document operations only. Diagnostic codes/severities/object IDs/messages are
retained. Texture/sampler mode prints legacy numeric values; it validates resource
definitions, **not image bytes or GPU format support**. Engine resource-manager
lookup and GPU-dependent capabilities are intentionally outside this subset.
Capability-free document validation does not claim a device supports the document.

The original renderer sources are retained for subsequent migration but **excluded
from the build**, not supplied as successful stubs. No `RenderSystem`, GPU resource,
pipeline execution, editor UI, DemoSuite or ModelConvert implementation is linked.
Unsupported symbols outside the subset fail at link time. Available crossings
fail with an explicit `not-yet-delivered Vulkan` exception: uniform binding,
child-resource realization and built-in GPU scene-pass factory creation. Enabling
`VPP_BUILD_RENDERER` fails configure explicitly. Built-in pass *metadata* is retained
for document validation; its factories throw rather than return dummy passes.

## Provenance and narrow decoupling

Module trees, ProgramBuilder and vendor headers were copied from immutable MPP
`21b1bcff040a4cfd32e3a5e814b9a4092d2b7cef`; the original checkout is untouched.
Only **13 imported implementation files** differ:

- Numeric-only GL include replacement in `TextureParams`, `SamplerParams`,
  `ProgrammaticTextureStream`, `ProgrammaticSamplerStream`, `MppException`,
  `FileTextureStream`, `FileSamplerStream`, `RenderGraphParser`, `RenderGraphSerializer`.
  Private `compat/LegacyGpuValues.h` contains the original numbers, no GL entry
  points. No Vulkan enums replace authored legacy values.
- `TextureData.cpp`: remove unused GL includes; explicitly include standard integer
  and size types. No public declaration change.
- `UniformCollection.cpp`: retained data operations, but binding throws.
- `ResourceStream.cpp`: retained load/unload/child-stream behaviour; resource
  realization/ownership operations throw.
- `FileProgramStream.cpp`: non-null engine shader-resource lookup throws. Parsing
  a logical reference with a null manager remains unchanged.

`compat/CpuDefinitions.cpp` extracts unchanged CPU view math, PBR shadow/culling
classification and curve-cache invalidation from mixed GPU files. It retains
built-in metadata, with throwing GPU factories. `CpuResourceBoundary.cpp` provides
throwing resource-manager crossings used by retained streams. Neither is a
renderer. When compiling the mixed files in later slices, move these extracted
CPU definitions out of those files to avoid duplicate definitions.

**All 258 public headers are byte-identical**, across all eight modules; the
frozen SHA-256 manifest is audited by CTest, without a local reference dependency.
`PublicInterfaces.cpp` additionally compiles representative existing client
usage against both VPP and MPP. This is declaration/source compatibility evidence,
not proof that every declared GPU API is implemented in this intermediate subset.
No compatibility exceptions or schema changes were introduced.

The two texture SNORM anomalies remain unchanged (`R16_SNORM` maps to legacy
RG16_SNORM; `RG16_SNORM` maps to legacy RGB16_SNORM). No reference update or silent
baseline fix was made.

## Shared comparison client and regressions

Build the exact same CPU client and regression runner against the read-only
reference's already-built Linux shared libraries (does not configure or edit MPP):

```sh
cmake -S tests/reference -B build/reference \
  -DMPP_REFERENCE_SOURCE=/home/wm/code/mpp-ref \
  -DMPP_REFERENCE_LIBDIR=/home/wm/code/mpp-ref/build/bin/Release
cmake --build build/reference --parallel
build/reference/MppCpuTests
python3 tests/compare_assets.py build/bin/VppAssetValidate \
  --reference build/reference/MppAssetValidate
```

The reference adapter currently targets Linux shared-library naming; Windows
reference comparison requires a real runner and imported DLL/import-library
configuration. It has **not** been validated on Windows.

Shared assets are unchanged copies of the frozen PBR preview scene, Minimal
pipeline and resource library, SteamVent particle document, and four invalid
editor fixtures. They extend the M01 comparison scenario's CPU portion; they do
not replace the future textured-geometry visual scenario.

The Python check uses temporary copies, invokes both clients with the **same
paths**, compares exit code and exact stdout/stderr, compares exact XML/YAML
serialization bytes, and reopens the output. It checks all 20 authored texture
formats, all six sampler min-filter names, four wrapping names and four targets,
plus malformed/invalid resources and unsupported operations. No visual tolerance
is involved: these CPU observations are exact.

### Recorded Linux results

- Release build: all eight CPU module targets + ProgramBuilder + clients pass.
- Independent fresh Debug configure/build (normal pinned dependency downloads): pass.
- CTest: **4/4 pass** in Release (1.54 s) and Debug (2.14 s), with `DISPLAY` and
  `WAYLAND_DISPLAY` unset.
- Reference regression runner: RSE5 binary sampler/child-string byte round trip,
  diagnostics, graph topology/view math, PBR specialization, XML/YAML graph/scene/
  PBR/legacy round trips, particle resources, and application document foundations
  all pass. Same seven suites pass on VPP; VPP additionally rejects unsupported
  binding/resource/pass creation.
- **74 authored-resource observations match frozen MPP exactly**, including negative
  diagnostic behaviour and both SNORM anomalies.
- 258-header audit passes; representative client declaration compilation passes
  against both libraries.
- ProgramBuilder valid test: exit 0 on both. Its original `test.modelspec` fixture
  uses unsupported `uint8`, returning `Invalid type: uint8` on **both** (exit 1).
  It remains unchanged. The separate `tests/fixtures/program.modelspec` explicitly
  uses the parser's existing `uint8_t` spelling; this is a new test, not a schema fix.
- Renderer-enabled configure fails as intended (exit 1).
- `ldd build/bin/VppAssetValidate` contains MPP CPU modules/Utils and standard
  runtimes only; no GL/GLEW, SDL or Vulkan libraries. Linux shared module linking
  also enforces `--no-undefined`.
- Release CPU runner sample: elapsed 1.12 s, maximum RSS 12,276 KiB (`/usr/bin/time`).
  This is a CPU test process measurement, not renderer frame-time evidence.
- Frozen source remains clean at the pinned SHA after all checks.

### Qualification gaps

**Windows results are missing and still block Milestone 1 qualification.**
Vulkan/synchronization validation, GPU/application rendering regressions, visual
captures, in-flight lifetimes, presentation and resize are not applicable to this
CPU-only ticket and remain required of the rendering slice. No GPU pass or
Milestone 1 sign-off is inferred from these tests. The reference's previously
recorded GL GPU smoke results remain in `M01-smoke-evidence.md`; this change does
not alter the reference renderer.
