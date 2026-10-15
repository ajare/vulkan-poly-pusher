#pragma once
// Private std140 uniform-block writes for the Vulkan program/material subset.
// Material and engine uniform values are packed into the CPU image of a
// realized program's generated `_mpp_Uniforms` block using the reflected
// member offsets delivered in #93, then staged into per-frame-context uniform
// buffers. Pure CPU logic: CPU regressions exercise it without a device.
//
// Retained reference semantics: uniforms the program does not declare are
// sparse no-ops (pass-scoped overrides stay valid), and unsupported
// type/shape combinations throw instead of corrupting neighbouring members.
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>
#include "mpp/CompiledProgram.h"
#include "mpp/program/glslTypes.h"

namespace mpp::detail {

inline std::string_view uniformMemberBase(std::string const& name) {
    // Reflection reports fixed arrays with a "[0]" suffix stripped nowhere;
    // normalize so authored names match both scalar and array members.
    if (name.size() > 3 && name.compare(name.size() - 3, 3, "[0]") == 0)
        return std::string_view(name).substr(0, name.size() - 3);
    return name;
}

// Write one named value into the block image. Returns false when the program
// declares no such member (a retained sparse-override no-op); throws an
// actionable diagnostic naming the program and member on type/shape mismatch.
inline bool writeUniformValue(std::vector<UniformMember> const& members, std::vector<uint8_t>& image,
    std::string const& markedName, program::GLSLType type, size_t count, size_t numElements,
    void const* data, std::string const& programName) {
    auto const fail = [&](std::string const& message) -> void {
        throw std::runtime_error("Program '" + programName + "' uniform '" + markedName + "': " + message);
    };
    UniformMember const* member = nullptr;
    for (auto const& candidate : members)
        if (uniformMemberBase(candidate.name) == markedName) { member = &candidate; break; }
    if (!member) return false;
    if (!count || !data) fail("no data supplied");
    if (member->scalarWidth != 32) fail("64-bit uniforms are not delivered; use 32-bit types");
    if (member->scalar == ShaderScalar::Boolean) fail("boolean uniforms are not supported by UniformCollection");
    if (member->rowMajor) fail("row-major matrix members are not delivered; retain column-major layout");
    if (member->scalar == ShaderScalar::Float && member->columns == 0 && type == program::GLSLType::FloatMatrix)
        fail("matrix value does not match the declared vector member");

    size_t const stride = member->arrayDimensions.empty() ? 0 : member->arrayStride;
    if (!member->arrayDimensions.empty()) {
        if (!stride) fail("reflected array member has no stride");
        if (count > member->arrayDimensions.front())
            fail("writing " + std::to_string(count) + " elements exceeds the declared " +
                std::to_string(member->arrayDimensions.front()) + "-element array");
    } else if (count != 1) {
        fail("writing " + std::to_string(count) + " elements into a non-array member");
    }

    auto writeBytes = [&](size_t offset, void const* source, size_t size) {
        if (offset > image.size() || size > image.size() - offset)
            fail("reflected member range exceeds the uniform block image");
        std::memcpy(image.data() + offset, source, size);
    };
    auto const* bytes = static_cast<uint8_t const*>(data);
    for (size_t element = 0; element < count; ++element) {
        auto const base = member->offset + element * stride;
        switch (type) {
        case program::GLSLType::Int:
            if (member->scalar != ShaderScalar::SignedInt || member->columns || member->components != numElements)
                fail("type mismatch: expected an integer member with " + std::to_string(member->components) + " components");
            writeBytes(base, bytes + element * numElements * 4, numElements * 4);
            break;
        case program::GLSLType::Uint:
            if (member->scalar != ShaderScalar::UnsignedInt || member->columns || member->components != numElements)
                fail("type mismatch: expected an unsigned-integer member with " + std::to_string(member->components) + " components");
            writeBytes(base, bytes + element * numElements * 4, numElements * 4);
            break;
        case program::GLSLType::Float:
            if (member->scalar != ShaderScalar::Float || member->columns || member->components != numElements)
                fail("type mismatch: expected a float member with " + std::to_string(member->components) + " components");
            writeBytes(base, bytes + element * numElements * 4, numElements * 4);
            break;
        case program::GLSLType::FloatMatrix:
            // Retained contract: only mat4 UniformCollection values are supported.
            if (numElements != 4) fail("only mat4 UniformCollection values are supported");
            if (member->scalar != ShaderScalar::Float || member->components != 4 || member->columns != 4)
                fail("type mismatch: expected a mat4 float member");
            if (!member->matrixStride) fail("reflected matrix member has no column stride");
            for (size_t column = 0; column < 4; ++column)
                writeBytes(base + column * member->matrixStride, bytes + (element * 16 + column * 4) * 4, 16);
            break;
        default:
            fail("unsupported uniform type");
        }
    }
    return true;
}

}
