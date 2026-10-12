# M01 — Vulkan clear/present slice (#4)

## Supported subset and ownership

The original public startup sequence is runnable: initialize SDL video,
`WindowSDL::create(width,height,fullscreen,vsync)`, construct `mpp::RenderSystem`,
`clearScreen(colour)`, `WindowSDL::show()`, destroy renderer/window, SDL quit.
`setDisplay`, full-window `resetViewport`/`setViewport`, frame statistics and
window/input/timer support are delivered. SDL owns the native window; a private
association owns instance, surface, device, graphics/present queue and swapchain.
The renderer holds shared lifetime state, not native handles in its public API.
Window destruction retires work and destroys the Vulkan surface before SDL's
window, even if the renderer is still alive; later rendering fails explicitly.
One live window is required by standard startup; ambiguous/missing associations
fail. These APIs are render-thread-only. Explicit custom-window integration is
not delivered in this narrow intermediate subset.

Two fence-protected frame contexts own command pools/buffers and acquire
semaphores. Present semaphores belong to swapchain images, so a signalled frame
fence is never mistaken for presentation completion. Transfer clears have
explicit acquire/transition/clear/present synchronization. Routine frames do
not call device-wide idle; idle is restricted to exceptional swapchain retirement
and shutdown. Capture waits only for the captured submission. A presentation
failure closes the backend rather than reusing half-submitted work.

Required device capabilities: Vulkan >=1.3, `VK_KHR_swapchain`, a combined
native graphics/present queue, surface transfer-source/destination usage, and
BGRA8/RGBA8 UNORM with SRGB_NONLINEAR presentation. No optional device features
are needed/enabled. Missing requirements get named diagnostics, not GL fallback.
Vsync selects FIFO; disabled vsync prefers mailbox/immediate, falling back to FIFO
where required. Minimized/zero-sized windows skip presentation; size changes and
out-of-date acquisition rebuild the swapchain.

`getCaps()` preserves its declarations but reports GL/GLSL versions zero and no
compatibility context. Additive `mpp/BackendCapabilities.h` reports backend,
physical-device name/API version, clear support and explicitly unavailable shader
compilation/dialect. Hardware compute limits are not advertised as delivered
renderer functionality. Only single-sample clears are supported; requested AA is
rejected. Shader/material/geometry/text/particles/graphs/resource realization
remain unavailable. Retained rendering entry points throw operation-specific
errors, not successful no-ops; unsupported `noexcept` flow hooks print a diagnostic
and terminate (they cannot throw). Unported resource classes are still outside the
linkable runtime subset. This is not a complete M01 rendering slice or application
port; existing CPU tests and ProgramBuilder remain usable.

Public declaration audit keeps the original 258-header manifest unchanged. It
reverses only enumerated private `RenderSystem` storage/forward/friend changes;
all frozen public declarations and include identities still have their original
bytes. `WindowSDL` keeps its public signatures and its unused null GL-context
storage for now; it makes no GL calls. Vulkan types/headers/libraries are private.
SDL remains visible through the *already SDL-dependent* WindowSDL interface.

## Dependency/build choices

- SDL **3.4.14**, submodule **147a8ee32dbf9ac02f3794964490687b6bbda1bc**,
  exactly the frozen MPP revision; zlib license (`ext/sdl/LICENSE.txt`). CMake
  builds the shared library from this pin, tests disabled, platform video drivers
  detected by SDL. No checkout of frozen MPP is changed.
- Vulkan system SDK/loader: require >=1.3; tested headers/loader and Khronos
  validation **1.4.341** on this Linux host. Native platform SDK, not vendored or
  driver-pinned; installations of other SDK versions need requalification.
  Vulkan Headers/Loader and Validation Layers: Apache-2.0. No VMA, shader compiler
  or reflection dependency is introduced for a transfer-only slice.
- Existing utils/yaml-cpp/vendor pins remain unchanged. C++20, CMake >=3.22,
  GCC 15.2.0, Release. `-DVPP_BUILD_RENDERER=OFF` retains the CPU-only option.

## Shared comparison scenario and commands

`tools/ClearPresent.cpp` builds unchanged against both libraries; backend-specific
capability checks and capture adapters are isolated with `VPP_VULKAN`. The frozen
source checkout is only read. Three 60-frame cycles use 320x240 at frame 29 with
clear `(0.2,0.4,0.6,1)`, then resize to 400x280 at frame 30 and capture frame 59
with `(0.6,0.2,0.4,1)`. The client resets the full viewport after `setDisplay`,
which is required by the reference's scissored clear. Vsync on/off are exercised;
shutdown order alternates, with GPU work in flight. The Vulkan variant additionally
checks missing/ambiguous windows, show-before-init, unsupported AA/text and stale
rendering after window destruction. No assertions inspect private Vulkan layout.

Captures: top-left P6 RGB PPM, 8-bit UNORM channel values, no tonemapping or sRGB
conversion. MPP reads the GL back buffer before swap and reverses rows; VPP copies
the cleared image before presentation. Expected-colour and paired-channel tolerance
is **1/255**, justified by 8-bit quantization rounding, not changed to hide errors.
Both initial and resized captures must match. This clear-only scenario extends
Basics; it is not evidence for geometry/depth/UV/winding.

```sh
git submodule update --init --recursive
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DVPP_BUILD_RENDERER=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
# Mandatory layer + sync validation; saves <capture>.validation.log.
python3 tests/run_clear_validation.py build/bin/VppClearPresent build/vpp-clear.ppm
VK_DRIVER_FILES=/usr/share/vulkan/icd.d/lvp_icd.json \
  python3 tests/run_clear_validation.py build/bin/VppClearPresent build/lvp-clear.ppm

cmake -S tests/reference -B build/reference \
  -DMPP_REFERENCE_SOURCE=/home/wm/code/mpp-ref \
  -DMPP_REFERENCE_LIBDIR=/home/wm/code/mpp-ref/build/bin/Release
cmake --build build/reference --parallel
SDL_VIDEODRIVER=x11 build/reference/MppClearPresent build/mpp-clear.ppm
python3 tests/compare_clear.py build/mpp-clear.ppm build/vpp-clear.ppm
# Separate interactive-compositor qualification scenario; may fail here:
python3 tests/run_clear_validation.py build/bin/VppClearPresent build/min-clear.ppm --minimize
```

The reference harness checks the immutable SHA and clean tracked source before
building. Its Linux imported SDL/GLEW library and pinned GLEW generated-header
paths use the documented default reference build location. It is not a Windows
reference harness. Windows VPP build uses the usual CMake/MSVC generator plus
Vulkan SDK; no Windows build/run has been observed.

## Recorded Linux results (2026-10-12)

- Release build succeeds; retained SDL input enum-arithmetic deprecation warnings
  are compiler warnings from unchanged code, not Vulkan validation diagnostics.
- **5/5 CTest pass**: clear/present under mandatory Khronos + sync validation,
  public declarations, CPU regressions, authored assets, ProgramBuilder. Separate
  renderer-disabled Release build also passes **4/4** CPU-only CTest tests.
- RADV **Radeon 8060S Graphics (STRIX_HALO)** and Mesa **llvmpipe (LLVM 21.1.8)**:
  180 frames, three lifetime cycles each; **zero Vulkan/sync validation errors or
  warnings** in the clear/resize/shutdown scenario. Linux GPU tests select X11.
- Frozen MPP shared client: 180 frames and captures pass. Both capture sizes match
  VPP with **maximum channel difference 0/255**. Reference CPU regressions pass;
  **74 authored-resource observations** still match frozen MPP exactly.
- Representative elapsed wall time, including window setup delays and diagnostic
  captures: VPP RADV 2.82 s (CTest), lavapipe 2.57 s, frozen MPP 2.73 s. These are
  smoke measurements, **not renderer performance comparisons**. Raw capture
  storage is 320x240x4 / 400x280x4 bytes during diagnostics; driver/native memory
  and GPU timings are not measured or qualified in this slice.
- Initial sync validation caught an acquire/transition hazard; corrected the
  transition source-stage chain to TRANSFER and reran both devices cleanly. No
  validation suppression or tolerance changes were used.
- **Minimize/restore is not qualified:** GNOME Wayland refused programmatic restore;
  X11 runs also intermittently leave SDL reporting MINIMIZED after restore/raise.
  One earlier 180-frame X11 run completed, but repeated runs failed the explicit
  restore assertion, so this is not reported as a pass. `--minimize` retains a
  failing-on-unrestored-window test rather than silently skipping it. Ordinary
  clear/resize/lifetime CTest is separate from this compositor-dependent check.
- **Windows evidence missing.** No actual Windows runner is available. Missing
  Windows, reliable minimize/restore, and the remaining M01 rendering features
  block milestone qualification. Closing #4 records delivery of this clear slice,
  not milestone completion.
