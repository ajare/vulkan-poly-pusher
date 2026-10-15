# M01 shared comparison scenario — "Basics"

Per spec #1, every milestone ships a **shared comparison scene** that loads in both
a frozen-MPP client and the equivalent VPP client, so the delivered rendering
capabilities can be evaluated on identical content. This file is the **contract**
for the Milestone 1 "Basics" scene.

> Status: **CPU portion (#3), clear/present (#4), and paired public triangle (#96–#97) runnable.**
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
negative cases exercise the same public stream seam. This fixture alone does
**not** draw the triangle; the delivered #95–#97 portions below supply
realization, drawing and paired captures. See [`../../migration/M01-shader-compilation.md`](../../migration/M01-shader-compilation.md).

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

## Delivered paired triangle comparison (#97)

The shared client above is the delivered **coloured triangle comparison scene**,
not the still-pending textured/depth scene. No second geometry or logical material
is generated for either backend. Each shared capture logs its zero-based frame,
pixel dimensions, scene variant, RGB8 encoding and top-left origin.

| Frame | Size | Scene | Expected projected vertex positions (top-left coordinates) |
| --- | --- | --- | --- |
| 19 | 320×240 | One triangle, white material TINT | red (40,198), green (280,198), blue (136,30) |
| 29 | 400×280 | Same triangle after resize | red (50,231), green (350,231), blue (170,35) |
| 47 | 400×280 | Two triangles, x offsets −0.5/+0.5, scale 0.5 | same orientation; left TINT (1,.25,.25,1), right (.25,1,1,1) |

Positions are projected vertex locations, not pixel-centre sampling locations.
Analytic interior interpolation verifies the colours near each vertex as well as
across the triangle, so a flip/mirror or swapped attributes cannot pass.
Camera is `(0,0,.25)` facing −Z with +Y up, identity projection, no animation or
random seed; clear is `(.05,.075,.1,1)`. No textures, depth, culling or blending
are claimed. The shader variants differ only in clip-depth conversion.

Both capture adapters write binary P6 **top-down RGB8** (no alpha, metadata or
colour-profile transform). Linear authored colours are quantized to UNORM bytes,
not sRGB encoded: frozen GL uses its ordinary non-sRGB-converting back buffer;
VPP uses a UNORM swapchain. The GL bottom-up readback is row-reversed exactly
once; VPP readback already uses the top-left convention. Captures are requested
after drawing and before that numbered `show`; VPP reads the submitted image
before presentation. Frames are numbered from zero, excluding later VPP-only
lifetime probes.

```sh
python3 tests/compare_triangle.py build/mpp-triangle.ppm --against build/97-radv.ppm
python3 tests/compare_triangle.py build/mpp-triangle.ppm --against build/97-lvp.ppm
```

The comparator checks **both** images analytically first, then compares paired
channels directly, retaining 2/255 interior and 1/255 background limits (not
adding the two analytic error budgets). RGB8 rounding contributes up to half a
code value per capture; interpolation/subpixel precision and final quantization
justify at most two interior code values, while constant clear needs only one.
The unchanged 0.02 barycentric edge band excludes raster-coverage ambiguity, not
colour errors in the interior. This is not a silhouette/pixel-identical test.
No tolerance was widened and no reference image was updated. The frozen client
is rerun from the pinned unchanged source; captured images are run artifacts,
not replacement golden assets. CPU-only negative controls reject mirrored,
flipped, missing or wrongly tinted geometry, swapped RGB attributes, malformed
captures, and over-budget analytic/paired differences.

See [`../../migration/M01-triangle-comparison.md`](../../migration/M01-triangle-comparison.md)
for paired measurements, reproducible commands, whole-#5 acceptance mapping and
remaining milestone qualification gaps.

## Blocked on

- Remaining VPP rendering slice (M1) for textured geometry/depth and full scenes.
- Full scene/intermediate graph capture; clear and triangle captures are delivered.
- Windows runner, reliable minimize/restore, and remaining rendering validation
  for full milestone qualification. Linux clear, uploads, shader/material and
  triangle execution have evidence only for their explicit subsets.
