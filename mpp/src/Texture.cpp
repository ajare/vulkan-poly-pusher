#include "mpp/Config.h"

#if defined(_WIN32)
#include <Windows.h>
#endif

#include <algorithm>
#include <cassert>
#include <GL/glew.h>
#include <GL/gl.h>

#include "mpp/RenderSystem.h"
#include "mpp/ResourceManager.h"
#include "mpp/Texture.h"
#include "mpp/TextureStream.h"
#include "mpp/MppException.h"
#include "mpp/GLErrorCheck.h"

using namespace std;

namespace mpp
{
	/*
	 * Constructor.
	 *
	 */
	Texture::Texture(string const& name, RenderSystem* renderSystem, ResourceManager* resourceMgr, ResourceStreamPtr resourceStream)
		: Resource(name, "Texture", renderSystem, resourceMgr, resourceStream)
		, mIsAtlas(false)
		, mNumAttachments(1)
		, mSortId(0)
		, mInternalFormat(0)
		, mWidth(0)
		, mHeight(0)
		, mDepth(0)
		, mBitsPerPixel(0)
		, mPixelFormat(0)
		, mDataType(0)
		, mTarget(0)
	{
	}

	/*
	 * Destructor
	 *
	 */
	Texture::~Texture()
	{
		destroy();
	}

	/*
	 * Should this be treated as a texture atlas?
	 *
	 */
	bool Texture::isAtlas() const
	{
		return mIsAtlas;
	}

	/*
	 * Create the data required for the program from the resource stream.
	 *
	 */
	void Texture::createImpl()
	{
		TextureStream* tStr = dynamic_cast<TextureStream*>(getResourceStream().get());
		if (!tStr)
		{
			THROW_MPP("Could not cast to type 'TextureStream'.", __LINE__, __FILE__, __func__);
		}

		auto dataSize = tStr->getDataSize();

		mIsAtlas = tStr->isAtlas();
		mInternalFormat = tStr->getInternalFormat();
		mTarget = tStr->getTarget();
		mParams = tStr->getParams();

		mWidth = tStr->getWidth();
		mHeight = tStr->getHeight();
		mDepth = tStr->getDepth();
		mBitsPerPixel = tStr->getBitsPerPixel();
		mPixelFormat = tStr->getPixelFormat();
		mDataType = tStr->getPixelDataType();

		auto sampler = tStr->getSampler();
		if (sampler != "")
		{
			mSampler = getResourceManager()->getResource(sampler);
			acquireDependentResource(mSampler);
			mSampler->create();
		}

		if (mInternalFormat == 0)
		{
			// If we haven't specified the format, work it out from bpp/pixelformat/datatype
			size_t channels{ 0 };

			switch (mDataType)
			{
			case GL_BYTE:
				// Signed, normalised
				channels = mBitsPerPixel / (sizeof(int8_t) * 8);
				switch (channels)
				{
				case 1:
					mInternalFormat = GL_R8_SNORM; break;
				case 2:
					mInternalFormat = GL_RG8_SNORM; break;
				case 3:
					mInternalFormat = GL_RGB8_SNORM; break;
				case 4:
					mInternalFormat = GL_RGBA8_SNORM; break;
				}
				break;

			case GL_UNSIGNED_BYTE:
				// Unsigned, normalised
				channels = mBitsPerPixel / (sizeof(uint8_t) * 8);
				switch (channels)
				{
				case 1:
					mInternalFormat = GL_R8; break;
				case 2:
					mInternalFormat = GL_RG8; break;
				case 3:
					mInternalFormat = GL_RGB8; break;
				case 4:
					mInternalFormat = GL_RGBA8; break;
				}
				break;

			case GL_SHORT:
				// Signed, normalised
				channels = mBitsPerPixel / (sizeof(int16_t) * 8);
				switch (channels)
				{
				case 1:
					mInternalFormat = GL_R16_SNORM; break;
				case 2:
					mInternalFormat = GL_RG16_SNORM; break;
				case 3:
					mInternalFormat = GL_RGB16_SNORM; break;
				case 4:
					mInternalFormat = GL_RGBA16_SNORM; break;
				}
				break;

			case GL_UNSIGNED_SHORT:
				// Unsigned, normalised
				channels = mBitsPerPixel / (sizeof(uint16_t) * 8);
				switch (channels)
				{
				case 1:
					mInternalFormat = GL_R16; break;
				case 2:
					mInternalFormat = GL_RG16; break;
				case 3:
					mInternalFormat = GL_RGB16; break;
				case 4:
					mInternalFormat = GL_RGBA16; break;
				}
				break;

			case GL_INT:
				// Signed, unnormalised
				channels = mBitsPerPixel / (sizeof(int32_t) * 8);
				switch (channels)
				{
				case 1:
					mInternalFormat = GL_R32I; break;
				case 2:
					mInternalFormat = GL_RG32I; break;
				case 3:
					mInternalFormat = GL_RGB32I; break;
				case 4:
					mInternalFormat = GL_RGBA32I; break;
				}
				break;

			case GL_UNSIGNED_INT:
				// Unsigned, unnormalised
				channels = mBitsPerPixel / (sizeof(uint32_t) * 8);
				switch (channels)
				{
				case 1:
					mInternalFormat = GL_R32UI; break;
				case 2:
					mInternalFormat = GL_RG32UI; break;
				case 3:
					mInternalFormat = GL_RGB32UI; break;
				case 4:
					mInternalFormat = GL_RGBA32UI; break;
				}
				break;

			case GL_HALF_FLOAT:
				// Signed, unnormalised
				channels = mBitsPerPixel / ((sizeof(float) / 2) * 8);
				switch (channels)
				{
				case 1:
					mInternalFormat = GL_R16F; break;
				case 2:
					mInternalFormat = GL_RG16F; break;
				case 3:
					mInternalFormat = GL_RGB16F; break;
				case 4:
					mInternalFormat = GL_RGBA16F; break;
				}
				break;

			case GL_FLOAT:
				// Signed, unnormalised
				channels = mBitsPerPixel / (sizeof(float) * 8);
				switch (channels)
				{
				case 1:
					mInternalFormat = GL_R32F; break;
				case 2:
					mInternalFormat = GL_RG32F; break;
				case 3:
					mInternalFormat = GL_RGB32F; break;
				case 4:
					mInternalFormat = GL_RGBA32F; break;
				}
				break;

			default:
				THROW_MPP("Unsupported data type.", __LINE__, __FILE__, __func__);
			}
		}

		if (mParams.colourSpace == TextureColourSpace::Srgb)
		{
			if (mInternalFormat == GL_RGB8)
			{
				mInternalFormat = GL_SRGB8;
			}
			else if (mInternalFormat == GL_RGBA8)
			{
				mInternalFormat = GL_SRGB8_ALPHA8;
			}
			else
			{
				THROW_MPP("sRGB textures require an RGB8 or RGBA8 source format.", __LINE__, __FILE__, __func__);
			}
		}
	}

	/*
	 * Destroy the texture data.
	 *
	 */
	void Texture::destroyImpl()
	{
		mWidth = 0;
		mHeight = 0;
		mDepth = 0;
		mBitsPerPixel = 0;
		mPixelFormat = 0;
		mDataType = 0;
		mSortId = 0;
	}

	/*
	 * Create OpenGL texture.
	 *
	 */
	void Texture::loadImpl()
	{
		TextureStream* tStr = dynamic_cast<TextureStream*>(getResourceStream().get());
		if (!tStr)
		{
			THROW_MPP("Could not cast to type 'TextureStream'.", __LINE__, __FILE__, __func__);
		}

		// Create and bind
		for (size_t i = 0; i < mNumAttachments; ++i)
		{
			uint32_t texId;
		
			GL_CHECK(glGenTextures(1, &texId));
			mTextureIds.push_back(texId);

			GL_CHECK(glBindTexture(mTarget, texId));

			// Set name for debugging
			auto label = "Texture: " + getName();
			glObjectLabel(GL_TEXTURE, texId, -1, label.c_str());

			// Set parameters
			switch (mTarget)
			{
			case GL_TEXTURE_3D:
				[[fallthrough]];
			case GL_TEXTURE_CUBE_MAP:
				GL_CHECK(glTexParameteri(mTarget, GL_TEXTURE_WRAP_R, mParams.wrap));
				[[fallthrough]];
			case GL_TEXTURE_2D:
				GL_CHECK(glTexParameteri(mTarget, GL_TEXTURE_WRAP_T, mParams.wrap));
				[[fallthrough]];
			case GL_TEXTURE_1D:
				GL_CHECK(glTexParameteri(mTarget, GL_TEXTURE_MIN_FILTER, mParams.minFilter));
				GL_CHECK(glTexParameteri(mTarget, GL_TEXTURE_MAG_FILTER, mParams.magFilter));
				GL_CHECK(glTexParameteri(mTarget, GL_TEXTURE_WRAP_S, mParams.wrap));
				break;
			default:
				THROW_MPP("Invalid target.", __LINE__, __FILE__, __func__);
			}

			// Set data
			auto data = tStr->getData();
			switch (mTarget)
			{
			case GL_TEXTURE_1D:
				GL_CHECK(glTexImage1D(mTarget, 0, mInternalFormat, (GLsizei)mWidth, 0, mPixelFormat, mDataType, data));
				break;

			case GL_TEXTURE_2D:
				GL_CHECK(glTexImage2D(mTarget, 0, mInternalFormat, (GLsizei)mWidth, (GLsizei)mHeight, 0, mPixelFormat, mDataType, data));
				break;

			case GL_TEXTURE_3D:
				GL_CHECK(glTexImage3D(mTarget, 0, mInternalFormat, (GLsizei)mWidth, (GLsizei)mHeight, (GLsizei)mDepth, 0, mPixelFormat, mDataType, data));
				break;

			case GL_TEXTURE_CUBE_MAP:
				// A stream may provide a single fallback image; callers can replace
				// each face with uploadCubeFace after creation.
				for (uint32_t face = 0; face < 6; ++face)
				{
					GL_CHECK(glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, 0, mInternalFormat, (GLsizei)mWidth, (GLsizei)mHeight, 0, mPixelFormat, mDataType, data));
				}
				break;

			default:
				THROW_MPP("Invalid target.", __LINE__, __FILE__, __func__);
			}

			// Set up mipmaps
			if (mParams.useMipmaps)
			{
				glGenerateMipmap(mTarget);
				GL_CHECK(glTexParameteri(mTarget, GL_TEXTURE_BASE_LEVEL, mParams.lodBaseLevel));
				GL_CHECK(glTexParameteri(mTarget, GL_TEXTURE_MAX_LEVEL, mParams.lodMaxLevel));
				GL_CHECK(glTexParameterf(mTarget, GL_TEXTURE_LOD_BIAS, mParams.lodBias));
			}

			auto const maxAnisotropy = getRenderSystem()->getCaps().maxAnisotropy;
			if (maxAnisotropy > 1.0f)
			{
				GL_CHECK(glTexParameterf(mTarget, GL_TEXTURE_MAX_ANISOTROPY, clamp(mParams.maxAnisotropy, 1.0f, maxAnisotropy)));
			}
		}

		GL_CHECK(glBindTexture(mTarget, 0));
		setId(mTextureIds.front());

		if (mSampler)
		{
			mSampler->load();
		}
	}

	/*
	 * Destroy the OpenGL texture.
	 *
	 */
	void Texture::unloadImpl()
	{
		GLuint id = getId();
		if (id != 0)
		{
			GL_CHECK(glDeleteTextures(1, &id));
			setId(0);
		}
	}
	
	/*
	 * Get texture width.
	 *
	 */
	size_t Texture::getWidth() const
	{
		return mWidth;
	}

	/*
	 * Get texture height.
	 *
	 */
	size_t Texture::getHeight() const
	{
		return mHeight;
	}

	/*
	 * Get texture depth.
	 *
	 */
	size_t Texture::getDepth() const
	{
		return mDepth;
	}

	/*
	 * Get texture bits per pixel.
	 *
	 */
	size_t Texture::getBitsPerPixel() const
	{
		return mBitsPerPixel;
	}

	size_t Texture::getNumAttachments() const
	{
		return mNumAttachments;
	}

	uint32_t Texture::getTextureTarget() const { return mTarget; }
	uint32_t Texture::getInternalFormat() const { return mInternalFormat; }

	uint32_t Texture::getMipLevels() const
	{
		// Without mipmaps only level zero exists, whatever the LOD clamps say.
		if (!mParams.useMipmaps) return 1;

		// loadImpl generates a full chain, then clamps sampling to
		// [lodBaseLevel, lodMaxLevel]. textureLod is relative to the base level,
		// so the reachable count is the clamped range, not the allocated one.
		uint32_t allocated = 1;
		for (auto dimension = max(mWidth, mHeight); dimension > 1; dimension >>= 1) ++allocated;
		auto const base = (uint32_t)max(0, mParams.lodBaseLevel);
		auto const highest = mParams.lodMaxLevel < 0 ? base : min(allocated - 1, (uint32_t)mParams.lodMaxLevel);
		return highest > base ? highest - base + 1 : 1;
	}

	/*
	 * Upload texture data.  This ignores the resource stream,
	 * overwriting any existing data, and assumes the size of the data
	 * matches the existing sizes
	 *
	 */
	size_t Texture::uploadData(int attachment, uint8_t const* data, float u0, float v0, float u1, float v1)
	{
		auto xoffset = (uint32_t)(mWidth * u0);
		auto yoffset = (uint32_t)(mHeight * v0);
		auto width = (size_t)(mWidth * (u1 - u0));
		auto height = (size_t)(mHeight * (v1 - v0));

		return uploadData(attachment, data, xoffset, yoffset, width, height);
	}

	size_t Texture::uploadData(int attachment, uint8_t const* data, uint32_t x, uint32_t y, size_t w, size_t h)
	{
		GL_CHECK(glBindTexture(mTarget, mTextureIds[attachment]));
		auto d = mDepth;

		switch (mTarget)
		{
		case GL_TEXTURE_1D:
			h = 1;
			d = 1;
			GL_CHECK(glTexSubImage1D(mTarget, 0, x, (GLsizei)w, mPixelFormat, mDataType, data));
			break;

		case GL_TEXTURE_2D:
			d = 1;
			GL_CHECK(glTexSubImage2D(mTarget, 0, x, y, (GLsizei)w, (GLsizei)h, mPixelFormat, mDataType, data));
			break;

		case GL_TEXTURE_3D:
			GL_CHECK(glTexSubImage3D(mTarget, 0, x, y, 0, (GLsizei)w, (GLsizei)h, (GLsizei)d, mPixelFormat, mDataType, data));
			break;

		case GL_TEXTURE_CUBE_MAP:
			d = 1;
			GL_CHECK(glTexSubImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X, 0, x, y, (GLsizei)w, (GLsizei)h, mPixelFormat, mDataType, data));
			break;

		default:
			THROW_MPP("Invalid target.", __LINE__, __FILE__, __func__);
		}

		GL_CHECK(glBindTexture(mTarget, 0));
		return w * h * d * mBitsPerPixel;
	}

	size_t Texture::uploadData(int attachment, uint8_t const* data)
	{
		return uploadData(attachment, data, 0.0f, 0.0f, 1.0f, 1.0f);
	}

	void Texture::uploadCubeFace(uint32_t face, uint8_t const* data)
	{
		if (mTarget != GL_TEXTURE_CUBE_MAP || face >= 6)
		{
			THROW_MPP("Invalid cube-map face upload.", __LINE__, __FILE__, __func__);
		}
		GL_CHECK(glBindTexture(GL_TEXTURE_CUBE_MAP, mTextureIds.at(0)));
		GL_CHECK(glTexSubImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, 0, 0, 0, (GLsizei)mWidth, (GLsizei)mHeight, mPixelFormat, mDataType, data));
		if (mParams.useMipmaps)
		{
			GL_CHECK(glGenerateMipmap(GL_TEXTURE_CUBE_MAP));
		}
		GL_CHECK(glBindTexture(GL_TEXTURE_CUBE_MAP, 0));
	}

	/*
	 * Bind texture.
	 *
	 */
	void Texture::bind(uint32_t unit, uint32_t attachment)
	{
		if (!isLoaded())
		{
			load();
		}

		GL_CHECK(glActiveTexture(GL_TEXTURE0 + unit));
		GL_CHECK(glBindTexture(mTarget, mTextureIds[attachment]));

		if (mSampler)
		{
			static_cast<Sampler*>(mSampler.get())->bind(unit);
		}
		else
		{
			GL_CHECK(glBindSampler(unit, 0));
		}
	}

	/*
	 * Have a separate id for sorting to the GL id, as the GL id may not fit in however many bytes
	 * we have assigned to texture in the sort key.
	 *
	 */
	void Texture::setSortId(uint32_t sortId)
	{
		mSortId = sortId;
	}

	/*
	 * Get the sort id.
	 *
	 */
	uint32_t Texture::getSortId() const
	{
		return mSortId;
	}

	/*
	 * How many GL names does this resource manage?
	 *
	 */
	int Texture::getIdCount() const
	{
		return (int)mTextureIds.size();
	}

	/*
	 * How many GL names are created?
	 *
	 */
	int Texture::getLiveIdCount() const
	{
		int c = 0;
		for (auto id : mTextureIds)
		{
			GL_CHECK(c += (int)glIsTexture(id));
		}

		return c;
	}

}