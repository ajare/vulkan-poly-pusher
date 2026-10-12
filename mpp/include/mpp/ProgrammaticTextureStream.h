#pragma once

#include "mpp/TextureStream.h"

namespace mpp
{
	class _MPPAPI ProgrammaticTextureStream : public TextureStream
	{

	public:

		ProgrammaticTextureStream(ResourceManager* resourceMgr, std::string const& type = "Texture");

		void setAtlas(bool isAtlas);

		void setTarget(TextureTarget target);

		void setParams(TextureParams const& params);

		void setInternalFormat(TextureInternalType type, bool normalized, size_t bitSize, size_t channels);

		void setData(ImageLoadFunction creator);

		void setFile(std::string const& filename, ImageLoadFunction loader);

		void setFiltering(TextureParams::MinFilter minFilter, TextureParams::MagFilter magFilter);

		void setWrapping(TextureParams::Wrapping wrapping);

		void setColourSpace(TextureColourSpace colourSpace);

		void enableMipMaps(bool enable);

		void setLodBaseLevel(int32_t level);

		void setLodMaxLevel(int32_t level);

		void setLodBias(float bias);
		
		void setMaxAnisotropy(float maxAnisotropy);

		void setSampler(std::string const& sampler);

		void setImageLoadFunction(ImageLoadFunction function);

	};
}