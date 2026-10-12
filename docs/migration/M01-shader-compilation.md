# M01 runtime Vulkan shader compilation — #93

## Dependency pins and build

Initialize with `git submodule update --init --recursive`, then build using the
root CMake project. These are implementation-private dependencies of
`MassivePolyPusher`, including when `VPP_BUILD_RENDERER=OFF`. No SDK binaries,
network fetches during configuration, external SPIRV-Tools, or offline compiler
are required. C is enabled for the reflection library; C++20 is retained.

| Dependency | Revision | License / integration |
| --- | --- | --- |
| glslang 15.1.0 | `1062752a891c95b2bfeed9e356562d88f9df84ac` | `ext/glslang/LICENSE.txt`: bundled BSD-3-Clause, BSD-2-Clause, MIT, Apache-2.0, GPL-3.0 with Bison exception for checked-in generated parser output, and additional permissive preprocessor notices. No Bison build dependency or grammar regeneration is required. Retain the complete notices when distributing. Static PIC `glslang` and default resource limits; tools, tests, HLSL, remapper, external dependencies and optimizer disabled. |
| SPIRV-Reflect (Vulkan SDK 1.4.304.0) | `281b55edb67b1f2e61e1dc92fdc42a650da05d83` | Apache-2.0, `ext/spirv-reflect/LICENSE`; compile `spirv_reflect.c` as a static PIC implementation dependency using its bundled SPIR-V headers. |

Neither dependency is linked into public CMake usage requirements or included by
public module headers. The additive `mpp/CompiledProgram.h` contains only engine
metadata and standard-library types. All frozen declarations remain byte-for-byte
unchanged. Distribution must include the dependencies' complete license notices;
this source/build verification does not claim packaged release qualification.

## Existing resource path and runtime authoring

```cpp
#include <mpp/CompiledProgram.h>
#include <mpp/resource-parsers/FileProgramStream.h>

mpp::resource_parsers::FileProgramStream stream(nullptr,
    "docs/comparison/m01-basics/shaders/vpp/Triangle.program.yaml");
auto compiled = mpp::compileProgram(stream, "Basics.Triangle");
// compiled->vertex/fragment.spirv and reflected inputs/outputs/bindings
```

`compileProgram` loads an ordinary program stream, consumes the existing parser's
generated GLSL, then compiles at runtime to Vulkan 1.3 / SPIR-V 1.6. The same API
accepts `ProgrammaticProgramStream` with a `program::Parser`, so editors can
unload, change source, and recompile. Parsing and stream loading alone remain CPU
operations: loading legacy shaders does not force Vulkan compilation. Parser
warning/error collection and existing throw behaviour are retained; Vulkan
compilation adds named stage/source diagnostics for invalid generated GLSL.
An unset programmatic parser fails explicitly rather than dereferencing null.

Compiled immutable results are cached by the existing length-prefixed
source/mesh-layout key (up to 64 results); failed compiles are never cached. The
compiler/cache is mutex-serialized. Cache eviction does not invalidate results
held by callers. No GPU handles or resource IDs are allocated here.

### Backend variants under one logical name

The existing resource-root selection workflow is unchanged. Both
`shaders/mpp/Triangle.program.yaml` and `shaders/vpp/Triangle.program.yaml` name
`Basics.Triangle` and use the same mesh and colour semantics. Select the MPP or
VPP root in the backend's resource configuration, not a new logical shader name.
The VPP vertex body ports clip depth to `[0,w]`; the MPP variant keeps `[-w,w]`.
This is a compilation fixture for the shared Basics scene, not a completed draw
comparison. No resource schema or serialization changes are introduced.

## Reflection and binding contract

- `@ViewPos`, `@MMatrix`, `@MCPMatrix`, `@NormalMatrix` and other existing tokens
  pass through the retained parser. `_mpp_*` variable names survive into SPIR-V
  metadata. No manually maintained uniform/member binding table is used.
- Parser-generated loose `_mpp_u_*` uniforms are lifted to a shared std140
  `_mpp_Uniforms` block at set 0, binding 0. Both stages receive the same sorted
  union of declarations, yielding identical member offsets. Conflicting types
  fail. That slot is reserved whenever generated loose uniforms exist.
- Generated `_mpp_t_*` sampler2D uniforms receive set 0 bindings 1 onward in
  sorted name order. Authored explicit Vulkan uniform blocks and sampler2D
  bindings remain unchanged; missing/negative/duplicate bindings fail clearly.
- Reflection reports vertex/stage inputs and outputs with locations, numeric
  scalar category/width, vector components and matrix columns; descriptors with
  set, binding, kind and array count; blocks with padded byte size and named
  members' offsets, sizes, array dimensions/stride, matrix stride and major order.
- MPP's generated stage input/output names differ. Stages are compiled/linked
  separately and matched by reflected location and numeric type, as Vulkan
  requires. Cross-stage descriptor collisions must agree on kind, count, block
  size, name and member metadata; incompatibilities fail before caching.

## Supported intermediate subset

This slice compiles vertex/fragment programs, plain scalar/vector/matrix stage
variables, flat uniform blocks (including fixed member arrays), and 2D combined
image samplers with fixed descriptor counts. Geometry shaders, push constants,
interface blocks/arrays, nested uniform structs, runtime descriptor arrays,
storage buffers/images, separate image/sampler descriptors, and non-2D,
arrayed or multisampled image descriptors are rejected explicitly.
Other invalid Vulkan GLSL is rejected by glslang with logical name, stage and
line diagnostics. Arbitrary OpenGL bodies are not promised unchanged execution.

Shader module creation, device-limit checks, pipeline/descriptor allocation,
material uniform uploads and draws are **not** delivered by this ticket; they
belong to #95/#96. The existing unsupported resource realization/binding methods
still fail rather than pretending to draw. No complete M01 sign-off is claimed.

## Linux evidence

Release build, GCC 15.2.0, on the existing Linux development machine:

```sh
cmake -S . -B build
cmake --build build -j 8
ctest --test-dir build --output-on-failure
cmake -S . -B build/cpu-only -DVPP_BUILD_RENDERER=OFF
cmake --build build/cpu-only -j 8
ctest --test-dir build/cpu-only --output-on-failure
python3 tests/compare_assets.py build/bin/VppAssetValidate \
  --reference build/reference/MppAssetValidate
```

Results: renderer build **5/5 CTests pass**; CPU-only build **4/4 pass**;
**74 authored-resource observations match frozen MPP exactly**. Public-header
audit passes all 258 frozen headers; the public compilation seam also includes
the new dependency-neutral header. The shader cases extend the existing CPU test
executable through real file/programmatic streams, testing SPIR-V, authored
vertex/stage variables, preserved built-ins, uniform member layout, generated
and explicit bindings, runtime edits/reloads, and named failures (missing stages,
invalid GLSL/version/bindings, descriptor collisions, stage mismatch and unsupported
features). Assertions inspect client-visible results rather than Vulkan internals.

The delivered clear/present CTest also passes with Vulkan and synchronization
validation. That GPU evidence still covers **clear/present only**, not compiled
shaders executing on a device. Windows remains **unverified: no Windows runner**;
missing Windows evidence continues to block milestone qualification, not silently
count as a pass. The frozen MPP checkout was not changed.
