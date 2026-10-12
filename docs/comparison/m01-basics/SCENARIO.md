# M01 shared comparison scenario — "Basics"

Per spec #1, every milestone ships a **shared comparison scene** that loads in both
a frozen-MPP client and the equivalent VPP client, so the delivered rendering
capabilities can be evaluated on identical content. This file is the **contract**
for the Milestone 1 "Basics" scene.

> Status: **contract only.** The MPP half is runnable today (the frozen reference).
> The VPP half becomes runnable when the Milestone 1 rendering slice lands. Until
> then, invoking the VPP client on this scene must fail with a clear
> "not implemented" diagnostic, not render nothing silently.

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

## Blocked on

- VPP rendering slice (M1) for the VPP half.
- A capture path on the host (no screenshot tooling present — see
  [`../../migration/M01-smoke-evidence.md`](../../migration/M01-smoke-evidence.md)).
- Windows runner + Khronos validation layer for full milestone qualification.
