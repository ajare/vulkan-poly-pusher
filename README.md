# Vulkan Poly Pusher

Native Vulkan migration of Massive Poly Pusher, preserving its public source and
authored-resource contracts. Specification: [issue #1](https://github.com/ajare/vulkan-poly-pusher/issues/1).

**Current runnable subset: CPU authored-asset validation and serialization only.**
No renderer or graphics context is created. Windows execution remains unverified;
Milestone 1 is not qualified.

```sh
git submodule update --init --recursive
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
build/bin/VppAssetValidate scene docs/comparison/m01-basics/assets/DefaultPbrPreview.scene.yaml
```

Requires C++20 and CMake 3.22; Python for tests. See
[CPU subset, comparison instructions and evidence](docs/migration/M01-cpu-assets.md)
and [migration inventory/status](docs/migration/README.md).
