# Migration documentation — Vulkan Poly Pusher (VPP)

This directory holds the working artifacts for the native Vulkan migration of
Massive Poly Pusher (MPP), governed by the umbrella specification in GitHub issue
**#1** and delivered through twelve cumulative milestones.

## Frozen reference (immutable)

| Field | Value |
| --- | --- |
| Repository | `ajare/massive-poly-pusher` |
| Branch | `vulkan-migration-target` |
| Commit | `21b1bcff040a4cfd32e3a5e814b9a4092d2b7cef` |
| Submodules | `ext/assimp c35200e3`, `ext/glew 285c35d3` (glew-2.3.1), `ext/sdl 147a8ee3`, `ext/utils eae7140c7` (→ `vendor/yaml-cpp c3f81127`) |
| Large assets | Git LFS (28 files: `*.mppmodel`, `*.png/.tga/.jpg/.exr`, `*.gltf`, `*.mpppackage`) |

The reference branch and its checkout are **never modified**. All VPP work is done
in this repository; the reference is reproduced read-only for comparison.

## Milestone 1 (ticket #2) — artifacts

| File | Purpose |
| --- | --- |
| [`M01-reference-setup.md`](M01-reference-setup.md) | Reproducible Windows/Linux reproduction of the frozen reference; what was verified here and what infrastructure is missing. |
| [`M01-coverage-inventory.md`](M01-coverage-inventory.md) | Human-readable coverage map: modules, rendering features, application paths, and test seams → milestone + classification. |
| [`coverage-inventory.csv`](coverage-inventory.csv) | Machine-readable per-public-header inventory (258 headers) with category + target milestone. Regenerate with [`gen_inventory.sh`](gen_inventory.sh). |
| [`M01-smoke-evidence.md`](M01-smoke-evidence.md) | Exact commands, environment fingerprints, and results of the reference build + smoke + delivered test runs. |
| [`M01-deviations-and-defects.md`](M01-deviations-and-defects.md) | Observed baseline-defect candidates and the approval policy. Nothing is fixed silently. |
| [`../comparison/m01-basics/SCENARIO.md`](../comparison/m01-basics/SCENARIO.md) | The M01 shared "Basics" comparison-scenario contract (loads in both MPP and VPP clients). |

## Milestone 1 (ticket #3) — CPU assets

[`M01-cpu-assets.md`](M01-cpu-assets.md) records the runnable context-free module
subset, shared comparison client, original regressions, exact reference comparisons,
public declaration checks, narrow GL decoupling and platform qualification gaps.

## Milestone 1 (ticket #4) — Vulkan clear/present

[`M01-vulkan-clear.md`](M01-vulkan-clear.md) records the native window/device/
swapchain slice, shared clear captures, dependency pins, validation and lifecycle
results, and remaining platform/compositor qualification gaps.

## Milestone 1 (ticket #93) — runtime shader compilation

[`M01-shader-compilation.md`](M01-shader-compilation.md) records the pinned,
private glslang/SPIRV-Reflect toolchain, runtime program-stream compilation,
backend resource-root variants, reflected bindings, CPU regressions and subset.

## Supported intermediate subset (explicit)

Early milestones expose **only their documented supported subset**. Not-yet-delivered
capabilities must **fail explicitly**, never masquerade as a successful no-op.

The delivered subset is the **reference reproduction + coverage inventory +
comparison-scenario contract + CPU authored-asset loading/validation and round trips**.
All eight original module targets have explicit CPU subsets; ProgramBuilder and
`VppAssetValidate` run without a graphics context. VPP now also supports **native
Vulkan window initialization, clear, presentation and diagnostic clear capture**
through the existing public window sequence, plus **runtime Vulkan shader
compilation and dependency-neutral SPIR-V reflection** through program streams.
Uploads, GPU program/material realization, geometry and depth remain for the rest
of Milestone 1.
Until a given VPP capability exists, invoking it must raise a clear
"not implemented" diagnostic rather than return success or draw nothing silently.
This rule is a standing gate for every subsequent milestone, not just M01.

## Qualification status — Milestone 1

| Acceptance area | Status |
| --- | --- |
| Frozen reference builds on Linux | ✅ Verified (Release, all 5 binaries) |
| Frozen reference smoke-runs on Linux | ✅ Verified (`DemoSuite --package-smoke-test`, 30 frames) |
| Delivered MPP tests pass on Linux | ✅ Verified (context-free + GPU + CTest) |
| Coverage inventory complete | ✅ Delivered (this directory) |
| Baseline defects recorded, not fixed | ✅ Delivered (3 candidates) |
| VPP CPU assets/regressions/interface checks on Linux | ✅ Verified Release + Debug (ticket #3) |
| VPP CPU assets on Windows | ⛔ **Unverified — no Windows runner** |
| **Windows reference build + smoke** | ⛔ **BLOCKED — no Windows runner available** |
| **Khronos Vulkan validation layer installed** | ✅ **Installed + verified loadable** (`VK_LAYER_KHRONOS_validation`, Vulkan 1.4.341) |
| **VPP Vulkan / sync-validation results** | ✅ Clear/resize/shutdown on RADV + lavapipe, zero diagnostics (#4) |
| Shared frozen-MPP/VPP clear captures | ✅ Both resolutions, maximum difference 0/255 (#4) |
| Minimize/restore | ⛔ Intermittent compositor restore failure; optional scenario retains failing assertion (#4) |

Per spec #1, **missing Windows evidence blocks milestone sign-off** and is reported
here rather than treated as a pass. See
[M01-smoke-evidence.md](M01-smoke-evidence.md) for the exact gap and how to close it.

> **Validation scope:** #4 produces actual Vulkan and synchronization-validation
> evidence for the delivered clear subset only. It does not qualify unimplemented
> uploads, shaders, geometry, depth or complete applications. Frozen MPP is OpenGL
> and is compared through a separately built shared client, not Vulkan validation.
