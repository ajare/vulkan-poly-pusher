#if defined(_WIN32)
#	include <Windows.h>
#endif




#include <cstddef>
#include <cstdint>
#include "mpp/TextureData.h"

using namespace std;

namespace mpp
{

	TextureData::TextureData()
		: data(nullptr)
		, width(0)
		, height(0)
		, depth(0)
		, bitsPerPixel(0)
		, pixelFormat(0)
		, dataType(0)
	{
	}

	TextureData::TextureData(
		uint8_t* _data,
		size_t _width,
		size_t _bitsPerPixel,
		uint32_t _pixelFormat,
		uint32_t _dataType)
		: data(_data)
		, width(_width)
		, height(0)
		, depth(0)
		, bitsPerPixel(_bitsPerPixel)
		, pixelFormat(_pixelFormat)
		, dataType(_dataType)
	{
	}

	TextureData::TextureData(
		uint8_t* _data,
		size_t _width,
		size_t _height,
		size_t _bitsPerPixel,
		uint32_t _pixelFormat,
		uint32_t _dataType)
		: data(_data)
		, width(_width)
		, height(_height)
		, depth(0)
		, bitsPerPixel(_bitsPerPixel)
		, pixelFormat(_pixelFormat)
		, dataType(_dataType)
	{
	}

	TextureData::TextureData(
		uint8_t* _data,
		size_t _width,
		size_t _height,
		size_t _depth,
		size_t _bitsPerPixel,
		uint32_t _pixelFormat,
		uint32_t _dataType)
		: data(_data)
		, width(_width)
		, height(_height)
		, depth(_depth)
		, bitsPerPixel(_bitsPerPixel)
		, pixelFormat(_pixelFormat)
		, dataType(_dataType)
	{
	}

}