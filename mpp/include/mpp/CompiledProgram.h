#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include "mpp/Config.h"

namespace mpp {
class ProgramStream;
// Additive, backend-dependency-free runtime compilation result. No GPU objects
// are created. Immutable results can be shared with later pipeline realization.
enum class ShaderScalar { Float, SignedInt, UnsignedInt, Boolean };
struct ShaderVariable {
    std::string name;
    uint32_t location{}, components{}, columns{}, scalarWidth{};
    ShaderScalar scalar{};
};
struct UniformMember {
    std::string name;
    uint32_t offset{}, size{}, arrayStride{}, matrixStride{};
    uint32_t components{}, columns{}, scalarWidth{};
    ShaderScalar scalar{};
    std::vector<uint32_t> arrayDimensions;
    bool rowMajor{};
    bool operator==(UniformMember const&) const = default;
};
struct ShaderBinding {
    enum class Kind { UniformBlock, CombinedImageSampler };
    std::string name;
    uint32_t set{}, binding{}, count{}, size{};
    Kind kind{};
    std::vector<UniformMember> members;
};
struct CompiledShader {
    std::vector<uint32_t> spirv;
    std::vector<ShaderVariable> inputs, outputs;
    std::vector<ShaderBinding> bindings;
};
struct CompiledProgram {
    CompiledShader vertex, fragment;
};
_MPPAPI std::shared_ptr<CompiledProgram const> compileProgram(ProgramStream& stream, std::string const& logicalName);
}
