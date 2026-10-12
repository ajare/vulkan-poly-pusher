#include <GL/glew.h>

#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <stdexcept>

#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#include <stb/stb_image.h>

#include "mpp/app/ImageLoader.h"

namespace mpp::app
{
	TextureData loadImageFile(std::string const& filename)
	{
		stbi_set_flip_vertically_on_load_thread(0);

		int width = 0;
		int height = 0;
		int channels = 0;
		size_t componentSize = 0;
		uint32_t dataType = 0;
		void* imageData = nullptr;

		if (stbi_is_hdr(filename.c_str()))
		{
			componentSize = sizeof(float);
			dataType = GL_FLOAT;
			imageData = stbi_loadf(filename.c_str(), &width, &height, &channels, 0);
		}
		else if (stbi_is_16_bit(filename.c_str()))
		{
			componentSize = sizeof(stbi_us);
			dataType = GL_UNSIGNED_SHORT;
			imageData = stbi_load_16(filename.c_str(), &width, &height, &channels, 0);
		}
		else
		{
			componentSize = sizeof(stbi_uc);
			dataType = GL_UNSIGNED_BYTE;
			imageData = stbi_load(filename.c_str(), &width, &height, &channels, 0);
		}

		std::unique_ptr<void, void(*)(void*)> loadedImage(imageData, stbi_image_free);
		if (!loadedImage)
		{
			auto const reason = stbi_failure_reason();
			throw std::runtime_error("Could not load image '" + filename + "': " +
				(reason ? reason : "unknown STB image error"));
		}

		if (width <= 0 || height <= 0 || channels < 1 || channels > 4)
		{
			throw std::runtime_error("Unsupported image dimensions or channel count in '" + filename + "'.");
		}

		auto const dataWidth = static_cast<size_t>(width);
		auto const dataHeight = static_cast<size_t>(height);
		auto const channelCount = static_cast<size_t>(channels);
		if (dataWidth > std::numeric_limits<size_t>::max() / dataHeight ||
			dataWidth * dataHeight > std::numeric_limits<size_t>::max() / channelCount ||
			dataWidth * dataHeight * channelCount > std::numeric_limits<size_t>::max() / componentSize)
		{
			throw std::overflow_error("Image data is too large: '" + filename + "'.");
		}

		auto const dataSize = dataWidth * dataHeight * channelCount * componentSize;
		auto pixels = std::make_unique<uint8_t[]>(dataSize);
		std::memcpy(pixels.get(), loadedImage.get(), dataSize);

		uint32_t pixelFormat = 0;
		switch (channels)
		{
		case 1: pixelFormat = GL_RED; break;
		case 2: pixelFormat = GL_RG; break;
		case 3: pixelFormat = GL_RGB; break;
		case 4: pixelFormat = GL_RGBA; break;
		}

		return {
			pixels.release(),
			dataWidth,
			dataHeight,
			channelCount * componentSize * 8,
			pixelFormat,
			dataType
		};
	}
}
