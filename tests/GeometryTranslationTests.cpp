// CPU regressions for the pure Vulkan-boundary vertex translation: retained
// numeric data-type vocabulary and VkFormat mapping, including actionable
// rejection of layouts Vulkan cannot express. No device is required.
#include "vulkan/VertexLayoutTranslation.h"
#include <stdexcept>
#include <string>

namespace {
void require(bool condition, char const* message) { if (!condition) throw std::runtime_error(message); }
using mpp::mesh::Vertex;
using mpp::detail::legacyVertexDataTypeConstant;
using mpp::detail::translateVertexFormat;
void rejects(uint32_t legacy, size_t components, bool normalise, std::string const& diagnostic) {
    try { translateVertexFormat(legacy, components, normalise); }
    catch (std::exception const& error) {
        if (std::string(error.what()).find(diagnostic) == std::string::npos)
            throw std::runtime_error("expected diagnostic '" + diagnostic + "', got: " + error.what());
        return;
    }
    throw std::runtime_error("invalid vertex layout succeeded: " + diagnostic);
}
}
bool geometryTranslationTests(std::string* failure) {
    try {
        // The public enum keeps its retained numeric meanings at the boundary.
        require(legacyVertexDataTypeConstant(Vertex::DataType::Float) == 0x1406, "float numeric vocabulary changed");
        require(legacyVertexDataTypeConstant(Vertex::DataType::UnsignedByte) == 0x1401, "ubyte numeric vocabulary changed");
        require(legacyVertexDataTypeConstant(Vertex::DataType::HalfFloat) == 0x140B, "half numeric vocabulary changed");
        require(legacyVertexDataTypeConstant(Vertex::DataType::Int_2_10_10_10_REV) == 0x8D9F, "packed numeric vocabulary changed");
        require(legacyVertexDataTypeConstant(Vertex::DataType::UnsignedInt_2_10_10_10_REV) == 0x8368, "packed unsigned numeric vocabulary changed");
        try { legacyVertexDataTypeConstant(Vertex::DataType::None); throw std::runtime_error("None data type accepted"); }
        catch (std::runtime_error const& error) { require(std::string(error.what()).find("Unsupported") != std::string::npos, "None diagnostic not actionable"); }

        // Float/half formats; normalization is ignored for floating point.
        require(translateVertexFormat(GL_FLOAT, 3, false) == VK_FORMAT_R32G32B32_SFLOAT, "float3 translation wrong");
        require(translateVertexFormat(GL_FLOAT, 4, true) == VK_FORMAT_R32G32B32A32_SFLOAT, "float4 translation wrong");
        require(translateVertexFormat(GL_HALF_FLOAT, 2, false) == VK_FORMAT_R16G16_SFLOAT, "half2 translation wrong");
        // Integers become integer attributes unless normalized.
        require(translateVertexFormat(GL_UNSIGNED_BYTE, 4, true) == VK_FORMAT_R8G8B8A8_UNORM, "normalized ubyte4 translation wrong");
        require(translateVertexFormat(GL_UNSIGNED_BYTE, 4, false) == VK_FORMAT_R8G8B8A8_UINT, "integer ubyte4 translation wrong");
        require(translateVertexFormat(GL_BYTE, 2, true) == VK_FORMAT_R8G8_SNORM, "normalized byte2 translation wrong");
        require(translateVertexFormat(GL_SHORT, 1, false) == VK_FORMAT_R16_SINT, "short1 translation wrong");
        require(translateVertexFormat(GL_UNSIGNED_SHORT, 3, true) == VK_FORMAT_R16G16B16_UNORM, "normalized ushort3 translation wrong");
        require(translateVertexFormat(GL_INT, 2, false) == VK_FORMAT_R32G32_SINT, "int2 translation wrong");
        require(translateVertexFormat(GL_UNSIGNED_INT, 1, false) == VK_FORMAT_R32_UINT, "uint1 translation wrong");
        // Packed 2.10.10.10 requires four components; REV maps to A2B10G10R10.
        require(translateVertexFormat(GL_UNSIGNED_INT_2_10_10_10_REV, 4, true) == VK_FORMAT_A2B10G10R10_UNORM_PACK32, "packed unorm translation wrong");
        require(translateVertexFormat(GL_INT_2_10_10_10_REV, 4, true) == VK_FORMAT_A2B10G10R10_SNORM_PACK32, "packed snorm translation wrong");
        require(translateVertexFormat(GL_UNSIGNED_INT_2_10_10_10_REV, 4, false) == VK_FORMAT_A2B10G10R10_UINT_PACK32, "packed uint translation wrong");

        // Layouts Vulkan cannot express fail with actionable diagnostics.
        rejects(GL_INT, 3, true, "Normalized 32-bit");
        rejects(GL_UNSIGNED_INT, 1, true, "Normalized 32-bit");
        rejects(GL_DOUBLE, 3, false, "Double-precision");
        rejects(GL_UNSIGNED_INT_2_10_10_10_REV, 3, true, "exactly 4 components");
        rejects(GL_FLOAT, 0, false, "1-4");
        rejects(GL_FLOAT, 5, false, "1-4");
        rejects(0xDEAD, 3, false, "Unsupported vertex data-type vocabulary value");
        return true;
    } catch (std::exception const& error) { *failure = error.what(); return false; }
}
