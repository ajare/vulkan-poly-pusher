#pragma once

#include "Config.h"

namespace mpp
{
	struct _MPPAPI TextureData
	{
		uint8_t* data;
		size_t width, height, depth, bitsPerPixel;
		uint32_t pixelFormat, dataType;

	public:

		TextureData();

		TextureData(
			uint8_t* _data,
			size_t _width,
			size_t _bitsPerPixel,
			uint32_t _pixelFormat,
			uint32_t _dataType);

		TextureData(
			uint8_t* _data,
			size_t _width,
			size_t _height,
			size_t _bitsPerPixel,
			uint32_t _pixelFormat,
			uint32_t _dataType);

		TextureData(
			uint8_t* _data,
			size_t _width,
			size_t _height,
			size_t _depth,
			size_t _bitsPerPixel,
			uint32_t _pixelFormat,
			uint32_t _dataType);
	};

}