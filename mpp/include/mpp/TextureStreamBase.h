#pragma once

#include <string>
#include <functional>

#include "mpp/TextureParams.h"

namespace mpp
{

	class _MPPAPI TextureStreamBase
	{
	protected:

		uint32_t mInternalFormat;

		uint32_t mTarget;

	public:

		TextureStreamBase();

		virtual ~TextureStreamBase() = default;

		uint32_t getInternalFormat() const;

		uint32_t getTarget() const;

		virtual size_t getWidth() const = 0;

		virtual size_t getHeight() const = 0;

		virtual size_t getDepth() const = 0;

		virtual size_t getBitsPerPixel() const = 0;

		virtual uint32_t getPixelFormat() const = 0;

		virtual uint32_t getPixelDataType() const = 0;

		virtual TextureParams const& getParams() const = 0;

		virtual std::string const& getSampler() const = 0;
	};
}