# M01 — Migration coverage inventory

This is the coverage inventory required by ticket #2 / Milestone 1: every public
header, rendering feature, application path, and existing test mapped to a delivery
milestone with a migration classification. It is built against the frozen reference
commit `21b1bcff…` and is **not** an exhaustive compatibility proof — it is the
starting map that later milestones complete against.

- Machine-readable per-header data: [`coverage-inventory.csv`](coverage-inventory.csv)
  (258 public headers). Regenerate with [`gen_inventory.sh`](gen_inventory.sh)
  (set `MPP_REF` to the frozen checkout).
- The classification below is a first-pass migration intent. It is refined as each
  milestone actually ports the code; a header's category is a plan, not a verdict.

## Classification legend

| Category | Meaning |
| --- | --- |
| `retained` | Backend-agnostic. Interface **and** implementation reused unchanged (parsing, serialization, data structures, scene documents, math, config). |
| `retained-interface/native-impl` | Public header/signature retained for source compatibility; the GL execution behind it is replaced by a native Vulkan implementation. |
| `narrow-decoupling` | Public header retained with **additive** Vulkan hooks (explicit resource-use declarations for custom passes) or with the legacy GL numeric format/sampler vocabulary remapped to Vulkan equivalents while preserving the numeric contract. |
| `native-replacement` | GL-specific plumbing removed and replaced outright (no retained GL semantics). |
| `test` | Test-only header. |

## Key structural findings

- **0 of 258 public headers include `GL/…` or `glew.h`.** The public source
  interface is already GL-free at the header level, which is what makes the
  source-compatibility seam viable: ordinary client code that compiles against MPP
  headers need not see Vulkan types either.
- **GL execution is confined to the `mpp` module's `src/`** — 24 source files
  contain `glXxx(` calls and ~2 870 `GL_*` enum references (352 unique). Every
  other module (`mpp-app-support`, `mpp-data`, `mpp-helper`, `mpp-mesh`,
  `mpp-mesh-specification-parser`, `mpp-program`, `mpp-resource-parsers`) has
  **zero** GL calls.
- **Legacy numeric vocabulary** (texture internal formats, sampler params) lives in
  `mpp-resource-parsers` (79 `GL_*` refs / 37 unique) and `mpp` sampler/texture
  params. These are the `narrow-decoupling` surface: the numeric contract clients
  set must keep its meaning, remapped to Vulkan formats/samplers, not reinterpreted.

## Module summary

| Module | Public headers | retained | native-impl | narrow-decoupling | native-replacement | test |
| --- | --- | --- | --- | --- | --- | --- |
| `mpp` (core renderer) | 154 | 101 | 39 | 7 | 1 | 6 |
| `mpp-app-support` | 22 | 17 | 0 | 3 | 0 | 2 |
| `mpp-resource-parsers` | 45 | 40 | 0 | 2 | 0 | 3 |
| `mpp-mesh` | 13 | 13 | 0 | 0 | 0 | 0 |
| `mpp-program` | 8 | 8 | 0 | 0 | 0 | 0 |
| `mpp-helper` | 10 | 10 | 0 | 0 | 0 | 0 |
| `mpp-mesh-specification-parser` | 4 | 4 | 0 | 0 | 0 | 0 |
| `mpp-data` | 2 | 2 | 0 | 0 | 0 | 0 |
| **Total** | **258** | **195** | **39** | **12** | **1** | **11** |

### `mpp` — native-implementation surface (39 headers)

These keep their public declarations; their GL execution becomes Vulkan:

`RenderSystem`, `RenderPipeline`, `RenderPipelineFlow`, `RenderPipelineOutput`,
`RenderPass`, `RenderTarget`, `RenderTexture`, `Screen`, `Program`,
`ComputeProgram`, `UniformBuffer`, `ShaderStorageBuffer`, `VertexBuffer`,
`VertexBufferRenderCommand`, `BatchRenderer`, `TextureRenderer`, `BufferRenderer`,
`RenderGraphExecutor`, `RenderGraphFrameContext`, `RenderGraphBuiltInPasses`,
`RenderGraphScenePass`, `RenderOutputProcessor`, `DefaultShaders`, `PbrShaders`,
`ParticleShaders`, `TrailShaders`, `ParticleSystem`, `ParticleDrawProgram`,
`TrailSystem`, `IblEnvironmentCache`, `Caps`, `RenderInfo`, `Mesh`,
`MeshInstance`, `Model`, `ModelInstance`, `ParticleData`, `Font`, `InternalFont`.

`GLErrorCheck` is the single `native-replacement` header (GL error-check plumbing;
replaced by Vulkan validation / result checks).

### `narrow-decoupling` surface (12 headers)

- Custom-pass / graph resource-use declarations: `RenderGraphImportRegistry`,
  `RenderGraphPassFactoryRegistry` (additive explicit-declaration hooks).
- Legacy numeric vocab remap: `Sampler`, `SamplerParams`, `SamplerStream`,
  `TextureParams`, `TextureData`, `FileSamplerStream`, `FileTextureStream`.
- ImGui render backend (currently GL): `app/ImGuiBackendData`,
  `app/ImGuiDataProvider`, `app/ImGuiPlatform` — window/input retained, the
  draw backend becomes Vulkan.

## Rendering-feature → milestone map

Feature groups and the headers that carry them, aligned to the spec #1 milestone
table. (A feature's milestone is where it becomes usable in VPP; earlier milestones
may introduce the minimum infrastructure it needs.)

| Feature group | Representative headers | Milestone |
| --- | --- | --- |
| Init / device / presentation / resize / capture | `RenderSystem`, `Screen`, `RenderInfo`, `Caps`, `RenderOutputProcessor` | M1 |
| Textured geometry, depth, winding, UVs | `Mesh`, `MeshInstance`, `Model`, `VertexBuffer`, `BasicMaterial*` | M1 |
| Mesh layouts, indexed geometry, materials/uniforms | `mpp-mesh/*`, `Material`, `UniformBuffer`, `UniformCollection` | M2 |
| Textures / samplers (legacy numeric vocab) | `Texture*`, `Sampler*`, `FileTextureStream`, `FileSamplerStream` | M2 |
| 2D batches, text, atlases, clipping | `Batch*`, `QuadBatch*`, `TriangleBatch*`, `LineBatch*`, `CircleBatch`, `Font`, `ClipRectangle` | M2 |
| Legacy authored pipeline | `LegacyPipeline*`, `LegacyMaterialConversion` | M2 |
| Authored multipass graphs, MRT, imports, mip/cubemap, pooling | `RenderGraph*`, `RenderGraphTemplate`, `RenderGraphImportRegistry`, `RenderGraphPassFactoryRegistry` | M3 |
| PBR materials / specialization / glTF | `Pbr*`, `FileGltfPbrMaterialStream`, `GltfPbrMaterialLoader` | M4 |
| HDR environment / IBL | `IblEnvironmentCache` | M4 |
| Shadows + AO | `PbrLight`, `AmbientOcclusion` | M5 |
| Post effects, bloom, tone map, MSAA/SSAA/TAA/FXAA, named outputs | `PostEffect*`, `AntiAliasing`, `RenderPipelineOutput` | M6 |
| Water / planar + auxiliary views / oblique clipping | (aux-view + water paths in `mpp` render pipeline) | M7 |
| Core GPU particles (spawn/sim/compaction/indirect) | `ParticleSystem`, `ParticleData`, `ParticleDrawProgram`, `ParticleShaders` | M8 |
| Advanced particle rendering / collisions / OIT | particle feature modules in `mpp` | M9 |
| Events, composition, trails | `TrailSystem`, `TrailData`, `TrailShaders`, child-effect composition | M10 |
| Applications + tools + Vulkan UI | `mpp-app-support/*`, app targets | M11 |
| Release / performance / packaging | packaging + measurement paths | M12 |

## Application path inventory

Five applications must remain usable as their capabilities are delivered
(spec #1). CLI smoke/validation modes are existing seams to reuse.

| Application | Target | Key CLI paths | Milestone closure |
| --- | --- | --- | --- |
| DemoSuite | `demo-suite` | `--package <pkg>`, `--package-smoke-test` (30 frames), `--particles`, `--particle-tests` | M11 (slice exercised from M1) |
| PipelineEditor | `pipeline-editor` | `--validate [--warnings-as-errors] <yaml>`, `--gpu-tests`, `--smoke-test`, `--export-package`, `--export-legacy-package`, `[pipeline.yaml]` | M11 |
| ParticleEditor | `particle-editor` | `--document-tests`, `--preview-tests`, `--resource-tests`, `--core-particle-tests`, `--spatial-tests`, `--control-tests`, `--validate <file>` | M11 |
| ProgramBuilder | `program-builder` | `-v <vert> [-g <geom>] -f <frag> -m <mesh-spec>` | M11 |
| ModelConvert | `model-convert` | `<file> -s specfile [-o <out>]` | M11 |

## Test seam inventory

Existing tests are the prior art for VPP regression coverage. Prefer these seams
over new low-level hooks. All were run against the frozen reference on Linux (see
[M01-smoke-evidence.md](M01-smoke-evidence.md)).

| Test seam | Host | Kind | Runs via |
| --- | --- | --- | --- |
| `RenderGraphTests` | `mpp` | CPU graph compile/validate | `PipelineEditor --validate <yaml>` (CTest `mpp_render_graph_cpu`) |
| `RenderGraphGpuTests` | `mpp` | GPU render-graph behaviour | `PipelineEditor --gpu-tests` (CTest `mpp_render_graph_gpu`) |
| `PbrMaterialTests` | `mpp` | PBR material/specialization | `mpp` test build |
| `ParticleSystemTests` / `ParticleGpuTests` | `mpp` | Particle lifecycle + GPU | `DemoSuite --particle-tests` |
| `DiagnosticTests` | `mpp` | Diagnostics catalogue | `mpp` test build |
| `MaterialResourceTests` / `ParticleResourceTests` / `RenderGraphResourceTests` | `mpp-resource-parsers` | Parser/serializer round-trip | resource test build / `--resource-tests` |
| `DocumentFoundationTests` / `RenderSystemConfigTests` | `mpp-app-support` | Document + config CPU | app-support test build |
| Particle document/preview/resource/spatial/control | `particle-editor` | Context-free document + preview | `ParticleEditor --*-tests` |
| Package smoke | `demo-suite` | 30-frame integration | `DemoSuite --package … --package-smoke-test` |

**Gap for M01:** these are the *reference's* tests. The VPP-side shared comparison
client (built against VPP, loading the same scenes) is established as a contract in
[`../comparison/m01-basics/SCENARIO.md`](../comparison/m01-basics/SCENARIO.md)
and becomes runnable when the M1 rendering slice lands.
