#pragma once
// Additive diagnostics for the Vulkan geometry-upload subset. Labels follow
// "Mesh '<name>' vertex buffer <i>" and "Mesh '<name>' index buffer", bound at
// Mesh::load(). Explicit diagnostic readback may wait for its own submission;
// ordinary uploads and presentation never wait device-wide.
#include <cstdint>
#include <string>
#include <vector>
#include "mpp/Config.h"
namespace mpp {
// Labels of currently live uploaded geometry buffers.
_MPPAPI std::vector<std::string> uploadedGeometryBuffers();
// Byte-exact contents of a live uploaded buffer. Fails actionably when no
// live buffer carries the label.
_MPPAPI std::vector<uint8_t> readbackUploadedGeometry(std::string const& label);
}
