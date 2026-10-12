#pragma once
#include <cstdint>
#include <string>
#include "mpp/Config.h"
namespace mpp {
class RenderSystem;
// Additive query: the retained Caps GL version/profile fields remain zero/false.
struct BackendCapabilities {
    std::string backend;
    std::string device;
    uint32_t apiVersionMajor{}, apiVersionMinor{}, apiVersionPatch{};
    bool clearPresentation{};
    bool shaderCompilation{};
    std::string shaderDialect;
};
_MPPAPI BackendCapabilities getBackendCapabilities(RenderSystem const&);
// Diagnostic only: captures the next presented clear as top-left RGB PPM.
// This may wait for GPU completion; ordinary presentation does not.
_MPPAPI void captureNextPresent(RenderSystem&, std::string const& path);
}
