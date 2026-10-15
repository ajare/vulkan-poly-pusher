# M01 — Vulkan geometry upload slice (#94)

## Supported subset and ownership

The frozen `mpp::Mesh`/`mpp::VertexBuffer` public APIs realize authored
geometry as native Vulkan buffers. CPU-side behaviour is retained unchanged:
constructors copy and validate CPU data, `setAttribute` stores the retained
numeric data-type vocabulary, `setIndexData`/`getBufferData`/`getIndexData`
manage CPU copies, and the same overflow/stride/range checks raise the same
`THROW_MPP` diagnostics. `Mesh::load()`/`unload()`, `mapBufferData` and
`mapIndexData` create, populate, update and retire device-local vertex/index
buffers through **staged uploads** — host-visible staging memory copied by
transfer commands on the window's graphics/present queue.

Uploads carry their own one-shot fences. Routine uploads, replacement and
destruction **never wait device-wide**: a single submission serial is shared
with the two presentation frame contexts delivered in #4, completion is
polled with zero-timeout fence queries, and the frame fence waits in
`present()` advance the completion watermark. Replaced or released native
storage is retired until every submission that could reference it has
completed, so geometry replacement or `Mesh` destruction never invalidates
work still in flight. `vkDeviceWaitIdle` remains restricted to swapchain
retirement and shutdown. Explicit **diagnostic readback**
(`mpp/GeometryDiagnostics.h`, additive) copies a live buffer back and waits
only for its own submission; labels follow `"Mesh '<name>' vertex buffer <i>"`
and `"Mesh '<name>' index buffer"`. Engine IDs stay opaque `uint32_t` values;
no Vulkan handle is ever exposed.

The retained numeric vertex data-type vocabulary (`GL_FLOAT`, `GL_UNSIGNED_BYTE`,
packed 2.10.10.10, …) keeps its established meanings and is translated to
`VkFormat` **only at the Vulkan boundary** (`VertexLayoutTranslation.h`, pure
CPU logic). Integer types become integer attributes unless normalized,
float/half ignore normalization, and packed 2.10.10.10 requires exactly four
components — all mirroring the frozen GL behaviour. Layouts Vulkan cannot
express fail with actionable diagnostics at `load()`: normalized 32-bit
integers (no Vulkan format), double-precision attributes (need shaderFloat64;
not delivered), bad component counts, unknown vocabulary values, and formats
the device does not support for vertex input (`bufferFeatures` +
`VK_FORMAT_FEATURE_VERTEX_BUFFER_BIT`, naming the device). `getCaps()` now
reports **truthful native vertex-input limits** (`maxVertexAttributes`,
`maxVertexAttributeStride` from device limits) used by the retained
`setAttribute`/stride validation; every other capability field stays zero.

Storage types `Static` and `Dynamic` both realize through staged uploads.
Full `mapBufferData` re-specifies storage (the deferred-safe analogue of the
reference's `glBufferData` orphaning); ranged maps stage subrange copies,
growing storage when the range exceeds capacity. Index widths 16 and 32 are
supported; other widths fail actionably. `usesPersistentMapping()` /
`usesPersistentIndexMapping()` truthfully report `false` — the
persistent-mapped streaming path is **not delivered** and fails explicitly if
ever reached through the private `PersistentMappedBuffer` type.

Not delivered, all failing explicitly: `Mesh::render` (drawing arrives with
pipeline realization, #95), `VertexBuffer::bind`/`unbind`/`enableAttribute`
(no GL buffer or attribute state exists), mapping before `load()`, and
material resource realization inside `Mesh::load()` (the reference loads the
material there; materials arrive with #95 — geometry upload does not require
one). ID diagnostics are truthful for the native backend: there is no
vertex-array object, so `getIdCount()`/`getLiveIdCount()` count vertex
buffers plus the index buffer rather than the reference's VAO+IBO names.
`Mesh::load()` is safe to call repeatedly (storage is replaced, not leaked),
and a failed `load()` leaves no partial native storage.

Public declaration audit keeps the 258-header manifest unchanged; `Mesh.h`
and `VertexBuffer.h` retain their original bytes (private storage is reused
with native meaning). `mpp/GeometryDiagnostics.h` is additive, as permitted
by spec #1, and compiled by the public-interface translation unit.

## Dependency/build choices

No new dependencies: Vulkan SDK/loader >=1.3 as pinned in #4. The geometry
store uses raw `VkBuffer`/`VkDeviceMemory` (one allocation per buffer,
device-local; one host-visible coherent staging buffer per upload, freed on
fence completion). VMA remains a permitted later optimization, not a slice
requirement. Sources live in `mpp/src/vulkan/` (`GeometryStore`, `Mesh`,
`VertexBuffer`, `PersistentMappedBuffer`, `GeometryDiagnostics`,
`VertexLayoutTranslation.h`); the window bridge owns the store and drives the
frame-completion hooks. `-DVPP_BUILD_RENDERER=OFF` still builds the CPU-only
subset unchanged.

## Scenario and commands

`tools/GeometryUpload.cpp` (VPP-only; frozen MPP has no public buffer-readback
seam, so paired comparison is deferred to the visual tickets #95–#97):

- Authored indexed triangle (position3 float + colour4 float, stride 28) and
  a 16-bit-index dynamic fan upload through the public APIs; readback is
  byte-exact against the CPU source for initial, full, ranged, grown and
  reloaded contents.
- Twelve in-flight update steps interleave full re-uploads, a growth
  replacement, an index growth, a storage-type switch (unload+load) and live
  presentation, followed by whole-mesh destruction while frames are in flight.
- Explicit-failure checks: render/bind/unbind/enableAttribute, map before
  load, normalized 32-bit and double attributes at load, bad offsets/strides,
  unsupported index width (24-bit), unknown readback labels, and readback of
  destroyed buffers.
- Half-float/normalized-ubyte mixed layout uploads and verifies byte-exact.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DVPP_BUILD_RENDERER=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
# Mandatory layer + sync validation; saves <log>.validation.log.
python3 tests/run_geometry_validation.py build/bin/VppGeometryUpload build/geometry.log
VK_DRIVER_FILES=/usr/share/vulkan/icd.d/lvp_icd.json \
  python3 tests/run_geometry_validation.py build/bin/VppGeometryUpload build/lvp-geometry.log
```

`tests/GeometryTranslationTests.cpp` extends CPU regressions with the pure
boundary translation: retained numeric constants, float/half/integer/packed
format mapping, and every actionable rejection. The CTest wrapper fails on any
validation error, warning, VUID or SYNC-HAZARD token.

## Recorded Linux results (2026-10-13)

- Release build succeeds; **6/6 CTest pass** (clear/present + geometry upload
  under mandatory Khronos + sync validation, public declarations, CPU
  regressions incl. translation tests, authored assets, ProgramBuilder).
  Renderer-disabled Release build passes **4/4** CPU-only tests.
- RADV **Radeon 8060S Graphics (STRIX_HALO)** and Mesa **llvmpipe (LLVM
  21.1.8)**: the full upload/replace/destroy-in-flight scenario reports
  **zero Vulkan/sync validation errors or warnings**. Linux GPU tests select
  X11 (the qualified scripted window seam from #4).
- Representative elapsed wall time for the whole scenario (window setup, ~35
  presented frames, 20+ staged uploads, 6 readbacks): RADV ~0.10 s,
  lavapipe ~0.10 s. Smoke measurements only, not renderer performance
  comparisons. Memory: one device-local allocation per live buffer plus one
  staging allocation per in-flight upload; retired/staging storage is freed
  on completion. Driver/native memory and GPU timings are not measured or
  qualified in this slice.
- **Windows evidence missing** (no Windows runner), minimize/restore remains
  unqualified (#4), and drawing/materials land in #95–#97. Closing #94
  records delivery of this upload slice, not milestone completion.

## Subset boundary

Uploads are verified by diagnostic readback only; no shading, drawing or
presentation of geometry is claimed. Vertex formats are validated against the
device, but pipeline realization, attribute/shader matching and draws are
explicitly #95+. Deferred-destruction correctness is asserted through clean
synchronization validation across in-flight replacement/destruction, not
through private object inspection.
