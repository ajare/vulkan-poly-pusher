# M01 — Reference build & smoke evidence (Linux)

All commands below were run against the frozen reference commit
`21b1bcff040a4cfd32e3a5e814b9a4092d2b7cef` on this host. Results are recorded
verbatim. **Windows and Khronos-validation evidence are missing and are reported as
blocking, not passed.**

## Environment fingerprints

| Component | Value |
| --- | --- |
| OS | Ubuntu 26.04.1 LTS |
| Kernel | 7.0.0-38-generic |
| Compiler | gcc (Ubuntu 15.2.0-16ubuntu1) 15.2.0 |
| CMake | 4.2.3 |
| Shader compiler | shaderc 2026.1-1 (`glslc`) |
| git-lfs | 3.7.1 |
| GPU | Radeon 8060S Graphics (RADV STRIX_HALO), integrated |
| Vulkan API / driver | 1.4.335 / 26.0.8 |
| Fallback Vulkan | llvmpipe (LLVM 21.1.8, 256 bits), CPU device |
| Frozen commit | `21b1bcff040a4cfd32e3a5e814b9a4092d2b7cef` |
| Submodules | assimp `c35200e3`, glew `285c35d3` (2.3.1), sdl `147a8ee3`, utils `eae7140c7` (yaml-cpp `c3f81127`) |
| `VK_LAYER_KHRONOS_validation` | **absent** (only Mesa/Intel layers listed) |

## Build

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release   # configure OK (GLEW archive hash-verified + downloaded)
cmake --build build --parallel                  # BUILD_EXIT=0
```

Produced in `build/bin/Release`: `DemoSuite`, `PipelineEditor`, `ParticleEditor`,
`ProgramBuilder`, `ModelConvert` (+ shared libs, `workspace.mpppackage`, ini files).

## Smoke + delivered tests (Linux)

| # | Command | Result |
| --- | --- | --- |
| 1 | `PipelineEditor --validate resources/shared/pbr/templates/Minimal.pipeline.yaml` | exit 0 (valid) |
| 2 | `ParticleEditor --document-tests` | `Particle Editor document tests passed.` exit 0 |
| 3 | `ParticleEditor --resource-tests` | `Particle Editor resource tests passed.` exit 0 |
| 4 | `ParticleEditor --preview-tests` | `Particle Editor preview tests passed.` exit 0 |
| 5 | `ParticleEditor --core-particle-tests` | `Production particle resource and CPU tests passed.` exit 0 |
| 6 | `ParticleEditor --spatial-tests` | `Particle Editor spatial tests passed.` exit 0 |
| 7 | `ParticleEditor --control-tests` | `Particle Editor control tests passed.` exit 0 |
| 8 | `PipelineEditor --gpu-tests` | `Render graph GPU tests passed.` exit 0 (Radeon 8060S) |
| 9 | `DemoSuite --package ./workspace.mpppackage --package-smoke-test` | exit 0 (render loop ran to 30 frames, `frameCount>=30` → clean exit) |
| 10 | `ctest --output-on-failure` | `100% tests passed, 0 tests failed out of 2` (`mpp_render_graph_cpu`, `mpp_render_graph_gpu`) |

Smoke semantics verified in source: `demo-suite/src/Main.cpp:626`
`if (gPackageSmokeTest && frameCount>=30) running=false;` — the smoke path runs the
real per-frame `update`/`renderToScreen` loop and exits 0 only on success (errors
set `exitCode=1`).

## What this evidence covers

- The frozen reference reproduces from the pinned commit + pinned submodules + LFS
  assets on Linux, and its delivered CPU, GPU, and integration tests pass on real
  AMD GPU hardware.
- The reference is a runnable baseline for the shared comparison scenes.

## What this evidence does NOT cover (blocking gaps)

1. **Windows** — no Windows host/runner was available. The Windows build + smoke
   path in [M01-reference-setup.md](M01-reference-setup.md) is unexecuted.
   ⛔ Blocks Milestone 1 qualification (spec #1).
2. **Khronos Vulkan validation layer** — `VK_LAYER_KHRONOS_validation` is not
   installed on this host, so no Vulkan/synchronization-validation results exist.
   ⛔ Blocks validation-based sign-off. Note: the frozen reference is an OpenGL
   renderer, so Vulkan validation applies to VPP rendering once it exists; the
   layer must be installed and verified before that milestone's sign-off.
3. **Visual capture tooling** — no screenshot/grab tool (`grim`, `scrot`,
   `import`, `spectacle`, `gnome-screenshot`) is installed on this host, so no
   pixel captures were taken here. The M01 comparison-scenario contract
   ([`../comparison/m01-basics/SCENARIO.md`](../comparison/m01-basics/SCENARIO.md))
   specifies capture conventions; a capture path must be provisioned for VPP
   visual comparison.

These gaps are reported rather than treated as success, per the ticket:
"missing infrastructure is reported rather than treated as success."
