#pragma once
// Private Vulkan-boundary translation of MPP's retained vertex data-type
// vocabulary. Public contracts keep the legacy numeric meanings (see
// compat/LegacyGpuValues.h); only this boundary sees native VkFormat values.
// Pure CPU logic: CPU regressions exercise it without a device.
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vulkan/vulkan_core.h>
#include "mpp/mesh/Vertex.h"
#include "LegacyGpuValues.h"

namespace mpp::detail {

// Retained numeric constant for an authored vertex data type. These values are
// private vocabulary; authored/public contracts use mesh::Vertex::DataType.
inline uint32_t legacyVertexDataTypeConstant(mesh::Vertex::DataType dataType) {
    switch (dataType) {
    case mesh::Vertex::DataType::Byte:                       return GL_BYTE;
    case mesh::Vertex::DataType::UnsignedByte:               return GL_UNSIGNED_BYTE;
    case mesh::Vertex::DataType::Short:                      return GL_SHORT;
    case mesh::Vertex::DataType::UnsignedShort:              return GL_UNSIGNED_SHORT;
    case mesh::Vertex::DataType::Int:                        return GL_INT;
    case mesh::Vertex::DataType::UnsignedInt:                return GL_UNSIGNED_INT;
    case mesh::Vertex::DataType::HalfFloat:                  return GL_HALF_FLOAT;
    case mesh::Vertex::DataType::Float:                      return GL_FLOAT;
    case mesh::Vertex::DataType::Double:                     return GL_DOUBLE;
    case mesh::Vertex::DataType::Int_2_10_10_10_REV:         return GL_INT_2_10_10_10_REV;
    case mesh::Vertex::DataType::UnsignedInt_2_10_10_10_REV: return GL_UNSIGNED_INT_2_10_10_10_REV;
    default: break;
    }
    throw std::runtime_error("Unsupported vertex data-type vocabulary value " +
        std::to_string(unsigned(dataType)) + "; supported types are byte/short/int, float, half and packed 2.10.10.10");
}

// Translate a retained numeric data type, component count and normalization
// into the native vertex format. Semantics mirror the frozen behaviour:
// integer types become *integer* attributes unless normalized, float/half
// ignore the normalization flag, and packed 2.10.10.10 requires 4 components.
inline VkFormat translateVertexFormat(uint32_t legacyDataType, size_t componentCount, bool normalise) {
    auto components = [&] {
        if (componentCount < 1 || componentCount > 4)
            throw std::runtime_error("Vertex attribute component count " + std::to_string(componentCount) +
                " is outside the supported 1-4 range");
        return componentCount;
    };
    auto pick = [&](VkFormat a, VkFormat b, VkFormat c, VkFormat d) {
        switch (components()) { case 1: return a; case 2: return b; case 3: return c; default: return d; }
    };
    switch (legacyDataType) {
    case GL_FLOAT:      return pick(VK_FORMAT_R32_SFLOAT, VK_FORMAT_R32G32_SFLOAT, VK_FORMAT_R32G32B32_SFLOAT, VK_FORMAT_R32G32B32A32_SFLOAT);
    case GL_HALF_FLOAT: return pick(VK_FORMAT_R16_SFLOAT, VK_FORMAT_R16G16_SFLOAT, VK_FORMAT_R16G16B16_SFLOAT, VK_FORMAT_R16G16B16A16_SFLOAT);
    case GL_UNSIGNED_BYTE:
        return normalise ? pick(VK_FORMAT_R8_UNORM, VK_FORMAT_R8G8_UNORM, VK_FORMAT_R8G8B8_UNORM, VK_FORMAT_R8G8B8A8_UNORM)
                         : pick(VK_FORMAT_R8_UINT, VK_FORMAT_R8G8_UINT, VK_FORMAT_R8G8B8_UINT, VK_FORMAT_R8G8B8A8_UINT);
    case GL_BYTE:
        return normalise ? pick(VK_FORMAT_R8_SNORM, VK_FORMAT_R8G8_SNORM, VK_FORMAT_R8G8B8_SNORM, VK_FORMAT_R8G8B8A8_SNORM)
                         : pick(VK_FORMAT_R8_SINT, VK_FORMAT_R8G8_SINT, VK_FORMAT_R8G8B8_SINT, VK_FORMAT_R8G8B8A8_SINT);
    case GL_UNSIGNED_SHORT:
        return normalise ? pick(VK_FORMAT_R16_UNORM, VK_FORMAT_R16G16_UNORM, VK_FORMAT_R16G16B16_UNORM, VK_FORMAT_R16G16B16A16_UNORM)
                         : pick(VK_FORMAT_R16_UINT, VK_FORMAT_R16G16_UINT, VK_FORMAT_R16G16B16_UINT, VK_FORMAT_R16G16B16A16_UINT);
    case GL_SHORT:
        return normalise ? pick(VK_FORMAT_R16_SNORM, VK_FORMAT_R16G16_SNORM, VK_FORMAT_R16G16B16_SNORM, VK_FORMAT_R16G16B16A16_SNORM)
                         : pick(VK_FORMAT_R16_SINT, VK_FORMAT_R16G16_SINT, VK_FORMAT_R16G16B16_SINT, VK_FORMAT_R16G16B16A16_SINT);
    case GL_UNSIGNED_INT:
        if (normalise) throw std::runtime_error("Normalized 32-bit unsigned-int vertex attributes have no Vulkan vertex format; use a float or 16-bit attribute");
        return pick(VK_FORMAT_R32_UINT, VK_FORMAT_R32G32_UINT, VK_FORMAT_R32G32B32_UINT, VK_FORMAT_R32G32B32A32_UINT);
    case GL_INT:
        if (normalise) throw std::runtime_error("Normalized 32-bit int vertex attributes have no Vulkan vertex format; use a float or 16-bit attribute");
        return pick(VK_FORMAT_R32_SINT, VK_FORMAT_R32G32_SINT, VK_FORMAT_R32G32B32_SINT, VK_FORMAT_R32G32B32A32_SINT);
    case GL_DOUBLE:
        throw std::runtime_error("Double-precision vertex attributes require shaderFloat64 and are not delivered in the Vulkan geometry-upload subset; use float attributes");
    case GL_UNSIGNED_INT_2_10_10_10_REV:
        if (components() != 4) throw std::runtime_error("Packed 2.10.10.10 vertex attributes require exactly 4 components");
        return normalise ? VK_FORMAT_A2B10G10R10_UNORM_PACK32 : VK_FORMAT_A2B10G10R10_UINT_PACK32;
    case GL_INT_2_10_10_10_REV:
        if (components() != 4) throw std::runtime_error("Packed 2.10.10.10 vertex attributes require exactly 4 components");
        return normalise ? VK_FORMAT_A2B10G10R10_SNORM_PACK32 : VK_FORMAT_A2B10G10R10_SINT_PACK32;
    default: break;
    }
    throw std::runtime_error("Unsupported vertex data-type vocabulary value 0x" +
        std::to_string(legacyDataType) + " at the Vulkan boundary");
}

}
