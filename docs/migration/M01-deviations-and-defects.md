# M01 — Deviations and baseline-defect candidates

Policy (spec #1): the frozen reference is **never** changed to excuse a regression.
Intentional deviations and observed baseline defects are recorded and approved
**separately**. Nothing here is fixed silently.

## Baseline-defect candidates (found during M01 inventory)

Both are in the file-texture internal-format name→GL-enum table and are
**inconsistent with the surrounding pattern**. They are recorded as
investigation/approval items only. **No code was changed.**

### BD-1 — `R16_SNORM` maps to a two-channel format

- Location: `mpp-resource-parsers/src/FileTextureStream.cpp:53`
- Code: `internalFormats["R16_SNORM"] = GL_RG16_SNORM;`
- Observation: a single-channel name (`R16_SNORM`) is mapped to the two-channel
  `GL_RG16_SNORM`. The neighbouring single-channel entries follow the expected
  pattern (`R8_SNORM → GL_R8_SNORM`, `R8 → GL_R8`, `R16 → GL_R16`,
  `R16F → GL_R16F`), and the three other 16-bit single-channel sites in `mpp`
  (`Texture.cpp:140`, `ProgrammaticTextureStream.cpp:157`,
  `ProgrammaticRenderTextureStream.cpp:180`) all correctly use `GL_R16_SNORM`.
- Status: **candidate, matches the item flagged in spec #1's Further Notes.**
  Not authorized to change. Needs: confirm whether any authored content actually
  requests `R16_SNORM` through this parser; if so, decide whether VPP preserves
  the (arguably wrong) behaviour for parity or files an approved deviation.

### BD-2 — `RG16_SNORM` maps to a three-channel format

- Location: `mpp-resource-parsers/src/FileTextureStream.cpp:54`
- Code: `internalFormats["RG16_SNORM"] = GL_RGB16_SNORM;`
- Observation: a two-channel name (`RG16_SNORM`) is mapped to the three-channel
  `GL_RGB16_SNORM`. Same surrounding pattern as BD-1 (`RG8_SNORM → GL_RG8_SNORM`,
  `RG16 → GL_RG16`, `RG16F → GL_RG16F`). This one was **not** pre-flagged in the
  spec; it surfaced during the M01 inventory.
- Status: **candidate, newly observed.** Same handling as BD-1: investigate real
  usage, then decide parity vs. approved deviation. Do not silently correct.

### BD-3 — ProgramBuilder's sample uses an unsupported type spelling

- Location: `program-builder/resources/test.modelspec`, colour channel `type="uint8"`.
- Observation: frozen ProgramBuilder and VPP both return exit 1 with
  `Invalid type: uint8`. The retained `SpecificationParser` recognizes `uint8_t`,
  not `uint8` (the separate resource-parser vocabulary is not interchangeable).
- Status: **sample-defect candidate; not fixed**. The imported sample remains
  byte-identical. A separate `tests/fixtures/program.modelspec` uses the existing
  `uint8_t` spelling to exercise the valid tool path; both clients return exit 0.
- BD-1 and BD-2 were subsequently confirmed by live CPU client comparison in #3;
  VPP preserves both original mappings. See `M01-cpu-assets.md`.

### BD-4 — `Mesh::setIndexData(vector, indexWidth)` computes element count with `>> 8`

- Location: `mpp/src/Mesh.cpp` (`setIndexData(std::vector<uint8_t> const&, size_t)`)
- Observation: the reference computes `numIndices = indexData.size() / (indexWidth >> 8)`.
  `indexWidth` is in bits (16 or 32; the sibling overload uses `>> 3` for byte
  conversion), so `indexWidth >> 8` is zero for every supported width — a
  division by zero. No compiled MPP code path reaches this overload with a
  valid width; it is a latent baseline defect.
- VPP behaviour: the native implementation uses the intended `>> 3` conversion
  and rejects non-byte-aligned widths actionably.
- Status: **candidate; VPP implements the intended behaviour.** Recorded per
  policy; not authorized to change the frozen reference. Pinning test: the
  geometry-upload scenario loads a 16-bit indexed mesh through this overload's
  semantics (`tools/GeometryUpload.cpp`).

## Approved compatibility exceptions

_None yet._ Any header that cannot be preserved exactly (e.g. a GL-dependent
public declaration that must change) must be added here with: the header, the
change, the reason, and explicit approval — before it is treated as an accepted
exception at the source-compatibility seam.

## Deviation-tracking convention

For each future deviation record:

| Field | Meaning |
| --- | --- |
| ID | `BD-n` (baseline defect) or `EX-n` (approved exception) |
| Location | file:line / header |
| Reference behaviour | what frozen MPP does |
| VPP behaviour | what VPP will do |
| Justification | why the deviation is acceptable |
| Approval | who approved, and where (issue/PR link) |
| Test | the observable-behaviour test that pins the chosen behaviour |
