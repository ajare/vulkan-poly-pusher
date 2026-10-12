# M01 — Reproducing the frozen MPP reference

The frozen reference is `ajare/massive-poly-pusher`, branch `vulkan-migration-target`,
commit `21b1bcff040a4cfd32e3a5e814b9a4092d2b7cef`. It must be reproduced **without
changing that branch or its checkout**. The reference is an OpenGL renderer; this
document covers reproducing it as the comparison baseline, not building VPP.

## Dependencies

- CMake ≥ 3.22 (a C++20 toolchain), git, git-lfs.
- Linux: OpenGL development files (`libgl-dev`), a C++20 compiler.
- Windows: Visual Studio 2022/2026 (MSVC) with the Desktop C++ workload.
- Submodules (pinned): `ext/assimp`, `ext/glew`, `ext/sdl`, `ext/utils`
  (`ext/utils` further pins `vendor/yaml-cpp`).
- GLEW: the GLEW git repo omits generated headers/`glew.c`; the first CMake
  configure downloads the hash-verified official source archive matching the pinned
  release. This requires network access at configure time.
- Large assets use Git LFS. The demo package and textures are LFS pointers until
  `git lfs pull` runs; a pointer-only package makes DemoSuite report
  `Package ZIP directory is missing`.

## Linux reproduction (verified on this host)

```sh
# 1. Clone the frozen reference at the pinned commit, read-only.
git init mpp-ref && cd mpp-ref
git remote add origin https://github.com/ajare/massive-poly-pusher.git
git fetch --depth 1 origin 21b1bcff040a4cfd32e3a5e814b9a4092d2b7cef
git checkout FETCH_HEAD
git rev-parse HEAD   # -> 21b1bcff040a4cfd32e3a5e814b9a4092d2b7cef

# 2. Pinned submodules (recursive: pulls utils/vendor/yaml-cpp).
git submodule update --init --recursive

# 3. Large assets.
git lfs install
git lfs pull

# 4. Configure + build (Release).
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel

# Binaries land in build/bin/Release:
#   DemoSuite  PipelineEditor  ParticleEditor  ProgramBuilder  ModelConvert
```

Smoke and test commands are recorded in
[`M01-smoke-evidence.md`](M01-smoke-evidence.md).

## Windows reproduction (NOT yet run — no Windows runner)

```bat
:: Clone the frozen reference at the pinned commit (same SHAs as above).
git clone https://github.com/ajare/massive-poly-pusher.git mpp-ref
cd mpp-ref
git checkout 21b1bcff040a4cfd32e3a5e814b9a4092d2b7cef
git submodule update --init --recursive
git lfs install
git lfs pull

:: Configure + build with Visual Studio 2026 x64.
cmake -S . -B build -G "Visual Studio 18 2026" -A x64
cmake --build build --config Release --parallel
:: Binaries land in build\bin\Release
```

> ⛔ **This Windows path has not been executed.** The current environment has no
> Windows host or Windows CI runner. Until a real Windows build + smoke run is
> recorded, Windows evidence is **missing** and blocks Milestone 1 qualification
> (spec #1: "Windows results must come from a real Windows runner. Missing Windows
> or validation evidence blocks milestone sign-off rather than being treated as a
> pass."). Provisioning a Windows runner (or Windows CI job) is the follow-up that
> closes this gap.

## Vulkan validation layer (NOT present on this host)

The M01 acceptance criteria require Vulkan + synchronization-validation results for
rendering work. This host's `vulkaninfo` lists only Mesa/Intel layers
(`VK_LAYER_MESA_*`, `VK_LAYER_INTEL_nullhw`); **`VK_LAYER_KHRONOS_validation` is
absent**. This matches the planning note in spec #1.

To close the gap before validation-based sign-off:

```sh
# Install the Khronos Vulkan SDK (provides VK_LAYER_KHRONOS_validation), then:
vulkaninfo 2>/dev/null | grep VK_LAYER_KHRONOS_validation   # must be listed
VK_LAYER_ENABLE=VK_LAYER_KHRONOS_validation ./<app> ...    # re-run scenarios under validation
```

VPP rendering validation (Vulkan + `VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION`
/ `VK_KHR synchronization validation`) is deferred to the milestone that first
produces a VPP renderer; the layer must be installed and verified before that
sign-off.

## Notes

- The reference checkout lives outside this repository (here: `/home/wm/code/mpp-ref`)
  and is never committed into VPP.
- Do not modify the frozen branch to make a comparison pass. If the reference and
  VPP disagree, record the difference in
  [`M01-deviations-and-defects.md`](M01-deviations-and-defects.md).
