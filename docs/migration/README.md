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

## Supported intermediate subset (explicit)

Early milestones expose **only their documented supported subset**. Not-yet-delivered
capabilities must **fail explicitly**, never masquerade as a successful no-op.

As of M01 the delivered subset is the **reference reproduction + coverage
inventory + comparison-scenario contract**. VPP itself has **no rendering
capability yet** — the first VPP rendering slice (init, uploads, minimal
shader/material, depth, presentation, capture) lands with the rest of Milestone 1.
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
| Baseline defects recorded, not fixed | ✅ Delivered (2 candidates) |
| **Windows reference build + smoke** | ⛔ **BLOCKED — no Windows runner available** |
| **Khronos Vulkan validation layer installed** | ✅ **Installed + verified loadable** (`VK_LAYER_KHRONOS_validation`, Vulkan 1.4.341) |
| **VPP Vulkan / sync-validation results** | ⏳ **Deferred — no VPP renderer yet** (see note) |

Per spec #1, **missing Windows evidence blocks milestone sign-off** and is reported
here rather than treated as a pass. See
[M01-smoke-evidence.md](M01-smoke-evidence.md) for the exact gap and how to close it.

> **Validation-layer note:** the layer is now installed and confirmed to load into a
> real Vulkan instance. However, the frozen MPP reference is an **OpenGL** renderer
> (links `libGLEW`/`libGLX`, makes **zero** Vulkan calls), so there is nothing in
> the M01 deliverable for the Vulkan validation layer to validate. Vulkan +
> synchronization-validation results become meaningful only when the VPP rendering
> slice lands (rest of Milestone 1); running the covered scenarios under
> `VK_LAYER_KHRONOS_validation` (with sync-validation feature enabled) is a
> standing gate for that sign-off, not something M01 can produce today.
