# M01 public authored triangle drawing — #96

## Delivered public surface

The existing `WindowSDL::create` → `RenderSystem` → `clearScreen` →
`Program::bind` / `BasicMaterial::setUniforms` / `UniformCollection::bindUniforms`
→ `Mesh::render` → `WindowSDL::show` sequence draws native Vulkan triangles.
`captureNextPresent` captures the actual swapchain image after those draws, not
an offscreen test-only renderer. No public declarations or module identities
changed; all Vulkan types remain implementation-private.

Like frozen MPP, `Mesh::render` consumes the caller's program and uniform
selection; it does not automatically select its attached material. `Mesh::load`
loads that material. Each draw snapshots the selected native pipeline, geometry
storage and uniform values; later changes cannot overwrite earlier draws.
The delivered pipeline is opaque, filled, single-sample, colour-only and
unculled. Both non-indexed and 16/32-bit indexed triangle lists are supported,
with exactly one instance. `render(1, start, count)` retains MPP's range units:
`count` is primitives, `start` is primitives for indexed meshes but **vertices**
for non-indexed meshes. Empty validated ranges produce no geometry.

Draws reject missing/unloaded programs or geometry, layout/topology mismatches,
invalid ranges, indices outside uploaded vertex storage, other primitive types
and instancing. Index validation uses the last **uploaded** index image, not
mutable CPU data that the client may have edited without mapping. Geometry
replacement, range updates, unload and destruction are safe both before `show`
and while earlier frames are in flight. Clear discards previously queued draws,
matching a clear of the full colour target. A minimized presentation discards
that frame's queued draws without submitting; the capture request remains pending.

### Minimal camera/transform surface

- Matrices start at explicit identity; `getCameraMatrix`, `getProjectionMatrix`,
  `getModelMatrix`, `getModelCameraProjectionMatrix`, `getNormalMatrix` work.
- `setCamera3d` retains `glm::lookAt` semantics. Camera/model push/pop, model
  reset, translation, degree-based rotation and scaling retain MPP multiplication
  order. Empty stacks fail with operation-specific errors.
- The shared triangle uses identity projection and the camera/model matrices
  through an authored `@@Uniform(mat4 TRANSFORM)`. `Mesh::render` does **not**
  inject engine uniforms. Frozen MPP's reserved `@MCPMatrix` is not exposed in
  its generic uniform-location map; using an authored transform keeps the shared
  draw entirely on the public uniform API. The #93 token fixture remains intact.
- Perspective/2D projection setters, projection stack operations, camera-frame
  buffers, model/scene rendering, depth, blending/culling state setters, textures,
  batches, text, graph outputs and all other undelivered renderer operations still
  fail explicitly. In particular, perspective setup cannot silently omit MPP's
  accompanying depth-state contract. Those are later slices, not claimed here.
- Client clip space remains MPP's `[-w,w]` depth and +Y up. The backend-specific
  vertex body maps depth to `[0,w]`; the native negative-height viewport preserves
  +Y up. Captures are top-down RGB8 from the UNORM swapchain, with no extra gamma
  conversion. Full-window viewport only.

## Native execution and lifetimes

The swapchain supplies colour-attachment image views/framebuffers. A colour-only
render pass clears and records the queued draws in order, followed by explicit
colour-write → transfer-read (capture) / presentation transitions. The acquire
semaphore wait and initial image transition run at colour-attachment output.
The two existing frame contexts own their command pools, fences and immutable
per-draw uniform buffers/descriptors. Descriptor snapshots are freed only after
that context's fence is waited on. This prevents two materials/transforms using
one Program in one frame from observing the last draw's values.

Geometry and pipeline pins protect references between `Mesh::render` and
submission, including unloading/reloading or destroying the public objects before
`show`. Pins become completion-serial protection at submission. Each frame now
records its **own** geometry/pipeline submission serials: waiting for the older
context must not mark the latest submissions complete. Upload barriers order prior
readers/writers before transfer writes and make writes visible to vertex/index,
vertex/fragment uniform and diagnostic transfer reads. A range update of a buffer
referenced by a queued draw uses copy-on-write; a retained uploaded CPU image
supports that copy and index safety checks.

There are no routine device-wide waits. Frame reuse waits only its own fence;
uploads use asynchronously polled fences. Explicit capture waits its submission;
resize and teardown may wait the device. This correctness-first slice allocates
per-draw descriptor pools/buffers and retains CPU upload images; pooling and
upload batching remain optimization work, not a performance claim.

## Observable regression scenario

`tools/TrianglePresent.cpp` is built as `VppTrianglePresent` and separately as
`MppTrianglePresent` against immutable MPP. It loads the same asymmetric
`Triangle.vertices`, logical `Basics.DrawTriangle` shader resource, and a logical
BasicMaterial with white TINT. Backend bodies differ only in clip-depth mapping.
The frozen GL client requires a small VAO adapter because `Mesh::bind` is private;
it binds attributes with public VertexBuffer methods. **VPP has no draw adapter or
private test hook**. Capture adapters use the existing paths from #4.

Shared script: 48 frames, camera at `(0,0,0.25)` looking at `(0,0,-1)`, identity
projection, RGB vertex colours, clear `(0.05,0.075,0.1)`. Capture frame 19 at
320×240, frame 29 after resize to 400×280, and frame 47 with two translated/scaled
triangles sharing one Program but different uniform tints. Single-triangle frames
get their tint from `BasicMaterial::setUniforms`, not a client override.

VPP additionally exercises in-flight range uploads, mesh/program reloads, explicit
unsupported calls and bad layouts/ranges, queued range-update copy-on-write,
clear-after-draw, indexed 16/32-bit captures, indexed/non-indexed subranges,
uploaded-invalid-index rejection
even after CPU-only edits, mesh/program destruction before presentation, and
window destruction with submitted **and queued** draws while the scene survives.

`tests/compare_triangle.py` computes expected barycentric vertex colours from the
shared asset, checks background and all interior samples, and rejects missing,
flipped, displaced or incorrectly tinted triangles. Interior tolerance **2/255**,
background **1/255**; a fixed 0.02 barycentric edge band excludes backend raster
coverage differences. Initial/resized images check 17,876 / 25,956 interior pixels;
each split triangle checks 6,559. Tolerances were set before the first capture,
not enlarged to make either backend pass. `run_triangle_validation.py` requires
Khronos and synchronization validation and rejects errors **and warnings**.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DVPP_BUILD_RENDERER=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
python3 tests/run_triangle_validation.py build/bin/VppTrianglePresent build/triangle.ppm
VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json \
  python3 tests/run_triangle_validation.py build/bin/VppTrianglePresent build/lvp-triangle.ppm

# Reference setup and immutable checkout checks: M01-reference-setup.md / M01-cpu-assets.md.
cmake -S tests/reference -B build/reference \
  -DMPP_REFERENCE_SOURCE=/home/wm/code/mpp-ref \
  -DMPP_REFERENCE_LIBDIR=/home/wm/code/mpp-ref/build/bin/Release
cmake --build build/reference --parallel
SDL_VIDEODRIVER=x11 build/reference/MppTrianglePresent build/mpp-triangle.ppm
python3 tests/compare_triangle.py build/mpp-triangle.ppm --reference
```

## Recorded evidence (Linux, 2026-10-15)

- Release renderer build: **8/8 CTest pass**; CPU-only build: **4/4 pass**.
  Existing clear, geometry, shader/material, authored-assets and CPU regressions
  remain green. The audit passes all 258 frozen public headers; representative
  public rendering calls compile against both VPP and frozen MPP. Shared test
  sources now guard additive VPP-only headers/tests when built against MPP.
- RADV Radeon 8060S Graphics (STRIX_HALO) and llvmpipe (LLVM 21.1.8): triangle,
  resize, updates, reloads and destruction pass with **zero Vulkan and
  synchronization-validation errors/warnings**. X11 is the scripted Linux seam.
- Frozen MPP and both Vulkan drivers pass the same analytic colour/orientation,
  background and transformed-multi-draw checks. Frozen source/dependencies remain
  unmodified. MPP's CPU document-foundation test failed once during concurrent
  builds/tests (`file watcher own-save suppression failed`); an isolated rerun
  passed all seven suites. No reference changes or assertion suppression.
- Smoke-only measurement on RADV: whole scenario including setup, compilation,
  frames and captures ~0.50 s, peak process RSS 89,028 KiB (`/usr/bin/time`);
  validation-enabled lavapipe ~0.18 s. These are not controlled comparative
  benchmarks, GPU timings or frame-time distributions.
- **Windows: no runner available, unverified.** Reliable compositor
  minimize/restore remains unqualified as documented in #4; no new claim here.
  Textures/depth, the complete Basics scene, and remaining #97 comparison gates
  are not delivered by this ticket. Missing platform evidence blocks **milestone
  qualification**, not closure of this explicitly scoped Linux-verified slice.
