# Vulkan Poly Pusher

Native Vulkan migration of Massive Poly Pusher, preserving its public source and
authored-resource contracts. Specification: [issue #1](https://github.com/ajare/vulkan-poly-pusher/issues/1).

**Current runnable subset: CPU authored assets plus native Vulkan coloured triangles.**
The existing window sequence presents clears and public `Program` / `BasicMaterial`
/ `Mesh::render` draws, with staged geometry uploads, runtime shader compilation,
descriptor-backed uniforms, resize and diagnostic capture. No GL context exists.
Textures, depth, full scene rendering and complete applications are not yet delivered.
Windows execution and reliable compositor minimize/restore remain unqualified;
Milestone 1 is not qualified.

```sh
git submodule update --init --recursive
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DVPP_BUILD_RENDERER=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
build/bin/VppAssetValidate scene docs/comparison/m01-basics/assets/DefaultPbrPreview.scene.yaml
```

Requires C++20, CMake 3.22, Vulkan 1.3+ development files, and Python for tests.
The GPU CTest requires an X11 display on Linux and the Khronos validation layer.
Use `-DVPP_BUILD_RENDERER=OFF` for a context-free CPU-only build. See
[Triangle drawing, shared captures and supported subset](docs/migration/M01-triangle-draw.md),
[Vulkan clear subset and evidence](docs/migration/M01-vulkan-clear.md),
[CPU subset, comparison instructions and evidence](docs/migration/M01-cpu-assets.md)
and [migration inventory/status](docs/migration/README.md).
