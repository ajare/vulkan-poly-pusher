#pragma once
// Additive diagnostics for the Vulkan program/material realization subset
// (#95). They expose observable results of native realization — never Vulkan
// handles — so tests can verify that authored uniform values reach the
// shader's descriptor-backed storage without inspecting private structure.
// Explicit diagnostic readback may wait for its own submission; uniform
// uploads and presentation never wait device-wide.
#include <cstddef>
#include <cstdint>
#include <vector>
#include "mpp/Config.h"
namespace mpp {
class Program;
// Number of presentation frame contexts each realized program carries
// descriptor-backed uniform storage for.
_MPPAPI size_t programFrameContextCount();
// Byte size of the program's generated `_mpp_Uniforms` block (0 when the
// program declares no generated uniforms). Fails actionably when the program
// is not realized.
_MPPAPI size_t programUniformBlockSize(Program const& program);
// Byte-exact contents of the program's uniform-block storage for one frame
// context. Fails actionably when the program is not realized, declares no
// generated uniforms, or the frame context does not exist.
_MPPAPI std::vector<uint8_t> readbackProgramUniforms(Program const& program, size_t frameContext);
}
