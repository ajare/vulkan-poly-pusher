#pragma once

#include "mpp/RenderTextureStream.h"

namespace mpp
{
	class _MPPAPI ProgrammaticRenderTextureStream : public RenderTextureStream
	{
	public:

		explicit ProgrammaticRenderTextureStream(ResourceManager* resourceMgr);

		void setParams(TextureParams const& params);

		void setTarget(TextureTarget target);

		void setMipLevels(uint32_t levels);

		void setInternalFormat(TextureInternalType type, bool normalized, size_t bitSize, size_t channels);

		// Exact OpenGL internal format for packed/sRGB render-graph formats.
		void setInternalFormat(uint32_t internalFormat);

		void setWidth(size_t width);

		void setHeight(size_t height);

		void setDepth(size_t depth);

		void setFiltering(TextureParams::MinFilter minFilter, TextureParams::MagFilter magFilter);

		void setWrapping(TextureParams::Wrapping wrapping);

		void enableMipMaps(bool enable);

		void setLodBaseLevel(int32_t level);

		void setLodMaxLevel(int32_t level);

		void setLodBias(float bias);

		void setMaxAnisotropy(float maxAnisotropy);

		void setSampler(std::string const& sampler);

		void setDepthBuffer(bool use);

		void setDepthAttachment(RenderTextureDepthAttachment attachment);

		void setDepthParams(RenderTextureDepthParams const& params);

		void setDepthFormat(RenderTextureDepthFormat format);

		void setNumAttachments(size_t numAttachments);

	};
}