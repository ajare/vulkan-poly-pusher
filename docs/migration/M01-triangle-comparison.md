# M01 frozen-MPP triangle comparison — #97 / #5 closeout

## Scope and dependencies

#96 already delivered the shared `tools/TrianglePresent.cpp` scene and independent
analytic pixel checks. #97 completes direct **paired** comparison, capture-contract
logging in both clients, negative controls for the pixel oracle, and slice evidence.
The frozen checkout is never patched. Same geometry, logical program/material,
camera, clear, frames and resolutions are specified in
[SCENARIO.md](../comparison/m01-basics/SCENARIO.md#delivered-paired-triangle-comparison-97).
The GL-only public VertexBuffer/VAO binding and capture adapters remain isolated;
VPP draws solely through public rendering APIs. No backend draw test hook is used.

No new library dependencies: Python standard-library comparison/tests; existing
private Vulkan loader, pinned SDL, glslang and SPIRV-Reflect choices remain as
recorded in [clear](M01-vulkan-clear.md), [shader](M01-shader-compilation.md) and
[pipeline](M01-pipeline-realization.md) evidence. GL/GLEW are reference-only.
Public/module/resource contracts and the supported intermediate subset remain
those in [triangle drawing](M01-triangle-draw.md), not a general scene renderer.
Textures/samplers, depth, blending/culling state, instancing, perspective setup,
full scene/model/batch/graph/application rendering remain unsupported and must
fail explicitly. The full textured-overlap Basics scene remains future work.

## Reproduce

Linux host: 2026-10-15, Linux 7.0.0-38-generic, GCC 15.2.0, Release, SDL X11.
Reference HEAD `21b1bcff040a4cfd32e3a5e814b9a4092d2b7cef` and recursive pinned
submodules are unchanged (`git status --short` empty before/after).
Reference CMake configuration enforces the pinned HEAD and clean tracked source.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DVPP_BUILD_RENDERER=ON
cmake --build build --parallel 8
ctest --test-dir build --output-on-failure
cmake -S . -B build/cpu-only -DCMAKE_BUILD_TYPE=Release -DVPP_BUILD_RENDERER=OFF
cmake --build build/cpu-only --parallel 8
ctest --test-dir build/cpu-only --output-on-failure

cmake -S tests/reference -B build/reference \
  -DMPP_REFERENCE_SOURCE=/home/wm/code/mpp-ref \
  -DMPP_REFERENCE_LIBDIR=/home/wm/code/mpp-ref/build/bin/Release
cmake --build build/reference --parallel 8
SDL_VIDEODRIVER=x11 build/reference/MppTrianglePresent build/mpp-triangle.ppm
build/reference/MppCpuTests
python3 tests/compare_assets.py build/bin/VppAssetValidate \
  --reference build/reference/MppAssetValidate

python3 tests/run_triangle_validation.py build/bin/VppTrianglePresent build/97-radv.ppm
python3 tests/compare_triangle.py build/mpp-triangle.ppm --against build/97-radv.ppm
VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json \
  ctest --test-dir build -L gpu --output-on-failure
VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json \
  python3 tests/run_triangle_validation.py build/bin/VppTrianglePresent build/97-lvp.ppm
python3 tests/compare_triangle.py build/mpp-triangle.ppm --against build/97-lvp.ppm

git -C /home/wm/code/mpp-ref diff --exit-code HEAD
git -C /home/wm/code/mpp-ref status --short
```

Run driver CTests **sequentially**: they share output filenames. Explicit paired
runs above use distinct paths so neither driver replaces the other's evidence.
`*.ppm.validation.log` records each validation run; paired commands print sample
counts and maximum channel difference. Captures/logs are generated under `build/`,
not committed goldens. No reference-update command exists in the comparator.

## Recorded results

Both frozen MPP and VPP pass the analytic oracle before direct paired comparison.
Tolerances remain fixed at **2/255 interior**, **1/255 background**, excluding the
original **0.02 barycentric** edge band. Quantization/behavioural reasoning and
colour-space/origin/frame conventions are recorded in SCENARIO.md; these are not
bit-identical or full-silhouette claims.

| Shared capture | Checked pixels | Interior pixels | RADV vs MPP max | lavapipe vs MPP max |
| --- | ---: | ---: | ---: | ---: |
| Frame 19, 320×240 | 72,096 | 17,876 | 1/255 | 1/255 |
| Frame 29, 400×280 | 104,896 | 25,956 | 1/255 | 1/255 |
| Frame 47, split, 400×280 | 108,748 | 6,559 each | 1/255 | 1/255 |

- Renderer Release: **9/9 CTests pass**. CPU-only Release: **5/5 pass**.
- All **4/4 GPU CTests** pass on both Radeon 8060S Graphics (RADV STRIX_HALO)
  and llvmpipe (LLVM 21.1.8, 256 bits). Mandatory Khronos **and synchronization**
  validation reports **zero errors/warnings** for clear, geometry, pipeline and
  triangle execution. The triangle runner also checks every extended VPP capture.
- Source-interface audit passes **258 frozen public headers**; representative
  interfaces compile against both libraries (`VppPublicInterfaces`,
  `MppPublicInterfaces`). No frozen public declarations changed.
- Frozen MPP CPU client: **7/7 suites pass**. Shared asset comparison:
  **74 observations match exactly**. ProgramBuilder and delivered VPP CPU suites
  remain green, including invalid shader/source/binding diagnostics.
- New `triangle_comparison_oracle` CTest: **6 tests pass** with negative controls
  for vertical/horizontal flip, swapped colour attributes, missing geometry,
  wrong split/tint, dimensions/truncation, fixed tolerance boundaries and paired
  errors that individually pass analytic bounds but exceed the paired bound.
- Lifetime checks retained under validation: mesh/program unload/reload and
  in-flight updates, queued geometry copy-on-write, indexed 16/32-bit draws,
  subranges, destruction before `show`, clear discarding queued draws, two draws
  sharing one Program with distinct uniform snapshots, resize and window
  destruction with queued/submitted draws while scene resources remain alive.

## Whole #5 acceptance mapping

| Umbrella requirement | Delivered evidence |
| --- | --- |
| Authoring tokens, Vulkan runtime compilation, reflected bindings, actionable invalid input, runtime editing | [#93](M01-shader-compilation.md), [#95](M01-pipeline-realization.md), current CPU/GPU reruns |
| Public authored geometry and real rendering calls | [#94](M01-geometry-upload.md), [#96](M01-triangle-draw.md) |
| Same frozen-MPP geometry/material, orientation and attributes | Shared client + paired results above; red lower-left, green lower-right, asymmetric blue upper-left in both |
| Native pipelines/descriptors, preserved interfaces/resources/modules | [#95](M01-pipeline-realization.md), unchanged public-header audit and dual client builds |
| Shared scenario/regressions, justified tolerances, no silent reference updates | SCENARIO.md; analytic + direct paired comparator and negative controls above |
| Vulkan/sync validation and resource lifetimes | Zero diagnostics on both Linux drivers, listed lifecycle probes |
| Available Linux/Windows evidence and explicit intermediate subset | Linux evidence above; **Windows unverified**, subset documented above |

The authored-triangle **#5 slice is complete** through #93–#97. This does **not**
qualify all of M01: no Windows runner is available, so Windows build/render/
validation/lifetime results remain **missing and block milestone qualification**.
Reliable compositor minimize/restore remains unqualified (see #4/#92 and
[M01-vulkan-clear.md](M01-vulkan-clear.md)); this work does not relax that test or
claim to fix it. Texture/depth/full-scene work remains outside this slice.
