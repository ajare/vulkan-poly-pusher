# M01 program/material realization as native pipelines and descriptors — #95

## Supported subset and ownership

The frozen `mpp::Program`, `mpp::BasicMaterial`, `mpp::ResourceManager` and
`mpp::UniformCollection::bindUniforms` public APIs are delivered natively in the
Vulkan renderer build. An authored MPP Program realizes into native execution
objects: the runtime-compiled SPIR-V variant and reflected bindings from #93
become shader modules, a descriptor set layout and a pipeline layout, and a
graphics pipeline whose vertex input state is translated from the authored
`MeshSpecification` geometry layouts of #94 — matched attribute-by-attribute
against the reflected shader inputs, never reconstructed from a GL state
machine. Reflected authoring tokens (`@ViewPos`, `@MMatrix`, `@MCPMatrix`,
`@NormalMatrix`, …) land in the parser-generated `_mpp_Uniforms` block
(set 0, binding 0) exactly as compiled in #93.

Material colour/uniform values reach the shader through **descriptor-backed
uniform buffers, one per presentation frame context** (the two contexts from
#4). `BasicMaterial::setUniforms()` keeps its MPP-level purpose: the retained
`UniformCollection` values are packed into the program's generated-block image
at the reflected std140 member offsets (`UniformBlockWrite.h`, pure CPU) and
staged into every frame context's uniform buffer through the geometry store's
staged-upload path — no OpenGL global-state emulation, no per-frame device-wide
waits. Retained sparse-override semantics: uniforms the program does not
declare are no-ops; unwritten members keep their last uploaded values; type,
shape, array-range and row-major mismatches fail naming the program and the
marked-up uniform. `UniformCollection::setUniform` keeps its retained
emplace-without-overwrite semantics, so runtime edits replace the collection
through the stream and recreate the material (exercised by the scenario).

`Program::bind()` keeps its MPP-level purpose as **input selection for
subsequent renderer operations**: it selects the realized pipeline for the
drawing slice (#96) to record; there is no global device state. Binding an
unloaded program fails explicitly. `Mesh::load()` now realizes an attached
material (retained reference ordering); meshes without a material keep the
upload-only behaviour used by the geometry tests. Public numeric contracts are
retained: `getUniformGlType`/`getSamplerGlType` report the legacy GL type
vocabulary (mapped at the boundary), and uniform "ids" are generated-block
byte offsets — engine-internal values, not GL location promises.

`ResourceManager` is delivered as a subset (`mpp/src/vulkan/ResourceManager.cpp`)
with retained lifetime, naming, program-source aliasing (identical sources
share one Program resource), sort-ID, default-program and CSV-dump behaviour.
Factories exist for the delivered types only (`Program`, `BasicMaterial`,
`String`); declaring any other authored type fails explicitly naming the type.
`ResourceStream`'s child-resource realization methods are restored to the
retained reference logic (a childless stream is a no-op). `RenderSystem`
diagnostic logging (`debugMessage`/`infoMessage`/`warnMessage`/`errorMessage`)
is delivered, routing to the client logger.

**Runtime shader authoring** remains possible end-to-end: editing a
programmatic parser's source and unloading/reloading the Program recompiles
the variant (#93 cache keys are source-based) and rebuilds the affected
pipeline, descriptor layouts and uniform storage without restarting the
renderer. The retired pipeline is deferred-destroyed against the
frame-completion watermark shared with the geometry store, so rebuilds never
invalidate in-flight frames. A failed rebuild leaves no partial native state
(realization is transactional) and the program unloads cleanly; fixing the
source and reloading recovers.

## Explicit subset boundary (all failing clearly, naming resource and binding)

- Programs declaring **sampler descriptors** fail realization: texture
  resources are not delivered. Material texture resolution is reachable only
  through such programs, so it fails through the same path first.
- **Authored explicit uniform blocks** (outside the generated
  `_mpp_Uniforms` block) fail realization naming set/binding; author material
  values through `@@Uniform` markup.
- **Matrix, boolean and 64-bit vertex inputs**, mesh layouts whose attributes
  do not exactly match the reflected shader inputs (count, components,
  scalar class), and device-unsupported vertex formats fail actionably.
- **Drawing** (`Mesh::render`) remains undelivered and fails explicitly; the
  render pass integration and public draw calls arrive in #96. Pipelines are
  created against a colour-only render pass compatible with the swapchain
  format; depth, blending and authored culling state arrive with the scene
  work. The pipeline rasterizes both faces in this subset.
- Row-major matrix members, uniform-block arrays, 64-bit uniforms and
  `Bool`/`Double`/`User` UniformCollection types fail on write.
- PbrMaterial, PostEffectMaterial, Texture, Sampler, Model and other resource
  types keep failing explicitly at declaration.

## Dependency/build choices

No new dependencies (Vulkan SDK/loader >=1.3 as pinned in #4; glslang +
SPIRV-Reflect as pinned in #93). New private sources live in
`mpp/src/vulkan/`: `PipelineStore` (native realization store owned by the
window bridge beside the geometry store, sharing its frame-completion
watermark), `Program.cpp`, `BasicMaterial.cpp`, `ResourceManager.cpp`,
`UniformBinding.cpp`, `PipelineDiagnostics.cpp`, plus the pure-CPU boundary
headers `VertexInputTranslation.h` and `UniformBlockWrite.h`. The retained
pure-CPU `Resource.cpp` and `String.cpp` join the shared build in both
configurations. `compat/CpuResourceBoundary.cpp` becomes CPU-only: the
CPU-only build keeps explicit-failure stubs for resource declaration, lookup
and uniform binding. `UniformCollection::bindUniforms` moved out of the
shared source into per-backend implementations.

Additive public header `mpp/PipelineDiagnostics.h` (permitted by spec #1;
dependency-neutral, compiled by the public-interface translation unit):
`programFrameContextCount()`, `programUniformBlockSize(program)` and
`readbackProgramUniforms(program, context)` expose observable realization
results — never Vulkan handles — so tests verify uniform values in native
storage without inspecting private pipeline structure. Public declaration
audit keeps the 258-header manifest unchanged.

## Scenario and commands

`tools/ProgramPipeline.cpp` (VPP-only; frozen MPP has no descriptor readback
seam, so paired comparison is deferred to the visual tickets #96–#97):

- Declares the authored `Basics.Triangle` program through
  `FileProgramStream`, verifies source-aliasing of an identical second
  declaration, duplicate-name and missing-resource failures, and unrealized
  `bind()` failure, then loads: realized-pipeline ID accounting, vertex
  attribute reflection, authoring-token uniform IDs, fragment-output
  validation and zero-initialized per-frame-context uniform storage.
- A programmatic tinted program (`@@Uniform(vec4 TINT)`) plus a
  `ProgrammaticBasicMaterialStream` material: material colour values are
  verified byte-exact in the descriptor-backed storage of **every** frame
  context after `setUniforms()`, across presented frames, edits, sparse
  overrides, and a type-mismatch failure naming the program and uniform.
- Runtime shader authoring: source edit → unload/reload rebuilds the pipeline
  (new ID, advanced reflection revision, uniforms intact); an invalid edit
  fails with the program name and stage diagnostic leaving no partial
  realization; a fix recovers.
- Explicit failures: sampler program, authored uniform block (naming set 1
  binding 3), mesh/shader vertex-input mismatch (naming the program), missing
  material program, undelivered resource type at declaration, and uniform
  readback misuse (unrealized program, uniform-free program, frame context
  out of range).
- Lifetime: pipeline unload/reload interleaved with live presentation;
  teardown with live programs/materials retires everything under mandatory
  Khronos + synchronization validation.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DVPP_BUILD_RENDERER=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
# Mandatory layer + sync validation; saves <log>.validation.log.
python3 tests/run_pipeline_validation.py build/bin/VppProgramPipeline build/pipeline.log
VK_DRIVER_FILES=/usr/share/vulkan/icd.d/lvp_icd.json \
  python3 tests/run_pipeline_validation.py build/bin/VppProgramPipeline build/lvp-pipeline.log
```

`tests/PipelineTranslationTests.cpp` extends the CPU regressions with the pure
boundary translation: authored layout + reflected inputs → native vertex input
state (locations, formats, offsets, strides, topology), every actionable
mismatch rejection, and std140 uniform-block writes (scalars, vectors, mat4
column strides, array strides, sparse no-ops, and every type/shape rejection).

## Recorded Linux results (2026-10-14)

- Release build succeeds; **7/7 CTest pass** (clear/present, geometry upload
  and program/pipeline realization under mandatory Khronos + sync validation,
  public declarations, CPU regressions incl. both translation suites, authored
  assets, ProgramBuilder). Renderer-disabled Release build passes **4/4**
  CPU-only tests; **74 authored-resource observations match frozen MPP
  exactly**.
- RADV **Radeon 8060S Graphics (STRIX_HALO)** and Mesa **llvmpipe (LLVM
  21.1.8)**: the full realize/edit/rebuild/unload-in-flight scenario reports
  **zero Vulkan/sync validation errors or warnings**. Linux GPU tests select
  X11 (the qualified scripted window seam from #4).
- Representative elapsed wall time for the whole scenario (window setup, ~15
  presented frames, 8 program realizations, staged uniform uploads and
  readbacks): ~0.16 s. Smoke measurement only, not a renderer performance
  comparison. Memory: one pipeline + descriptor pool + one uniform buffer per
  frame context per realized program; retired pipelines and buffers are freed
  on frame completion.
- **Windows evidence missing** (no Windows runner); drawing, depth and paired
  visual captures land in #96–#97. Closing #95 records delivery of this
  realization slice, not milestone completion.
