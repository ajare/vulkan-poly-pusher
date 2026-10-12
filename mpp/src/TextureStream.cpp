#include <cassert>
#include <filesystem>

#include "utils/FileSystem.h"

#include "mpp/TextureStream.h"

using namespace std;

namespace mpp
{

	/*
	 * Constructor.
	 *
	 */
	TextureStream::TextureStream(ResourceManager* resourceMgr, string streamType)
		: ResourceStream(resourceMgr, streamType)
		, mIsAtlas(false)
	{
	}

	/*
	* Destructor.
	*
	*/
	TextureStream::~TextureStream()
	{
		delete[] mData.data;
	}

	/*
	 * Load data.
	 *
	 */
	void TextureStream::loadImpl()
	{
		if (mDefinition.loadFunc)
		{
			mData = mDefinition.loadFunc(mDefinition.source);
		}
	}

	void TextureStream::unloadImpl()
	{
		if (mData.data)
		{
			delete[] mData.data;
			mData.data = nullptr;

			mData.width = 0;
			mData.height = 0;
			mData.depth = 0;
			mData.bitsPerPixel = 0;
			mData.pixelFormat = 0;
			mData.dataType = 0;
		}
	}

	bool TextureStream::isAtlas() const
	{
		return mIsAtlas;
	}

	uint32_t TextureStream::getInternalFormat() const
	{
		return mDefinition.internalFormat;
	}

	uint32_t TextureStream::getTarget() const
	{
		return mDefinition.target;
	}

	/*
	 * Get raw texture data.
	 *
	 */
	uint8_t const* TextureStream::getData() const
	{
		return mData.data;
	}

	/*
	 * Get texture width.
	 *
	 */
	size_t TextureStream::getWidth() const
	{
		return mData.width;
	}

	/*
	 * Get texture height.
	 *
	 */
	size_t TextureStream::getHeight() const
	{
		return mData.height;
	}

	size_t TextureStream::getDepth() const
	{
		return mData.depth;
	}

	/*
	 * Get texture bits per pixel.
	 *
	 */
	size_t TextureStream::getBitsPerPixel() const
	{
		return mData.bitsPerPixel;
	}

	uint32_t TextureStream::getPixelFormat() const
	{
		return mData.pixelFormat;
	}

	uint32_t TextureStream::getPixelDataType() const
	{
		return mData.dataType;
	}

	/*
	 * Get texture data size in bytes.
	 *
	 */
	size_t TextureStream::getDataSize() const
	{
		return getWidth() * getHeight() * getBitsPerPixel() / 8;
	}

	TextureParams const& TextureStream::getParams() const
	{
		return mDefinition.params;
	}

	string const& TextureStream::getSampler() const
	{
		return mDefinition.sampler;
	}

	void TextureStream::setFileBasePaths(string const& basepath)
	{
		filesystem::path sourceFp(mDefinition.source);
		if (sourceFp.is_relative()) mDefinition.source = utils::FileSystem::concatPaths(basepath, mDefinition.source);
	}
}