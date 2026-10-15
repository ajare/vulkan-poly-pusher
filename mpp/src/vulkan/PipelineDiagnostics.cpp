#include "mpp/PipelineDiagnostics.h"
#include "mpp/Program.h"
#include "WindowBridge.h"
#include "PipelineStore.h"
#include <stdexcept>

namespace mpp {
namespace {
detail::PipelineStore& store(Program const& program) {
    if (program.getId() == 0)
        throw std::runtime_error("Program '" + program.getName() + "' is not realized (call load() first)");
    return detail::associatedWindow()->pipelines();
}
}
size_t programFrameContextCount() {
    return detail::associatedWindow()->pipelines().frameContextCount();
}
size_t programUniformBlockSize(Program const& program) {
    return store(program).uniformBlockSize(program.getId());
}
std::vector<uint8_t> readbackProgramUniforms(Program const& program, size_t frameContext) {
    return store(program).readbackUniforms(program.getId(), frameContext);
}
}
