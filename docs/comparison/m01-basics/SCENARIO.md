# M01 shared comparison scenario — "Basics"

Per spec #1, every milestone ships a **shared comparison scene** that loads in both
a frozen-MPP client and the equivalent VPP client, so the delivered rendering
capabilities can be evaluated on identical content. This file is the **contract**
for the Milestone 1 "Basics" scene.

> Status: **CPU portion (#3), clear/present (#4), and public triangle (#96) runnable.**
> `tools/AssetValidate.cpp` validates assets in both libraries without a context;
> `tools/ClearPresent.cpp` exercises the original startup/window sequence and
> compares initial/resized clear captures. The textured-geometry portion below is
> still a visual contract, not delivered rendering. Requests for that unsupported
> portion fail explicitly, never render nothing as a successful scene.

## Scene contents (authored, backend-agnostic)

A minimal authored scene exercising the M1 acceptance focus:

- **Textured geometry** — at least two textured meshes with distinct albedo textures.
- **Overlap** — meshes that overlap in depth so depth ordering is observable.
- **Asymmetric orientation markers** — a deliberately asymmetric, handed marker
  (e.g. an L-shaped / arrow quad with a corner colour tag) so winding, UV
  orientation, and handedness errors are visible rather than symmetric-hidden.

## Shared invariants across both renderers

Identical across MPP and VPP: same authored scene document, geometry, textures,
camera transform, resolution, clear colour, and capture frame number.

## What is verified (M1 acceptance focus)

| Check | Observable |
| --- | --- |
| Colour | sampled albedo matches authored texture within tolerance |
| UVs | marker corner lands in the expected screen corner (no flip/mirror) |
| Winding | front/back faces consistent; no inverted-geometry holes |
| Depth | nearer marker occludes the farther one correctly |
| Capture | final-frame capture produced by both clients at the same frame number |
| Resize / minimize / restore | scene re-renders correctly after each window transition |
| Shutdown | clean teardown, no leaks/errors on exit |

## Comparison conventions (to be recorded per capture)

- Colour space and any sRGB/linear handling used for the capture.
- Y-orientation convention of the captured image (top-left vs bottom-left origin).
- Capture frame number and deterministic camera/time seed.
- Documented visual tolerance with justification (behavioural + visual
  equivalence, **not** bit-identical). Never widen a tolerance to hide a defect.

## Harness contract

- One shared comparison-client implementation, built separately against frozen MPP
  and against VPP, exercising only public APIs and loading this scene.
- Backend startup/capture adapters stay isolated; frozen MPP is never modified to
  make a comparison pass.
- Deliverables per run: paired captures (MPP, VPP), automated behavioural check
  results, and the recorded tolerance + conventions.

## Delivered CPU portion (#3)

`assets/` contains unchanged frozen-MPP preview scene/pipeline/library/particle
assets and negative editor fixtures. The preview scene is a parser comparison
fixture, **not** the final textured-overlap/orientation rendering scene above.
`tests/compare_assets.py` compares exact diagnostic, numeric and XML/YAML round-trip
observations against separately built clients; CTest runs local assertions and
retained CPU regressions. There are no visual captures/tolerances in this portion.
See [`../../migration/M01-cpu-assets.md`](../../migration/M01-cpu-assets.md) for
runnable instructions, provenance, results and unsupported-feature boundaries.

## Delivered clear/present prelude (#4)

The shared `tools/ClearPresent.cpp` client supplies 180 scripted frames in three
lifetime cycles, initial/resized PPM captures, exact expected-colour checks, and a
1/255 paired-channel tolerance. Native Vulkan + sync validation is mandatory in
its CTest wrapper. Frozen MPP and VPP captures match with maximum difference 0/255
on this Linux host. See [`../../migration/M01-vulkan-clear.md`](../../migration/M01-vulkan-clear.md)
for commands, dependency choices, subset boundaries and qualification evidence.
The optional `--minimize` scenario fails if the compositor refuses restore; this
area remains unqualified. This prelude does not test depth, UVs or winding.

## Delivered shader compilation fixture (#93)

`shaders/mpp/` and `shaders/vpp/` contain authored vertex/fragment program
variants named `Basics.Triangle`, selected through the backend's resource root.
The VPP body ports clip depth while retaining the same geometry and authoring
tokens. CPU regressions load the VPP file stream, compile it at runtime and
validate reflected attributes, uniforms and descriptors. Programmatic edits and
negative cases exercise the same public stream seam. This does **not** yet draw
the triangle or claim visual equivalence; #95–#97 supply realization, drawing and
paired captures. See [`../../migration/M01-shader-compilation.md`](../../migration/M01-shader-compilation.md).

## Delivered geometry-upload portion (#94)

The authored triangle layout from the shader fixture (position3 float +
colour4 float, stride 28) uploads through the public Mesh/VertexBuffer APIs
into native vertex/index buffers: staged uploads, completion-based deferred
destruction across the frame contexts, and byte-exact diagnostic readback via
additive `mpp/GeometryDiagnostics.h`. `tools/GeometryUpload.cpp` exercises
initial/full/ranged/grown uploads, reloads, storage-type switches and
in-flight destruction under mandatory Khronos + synchronization validation.
This verifies upload contents only; it does **not** yet draw the triangle.
See [`../../migration/M01-geometry-upload.md`](../../migration/M01-geometry-upload.md).

## Delivered program/material realization fixture (#95)

The authored `Basics.Triangle` program and programmatically authored variants
realize natively through the public Program/BasicMaterial/ResourceManager APIs:
graphics pipelines with vertex input matched to the authored mesh layout,
descriptor set/pipeline layouts from reflection, and material uniform values in
descriptor-backed per-frame-context storage (verified byte-exact by additive
diagnostic readback). Runtime shader edits rebuild the affected pipeline
without restarting the renderer; sampler/texture and authored-block programs
fail explicitly. This verifies realization only; it does **not** yet draw the
triangle — #96–#97 supply drawing and paired captures.
See [`../../migration/M01-pipeline-realization.md`](../../migration/M01-pipeline-realization.md).

## Delivered public triangle portion (#96)

`tools/TrianglePresent.cpp` loads `assets/Triangle.vertices` and logical
`Basics.DrawTriangle` from the backend shader roots, then draws through real
Program/BasicMaterial/UniformCollection/Mesh APIs. Both frozen-MPP and VPP clients
capture frame 19 (320×240), frame 29 after resize (400×280), and frame 47 with two
transformed triangles using different uniforms on one Program. The minimal camera
uses identity projection and a public authored `TRANSFORM` uniform. The earlier
`Basics.Triangle` token/reflection fixture remains unchanged.

`tests/compare_triangle.py` checks analytic barycentric colours/orientation and
background from the same authored geometry: 2/255 interior, 1/255 background,
fixed 0.02 barycentric edge exclusion. VPP additionally tests indexed draws,
queued/in-flight updates and destruction, pipeline reloads, clear-after-draw,
explicit failures, and window destruction with a live scene. All captures use the
existing capture path; VPP has no private rendering test hook. Linux RADV and
lavapipe pass with zero Vulkan/synchronization-validation diagnostics; the frozen
MPP client passes the shared pixel checks.
See [`../../migration/M01-triangle-draw.md`](../../migration/M01-triangle-draw.md)
for commands, the frozen-GL binding adapter, exact subset and evidence.

## Blocked on

- Remaining VPP rendering slice (M1) for textured geometry/depth and full scenes.
- Full scene/intermediate graph capture; clear and triangle captures are delivered.
- Windows runner, reliable minimize/restore, and remaining rendering validation
  for full milestone qualification. Linux clear, uploads, shader/material and
  triangle execution have evidence only for their explicit subsets.
