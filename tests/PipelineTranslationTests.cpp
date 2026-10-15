// CPU regressions for the pure Vulkan-boundary pipeline translation (#95):
// authored mesh layout + reflected inputs -> native vertex input state, and
// std140 uniform-block writes into the generated `_mpp_Uniforms` image.
// Observable contracts (locations, formats, offsets, diagnostics) only; no
// device is required.
#include "vulkan/VertexInputTranslation.h"
#include "vulkan/UniformBlockWrite.h"
#include <cstring>
#include <stdexcept>

namespace {
void require(bool condition, char const* message) { if (!condition) throw std::runtime_error(message); }

mpp::mesh::MeshSpecification triangleSpec() {
    mpp::mesh::MeshSpecification spec(mpp::mesh::Primitive::Type::Triangles);
    auto* layout = spec.createVertexBufferAttributeLayout(true);
    layout->createAttribute(mpp::mesh::Vertex::Component::Position3, mpp::mesh::Vertex::DataType::Float, false);
    layout->createAttribute(mpp::mesh::Vertex::Component::Colour4, mpp::mesh::Vertex::DataType::Float, false);
    return spec;
}
mpp::ShaderVariable input(std::string name, uint32_t location, uint32_t components,
    mpp::ShaderScalar scalar = mpp::ShaderScalar::Float, uint32_t columns = 0) {
    mpp::ShaderVariable variable;
    variable.name = std::move(name);
    variable.location = location;
    variable.components = components;
    variable.columns = columns;
    variable.scalarWidth = 32;
    variable.scalar = scalar;
    return variable;
}
template <typename F>
void requireFails(std::string const& diagnostic, std::string const& needle, F operation) {
    try { operation(); }
    catch (std::exception const& error) {
        if (std::string(error.what()).find(needle) == std::string::npos)
            throw std::runtime_error("diagnostic '" + diagnostic + "' lacks '" + needle + "': " + error.what());
        return;
    }
    throw std::runtime_error("expected failure did not occur: " + diagnostic);
}
mpp::UniformMember member(std::string name, uint32_t offset, uint32_t components,
    mpp::ShaderScalar scalar = mpp::ShaderScalar::Float, uint32_t columns = 0,
    uint32_t arrayStride = 0, uint32_t matrixStride = 0, std::vector<uint32_t> dims = {}) {
    mpp::UniformMember member;
    member.name = std::move(name);
    member.offset = offset;
    member.size = components * 4;
    member.arrayStride = arrayStride;
    member.matrixStride = matrixStride;
    member.components = components;
    member.columns = columns;
    member.scalarWidth = 32;
    member.scalar = scalar;
    member.arrayDimensions = std::move(dims);
    return member;
}
}
bool pipelineTranslationTests(std::string* failure) {
    using namespace mpp;
    try {
        // The authored Basics triangle layout maps to native vertex input.
        {
            auto spec = triangleSpec();
            auto state = detail::buildVertexInputState(spec,
                { input("_mpp_i_COLOUR", 1, 4), input("_mpp_i_POSITION", 0, 3) }, "Basics.Triangle");
            require(state.topology == VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, "topology mismatch");
            require(state.bindings.size() == 1 && state.bindings[0].binding == 0 &&
                state.bindings[0].stride == 28 && state.bindings[0].inputRate == VK_VERTEX_INPUT_RATE_VERTEX, "binding mismatch");
            require(state.attributes.size() == 2, "attribute count mismatch");
            require(state.attributes[0].location == 0 && state.attributes[0].format == VK_FORMAT_R32G32B32_SFLOAT &&
                state.attributes[0].offset == 0, "position attribute mismatch");
            require(state.attributes[1].location == 1 && state.attributes[1].format == VK_FORMAT_R32G32B32A32_SFLOAT &&
                state.attributes[1].offset == 12, "colour attribute mismatch");
        }
        // Multi-layout (two buffers) maps bindings per layout in order.
        {
            mesh::MeshSpecification spec(mesh::Primitive::Type::Lines);
            auto* first = spec.createVertexBufferAttributeLayout(true);
            first->createAttribute(mesh::Vertex::Component::Position3, mesh::Vertex::DataType::Float, false);
            auto* second = spec.createVertexBufferAttributeLayout(false);
            second->createAttribute(mesh::Vertex::Component::Colour4, mesh::Vertex::DataType::UnsignedByte, true);
            auto state = detail::buildVertexInputState(spec,
                { input("_mpp_i_POSITION", 0, 3), input("_mpp_i_COLOUR", 1, 4) }, "Two.Buffers");
            require(state.topology == VK_PRIMITIVE_TOPOLOGY_LINE_LIST, "line topology mismatch");
            require(state.bindings.size() == 2 && state.bindings[1].binding == 1 && state.bindings[1].stride == 4, "second binding mismatch");
            require(state.attributes[1].binding == 1 && state.attributes[1].format == VK_FORMAT_R8G8B8A8_UNORM,
                "normalized colour format mismatch");
        }
        // Mismatches fail actionably, naming the program and attribute.
        requireFails("input count", "Basics.Triangle", [&] {
            auto spec = triangleSpec();
            detail::buildVertexInputState(spec, { input("_mpp_i_POSITION", 0, 3) }, "Basics.Triangle");
        });
        requireFails("component mismatch", "_mpp_i_COLOUR", [&] {
            mesh::MeshSpecification spec(mesh::Primitive::Type::Triangles);
            auto* layout = spec.createVertexBufferAttributeLayout(true);
            layout->createAttribute(mesh::Vertex::Component::Position3, mesh::Vertex::DataType::Float, false);
            layout->createAttribute(mesh::Vertex::Component::Colour3, mesh::Vertex::DataType::Float, false);
            detail::buildVertexInputState(spec, { input("_mpp_i_POSITION", 0, 3), input("_mpp_i_COLOUR", 1, 4) }, "Mismatch");
        });
        requireFails("integer/float mismatch", "integer", [&] {
            mesh::MeshSpecification spec(mesh::Primitive::Type::Triangles);
            auto* layout = spec.createVertexBufferAttributeLayout(true);
            layout->createAttribute(mesh::Vertex::Component::Position3, mesh::Vertex::DataType::Float, false);
            layout->createAttribute(mesh::Vertex::Component::Colour4, mesh::Vertex::DataType::Int, false);
            detail::buildVertexInputState(spec, { input("_mpp_i_POSITION", 0, 3), input("_mpp_i_COLOUR", 1, 4) }, "Mismatch");
        });
        // Integer shader inputs accept integer (non-normalized) attributes.
        {
            mesh::MeshSpecification spec(mesh::Primitive::Type::Points);
            auto* layout = spec.createVertexBufferAttributeLayout(true);
            layout->createAttribute(mesh::Vertex::Component::UserDefined1, mesh::Vertex::DataType::Float, false);
            layout->createAttribute(mesh::Vertex::Component::Colour4, mesh::Vertex::DataType::Int, false);
            auto state = detail::buildVertexInputState(spec,
                { input("_mpp_i_POSITION", 0, 1), input("_mpp_i_COLOUR", 1, 4, ShaderScalar::SignedInt) }, "Integer");
            require(state.attributes[1].format == VK_FORMAT_R32G32B32A32_SINT, "integer format mismatch");
        }
        requireFails("matrix input", "not delivered", [&] {
            auto spec = triangleSpec();
            detail::buildVertexInputState(spec, { input("_mpp_i_POSITION", 0, 3), input("_mpp_i_COLOUR", 1, 4, ShaderScalar::Float, 4) }, "Matrix");
        });
        requireFails("double attribute", "double", [&] {
            mesh::MeshSpecification spec(mesh::Primitive::Type::Triangles);
            auto* layout = spec.createVertexBufferAttributeLayout(true);
            layout->createAttribute(mesh::Vertex::Component::Position3, mesh::Vertex::DataType::Float, false);
            layout->createAttribute(mesh::Vertex::Component::Colour4, mesh::Vertex::DataType::Double, false);
            detail::buildVertexInputState(spec, { input("_mpp_i_POSITION", 0, 3), input("_mpp_i_COLOUR", 1, 4) }, "Double");
        });

        // std140 uniform-block writes against reflected member layouts.
        std::vector<UniformMember> members{
            member("_mpp_u_TINT_", 0, 4),
            member("_mpp_u_scale_", 16, 1),
            member("_mpp_u_model_", 32, 4, ShaderScalar::Float, 4, 0, 16),
            member("_mpp_u_weights_[0]", 96, 4, ShaderScalar::Float, 0, 16, 0, { 2 }),
        };
        std::vector<uint8_t> image(128, 0);
        float tint[4] = { 0.25f, 0.5f, 0.75f, 1.0f };
        require(detail::writeUniformValue(members, image, "_mpp_u_TINT_", program::GLSLType::Float, 1, 4, tint, "Tinted"),
            "declared uniform not written");
        require(!std::memcmp(image.data(), tint, sizeof(tint)), "vec4 written at the wrong offset");
        float scale = 2.5f;
        require(detail::writeUniformValue(members, image, "_mpp_u_scale_", program::GLSLType::Float, 1, 1, &scale, "Tinted"), "scalar not written");
        require(!std::memcmp(image.data() + 16, &scale, 4), "float written at the wrong offset");
        float matrix[16];
        for (int i = 0; i < 16; ++i) matrix[i] = float(i);
        require(detail::writeUniformValue(members, image, "_mpp_u_model_", program::GLSLType::FloatMatrix, 1, 4, matrix, "Tinted"), "mat4 not written");
        require(!std::memcmp(image.data() + 32 + 3 * 16, matrix + 12, 16), "mat4 column written at the wrong offset");
        float weights[8];
        for (int i = 0; i < 8; ++i) weights[i] = float(i) + 0.5f;
        require(detail::writeUniformValue(members, image, "_mpp_u_weights_", program::GLSLType::Float, 2, 4, weights, "Tinted"), "array not written");
        require(!std::memcmp(image.data() + 96, weights, 16) && !std::memcmp(image.data() + 112, weights + 4, 16),
            "array elements not strided by the reflected stride");
        // Sparse overrides: undeclared uniforms are no-ops and change nothing.
        auto snapshot = image;
        require(!detail::writeUniformValue(members, image, "_mpp_u_MISSING_", program::GLSLType::Float, 1, 4, tint, "Tinted"),
            "undeclared uniform was not a no-op");
        require(image == snapshot, "undeclared uniform modified the block image");
        // Type/shape errors fail actionably, naming the program and member.
        int32_t wrong = 1;
        requireFails("int into vec4", "_mpp_u_TINT_", [&] {
            detail::writeUniformValue(members, image, "_mpp_u_TINT_", program::GLSLType::Int, 1, 1, &wrong, "Tinted"); });
        requireFails("float into int member", "type mismatch", [&] {
            std::vector<UniformMember> ints{ member("_mpp_u_index_", 0, 1, ShaderScalar::SignedInt) };
            std::vector<uint8_t> intImage(16, 0);
            float value = 1.0f;
            detail::writeUniformValue(ints, intImage, "_mpp_u_index_", program::GLSLType::Float, 1, 1, &value, "Tinted"); });
        requireFails("non-mat4 matrix", "mat4", [&] {
            float mat3[9]{};
            detail::writeUniformValue(members, image, "_mpp_u_model_", program::GLSLType::FloatMatrix, 1, 3, mat3, "Tinted"); });
        requireFails("array overflow", "exceeds", [&] {
            float tooMany[12]{};
            detail::writeUniformValue(members, image, "_mpp_u_weights_", program::GLSLType::Float, 3, 4, tooMany, "Tinted"); });
        requireFails("array count into scalar", "non-array", [&] {
            float two[8]{};
            detail::writeUniformValue(members, image, "_mpp_u_TINT_", program::GLSLType::Float, 2, 4, two, "Tinted"); });
        requireFails("boolean member", "boolean", [&] {
            std::vector<UniformMember> bools{ member("_mpp_u_flag_", 0, 1, ShaderScalar::Boolean) };
            std::vector<uint8_t> boolImage(16, 0);
            detail::writeUniformValue(bools, boolImage, "_mpp_u_flag_", program::GLSLType::Int, 1, 1, &wrong, "Tinted"); });
        return true;
    } catch (std::exception const& error) { *failure = error.what(); return false; }
}
