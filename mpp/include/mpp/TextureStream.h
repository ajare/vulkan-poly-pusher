#pragma once

#include <functional>
#include <string>

#include "mpp/ResourceStream.h"
#include "mpp/TextureParams.h"
#include "mpp/TextureData.h"

namespace mpp
{
	class TextureStream;
	typedef std::function<TextureData(std::string const&)> ImageLoadFunction;

	class _MPPAPI TextureStream : public ResourceStream
	{
		friend class ResourceStreamSerializer;

	protected:
		struct Definition
		{
			uint32_t internalFormat{ 0 };
			uint32_t target{ 0 };
			TextureParams params;
			std::string sampler;
			std::string source;
			ImageLoadFunction loadFunc;
		};
		Definition mDefinition;
		bool mIsAtlas;
		TextureData mData;
		void loadImpl();
		void unloadImpl();

	public:
		TextureStream(ResourceManager* resourceMgr, std::string streamType = "Texture");
		virtual ~TextureStream();
		bool isAtlas() const;
		uint32_t getInternalFormat() const;
		uint32_t getTarget() const;
		uint8_t const* getData() const;
		size_t getWidth() const;
		size_t getHeight() const;
		size_t getDepth() const;
		size_t getBitsPerPixel() const;
		uint32_t getPixelFormat() const;
		uint32_t getPixelDataType() const;
		size_t getDataSize() const;
		TextureParams const& getParams() const;
		std::string const& getSampler() const;
		void setFileBasePaths(std::string const& basepath);
	};
}
