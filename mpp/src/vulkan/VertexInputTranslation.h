#pragma once
// Private Vulkan-boundary mapping from an authored mesh layout (the geometry
// layouts delivered in #94) and a compiled program's reflected vertex inputs
// (#93) to native vertex-input state. Pure CPU logic: CPU regressions exercise
// it without a device. Every mismatch fails with an actionable diagnostic
// naming the program and the offending attribute; nothing is silently
// reinterpreted.
#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>
#include <vulkan/vulkan_core.h>
#include "mpp/CompiledProgram.h"
#include "mpp/mesh/MeshSpecification.h"
#include "VertexLayoutTranslation.h"

namespace mpp::detail {

struct VertexInputState {
    std::vector<VkVertexInputBindingDescription> bindings;
    std::vector<VkVertexInputAttributeDescription> attributes;
    VkPrimitiveTopology topology{};
};

inline VkPrimitiveTopology translateTopology(mesh::Primitive::Type type) {
    switch (type) {
    case mesh::Primitive::Type::Points:    return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
    case mesh::Primitive::Type::Lines:     return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
    case mesh::Primitive::Type::Triangles: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    }
    throw std::runtime_error("Unknown primitive topology vocabulary value");
}

// The shader-input scalar class an authored attribute produces at the native
// boundary. Mirrors the format translation: normalized integer attributes and
// float/half attributes feed float shader inputs; plain integer attributes
// feed integer shader inputs.
inline ShaderScalar attributeScalarClass(mesh::Vertex::DataType dataType, bool normalised, std::string const& context) {
    switch (dataType) {
    case mesh::Vertex::DataType::Float:
    case mesh::Vertex::DataType::HalfFloat:
        return ShaderScalar::Float;
    case mesh::Vertex::DataType::Byte:
    case mesh::Vertex::DataType::Short:
    case mesh::Vertex::DataType::Int:
        return normalised ? ShaderScalar::Float : ShaderScalar::SignedInt;
    case mesh::Vertex::DataType::UnsignedByte:
    case mesh::Vertex::DataType::UnsignedShort:
    case mesh::Vertex::DataType::UnsignedInt:
        return normalised ? ShaderScalar::Float : ShaderScalar::UnsignedInt;
    case mesh::Vertex::DataType::Int_2_10_10_10_REV:
        return normalised ? ShaderScalar::Float : ShaderScalar::SignedInt;
    case mesh::Vertex::DataType::UnsignedInt_2_10_10_10_REV:
        return normalised ? ShaderScalar::Float : ShaderScalar::UnsignedInt;
    default: break;
    }
    throw std::runtime_error(context + ": double-precision vertex attributes are not delivered; use float attributes");
}

// Match reflected vertex inputs (by ascending location) against the flattened
// authored attribute layouts in declaration order — the same sequential
// assignment the retained parser uses when generating input locations.
inline VertexInputState buildVertexInputState(mesh::MeshSpecification const& spec,
    std::vector<ShaderVariable> inputs, std::string const& programName) {
    auto const fail = [&](std::string const& message) -> void {
        throw std::runtime_error("Program '" + programName + "': " + message);
    };
    std::sort(inputs.begin(), inputs.end(), [](auto const& a, auto const& b) { return a.location < b.location; });
    for (auto const& input : inputs) {
        if (input.columns > 1)
            fail("matrix vertex input '" + input.name + "' (location " + std::to_string(input.location) +
                ") is not delivered; use vector attributes");
        if (input.scalar == ShaderScalar::Boolean)
            fail("boolean vertex input '" + input.name + "' (location " + std::to_string(input.location) + ") is not supported");
    }

    struct FlatAttribute {
        mesh::VertexBufferAttributeLayout::Attribute const* attribute;
        uint32_t binding;
        uint32_t stride;
    };
    std::vector<FlatAttribute> flat;
    for (uint32_t layout = 0; layout < spec.getNumVertexBufferAttributeLayouts(); ++layout) {
        auto const& attributes = spec.getVertexBufferAttributeLayout(layout);
        for (size_t i = 0; i < attributes.getNumAttributes(); ++i)
            flat.push_back({ &attributes.getAttribute(i), layout, uint32_t(attributes.getVertexSize()) });
    }
    if (inputs.size() != flat.size())
        fail("vertex input mismatch: shader declares " + std::to_string(inputs.size()) +
            " inputs but the mesh specification supplies " + std::to_string(flat.size()) +
            " attributes; align the authored MeshSpecification with the shader's @In inputs");

    VertexInputState state;
    state.topology = translateTopology(spec.getPrimitiveType());
    for (uint32_t layout = 0; layout < spec.getNumVertexBufferAttributeLayouts(); ++layout)
        state.bindings.push_back({ layout, uint32_t(spec.getVertexBufferAttributeLayout(layout).getVertexSize()), VK_VERTEX_INPUT_RATE_VERTEX });
    for (size_t i = 0; i < inputs.size(); ++i) {
        auto const& input = inputs[i];
        auto const& attribute = *flat[i].attribute;
        auto const components = mesh::Vertex::getComponentSize(attribute.component);
        auto const context = "vertex attribute '" + input.name + "' (location " + std::to_string(input.location) + ")";
        auto describe = [&] {
            return attribute.identifier.empty() ? mesh::Vertex::getComponentName(attribute.component) : attribute.identifier;
        };
        if (components != input.components)
            fail(context + " declares " + std::to_string(input.components) + " components but mesh specification attribute '" +
                describe() + "' supplies " + std::to_string(components));
        auto const shaderClass = attributeScalarClass(attribute.dataType, attribute.normalised, context);
        if (shaderClass != input.scalar)
            fail(context + " is " + std::string(input.scalar == ShaderScalar::Float ? "float" : "integer") +
                " in the shader but mesh specification attribute '" + describe() + "' produces " +
                (shaderClass == ShaderScalar::Float ? "float" : "integer") + " values");
        state.attributes.push_back({ input.location, flat[i].binding,
            translateVertexFormat(legacyVertexDataTypeConstant(attribute.dataType), components, attribute.normalised),
            uint32_t(attribute.offsetInBytes) });
    }
    return state;
}

}
