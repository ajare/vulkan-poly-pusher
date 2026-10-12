#pragma once

#include <cstdint>

namespace mpp
{

	struct Caps
	{
		int glVersionMajor{ 0 };
		int glVersionMinor{ 0 };
		int glslVersionMajor{ 0 };
		int glslVersionMinor{ 0 };

		// A compatibility context still gates point-sprite coordinate generation
		// behind GL_POINT_SPRITE; a core context generates gl_PointCoord always and
		// rejects the enum. Which one this is decides whether that state is set.
		bool compatibilityProfile{ false };

		float pointSizeRange[2]{ 0.0f, 0.0f };
		float aliasedLineWidthRange[2]{ 0.0f, 0.0f };

		int maxTextureSize{ 0 };
		int maxRectTextureSize{ 0 };
		int maxCubeMapTextureSize{ 0 };

		// Framebuffer limits used by graph/MRT validation and execution.
		uint32_t maxColourAttachments{ 1 };
		uint32_t maxDrawBuffers{ 1 };
		uint32_t maxSamples{ 1 };
		// Bit N is set when N-sample MSAA is supported for standard RGBA8
		// render targets. Output validation/allocation uses the 2, 4 and 8 bits.
		uint32_t supportedMsaaSampleMask{ 0 };
		bool supportsMsaa(uint32_t samples) const
		{
			return samples == 1 || (samples < 32 && (supportedMsaaSampleMask & (1u << samples)) != 0);
		}

		float depthRange[2]{ 0.0f, 0.0f };

		int maxRecommendedElements{ 0 };
		int maxRecommendedVertices{ 0 };
		int maxElements{ 0 };

		// One means anisotropic filtering is unavailable or has no effect.
		float maxAnisotropy{ 1.0f };

		bool streamingGeometry{ false };

		uint32_t maxVertexShaderUniforms{ 0 };
		uint32_t maxGeometryShaderUniforms{ 0 };
		uint32_t maxFragmentShaderUniforms{ 0 };

		uint32_t maxVertexTextureUnits{ 0 };
		uint32_t maxGeometryTextureUnits{ 0 };
		uint32_t maxFragmentTextureUnits{ 0 };

		uint32_t maxVertexAttributes{ 0 };
		uint32_t maxVertexAttributeStride{ 0 };

		// Compute and shader-storage limits. The context is requested at 4.4 core,
		// but a request is only a request: SDL may return a lower context and a
		// driver may still refuse a valid kernel. Everything below stays zero
		// while supportsCompute is false, so callers validate against queried
		// numbers rather than assumed ones.
		bool supportsCompute{ false };
		bool supportsMultiDrawIndirect{ false };
		// Generic capability flag retained for clients that use bindless textures;
		// the particle renderer itself uses a core texture-array path.
		bool supportsBindlessTextures{ false };
		uint32_t maxComputeWorkGroupCount[3]{};
		uint32_t maxComputeWorkGroupSize[3]{};
		uint32_t maxComputeWorkGroupInvocations{ 0 };
		uint32_t maxShaderStorageBlockSize{ 0 };
		uint32_t maxShaderStorageBufferBindings{ 0 };
	};

}