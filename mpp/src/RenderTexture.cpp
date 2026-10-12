#include <algorithm>
#include <format>
#include <exception>

#include "utils/StringUtils.h"

#include "mpp/Config.h"
#include "mpp/ResourceManager.h"
#include <GL/glew.h>

#include "mpp/RenderTexture.h"
#include "mpp/RenderTextureStream.h"
#include "mpp/MppException.h"
#include "mpp/GLErrorCheck.h"

using namespace std;

namespace mpp
{
	namespace
	{
		uint32_t getPixelFormat(uint32_t internalFormat)
		{
			switch (internalFormat)
			{
			case GL_R8: case GL_R16: case GL_R16F: case GL_R32F: case GL_R8UI: case GL_R16UI: case GL_R32UI: case GL_R8I: case GL_R16I: case GL_R32I:
				return GL_RED;
			case GL_RG8: case GL_RG16: case GL_RG16F: case GL_RG32F: case GL_RG8UI: case GL_RG16UI: case GL_RG32UI: case GL_RG8I: case GL_RG16I: case GL_RG32I:
				return GL_RG;
			case GL_RGB4: case GL_RGB5: case GL_RGB8: case GL_RGB10: case GL_RGB12: case GL_RGB16: case GL_RGB16F: case GL_RGB32F: case GL_R11F_G11F_B10F:
				return GL_RGB;
			default:
				return GL_RGBA;
			}
		}

		uint32_t getPixelDataType(uint32_t internalFormat)
		{
			switch (internalFormat)
			{
			case GL_R16F: case GL_RG16F: case GL_RGB16F: case GL_RGBA16F:
				return GL_HALF_FLOAT;
			case GL_R32F: case GL_RG32F: case GL_RGB32F: case GL_RGBA32F:
				return GL_FLOAT;
			case GL_R11F_G11F_B10F: return GL_UNSIGNED_INT_10F_11F_11F_REV;
			case GL_RGB10_A2: return GL_UNSIGNED_INT_2_10_10_10_REV;
			default:
				return GL_UNSIGNED_BYTE;
			}
		}

		uint32_t getDepthInternalFormat(RenderTextureDepthFormat format)
		{
			switch (format)
			{
			case RenderTextureDepthFormat::Depth16: return GL_DEPTH_COMPONENT16;
			case RenderTextureDepthFormat::Depth24: return GL_DEPTH_COMPONENT24;
			case RenderTextureDepthFormat::Depth32f: return GL_DEPTH_COMPONENT32F;
			case RenderTextureDepthFormat::Depth24Stencil8: return GL_DEPTH24_STENCIL8;
			case RenderTextureDepthFormat::Depth32fStencil8: return GL_DEPTH32F_STENCIL8;
			}
			return GL_DEPTH_COMPONENT24;
		}

		uint32_t getDepthDataType(RenderTextureDepthFormat format)
		{
			switch (format)
			{
			case RenderTextureDepthFormat::Depth16: return GL_UNSIGNED_SHORT;
			case RenderTextureDepthFormat::Depth24: return GL_UNSIGNED_INT;
			case RenderTextureDepthFormat::Depth32f: return GL_FLOAT;
			case RenderTextureDepthFormat::Depth24Stencil8: return GL_UNSIGNED_INT_24_8;
			case RenderTextureDepthFormat::Depth32fStencil8: return GL_FLOAT_32_UNSIGNED_INT_24_8_REV;
			}
			return GL_UNSIGNED_INT;
		}

		size_t getFormatBitsPerPixel(uint32_t internalFormat)
		{
			switch (internalFormat)
			{
			case GL_R16F: return 16;
			case GL_RG16F: return 32;
			case GL_RGB16F: return 48;
			case GL_RGBA16F: return 64;
			case GL_R32F: return 32;
			case GL_RG32F: return 64;
			case GL_RGB32F: return 96;
			case GL_RGBA32F: return 128;
			case GL_R8: return 8;
			case GL_RG8: return 16;
			case GL_RGB8: return 24;
			case GL_R11F_G11F_B10F: case GL_RGB10_A2: case GL_SRGB8_ALPHA8: return 32;
			default: return 32;
			}
		}
	}

	RenderTexture::RenderTexture(string const& name, RenderSystem* renderSystem, ResourceManager* resourceMgr, ResourceStreamPtr resourceStream)
		: RenderTarget(0, 0)
		, Texture(name, renderSystem, resourceMgr, resourceStream)
		, mRenderSystem(renderSystem)
		, mDepthAttachment(RenderTextureDepthAttachment::None)
		, mFrameBuffer(0)
		, mDepthBuffer(0)
		, mDepthTexture(0)
	{
	}

	RenderTexture::~RenderTexture()
	{
		destroy();
	}

	void RenderTexture::createImpl()
	{
		RenderTextureStream* rtStr = dynamic_cast<RenderTextureStream*>(getResourceStream().get());
		if (!rtStr)
		{
			THROW_MPP("Could not cast to type 'RenderTextureStream'.", __LINE__, __FILE__, __func__);
		}

		mInternalFormat = rtStr->getInternalFormat();
		mTarget = rtStr->getTarget();
		mParams = rtStr->getParams();

		if (mTarget != GL_TEXTURE_2D && mTarget != GL_TEXTURE_CUBE_MAP)
		{
			THROW_MPP("Render textures support only Texture2D and CubeMap colour attachments.", __LINE__, __FILE__, __func__);
		}

		Texture::mWidth = RenderTarget::mWidth = rtStr->getWidth();
		Texture::mHeight = RenderTarget::mHeight = rtStr->getHeight();
		Texture::mDepth = rtStr->getDepth();
		mBitsPerPixel = rtStr->getBitsPerPixel();
		mPixelFormat = rtStr->getPixelFormat();
		mDataType = rtStr->getPixelDataType();

		if (mInternalFormat == 0)
		{
			THROW_MPP("Render texture internal format must be specified.", __LINE__, __FILE__, __func__);
		}
		if (mPixelFormat == 0)
		{
			mPixelFormat = getPixelFormat(mInternalFormat);
		}
		if (mDataType == 0)
		{
			mDataType = getPixelDataType(mInternalFormat);
		}
		if (mBitsPerPixel == 0)
		{
			mBitsPerPixel = getFormatBitsPerPixel(mInternalFormat);
		}

		auto sampler = rtStr->getSampler();
		if (sampler != "")
		{
			mSampler = getResourceManager()->acquireResource(this, sampler);
			mSampler->create();
		}

		mDepthAttachment = rtStr->getDepthAttachment();
		mDepthParams = rtStr->getDepthParams();
		mDepthFormat = rtStr->getDepthFormat();
		mNumAttachments = rtStr->getNumAttachments();
		mSamples = rtStr->mPhysicalSamples;
		mMipLevels = rtStr->getMipLevels();
		if (mSamples == 0 || (mSamples > 1 && (mParams.useMipmaps || mDepthParams.params.useMipmaps)))
		{
			THROW_MPP("Multisample render textures require at least one sample and cannot use mipmaps.", __LINE__, __FILE__, __func__);
		}
		if (mNumAttachments == 0 && mDepthAttachment == RenderTextureDepthAttachment::None)
		{
			THROW_MPP("Render texture needs at least one colour or depth attachment.", __LINE__, __FILE__, __func__);
		}
	}

	void RenderTexture::loadImpl()
	{
		const auto width = getWidth();
		const auto height = getHeight();
		if (width == 0 || height == 0)
		{
			THROW_MPP("Render texture dimensions must be non-zero.", __LINE__, __FILE__, __func__);
		}

		GL_CHECK(glGenFramebuffers(1, &mFrameBuffer));
		GL_CHECK(glBindFramebuffer(GL_FRAMEBUFFER, mFrameBuffer));
		string label = "Framebuffer: " + getName();
		GL_CHECK(glObjectLabel(GL_FRAMEBUFFER, mFrameBuffer, -1, label.c_str()));

		mTextureIds.clear();
		for (size_t i = 0; i < mNumAttachments; ++i)
		{
			GLuint texId{ 0 };
			GL_CHECK(glGenTextures(1, &texId));
			GLenum textureTarget = mSamples > 1 ? GL_TEXTURE_2D_MULTISAMPLE : (GLenum)mTarget;
			GL_CHECK(glBindTexture(textureTarget, texId));

			label = std::format("Texture: {}_attachment_{}", getName(), i);
			GL_CHECK(glObjectLabel(GL_TEXTURE, texId, -1, label.c_str()));
			if (mSamples > 1)
			{
				GL_CHECK(glTexImage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE, (GLsizei)mSamples, mInternalFormat, (GLsizei)width, (GLsizei)height, GL_TRUE));
			}
			else
			{
				if (mTarget == GL_TEXTURE_CUBE_MAP) for (uint32_t face = 0; face < 6; ++face) for (uint32_t mip = 0; mip < mMipLevels; ++mip) { auto dimension = (GLsizei)std::max<size_t>(1, width >> mip); GL_CHECK(glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, mip, mInternalFormat, dimension, dimension, 0, mPixelFormat, mDataType, nullptr)); }
				else GL_CHECK(glTexImage2D(GL_TEXTURE_2D, 0, mInternalFormat, (GLsizei)width, (GLsizei)height, 0, mPixelFormat, mDataType, nullptr));
				GL_CHECK(glTexParameteri(textureTarget, GL_TEXTURE_MAG_FILTER, mParams.magFilter));
				GL_CHECK(glTexParameteri(textureTarget, GL_TEXTURE_MIN_FILTER, mParams.minFilter));
				GL_CHECK(glTexParameteri(textureTarget, GL_TEXTURE_WRAP_S, mParams.wrap));
				GL_CHECK(glTexParameteri(textureTarget, GL_TEXTURE_WRAP_T, mParams.wrap));
				if(mTarget == GL_TEXTURE_CUBE_MAP) GL_CHECK(glTexParameteri(textureTarget, GL_TEXTURE_WRAP_R, mParams.wrap));
				auto const maxAnisotropy = getRenderSystem()->getCaps().maxAnisotropy;
				if (maxAnisotropy > 1.0f)
				{
					GL_CHECK(glTexParameterf(textureTarget, GL_TEXTURE_MAX_ANISOTROPY, clamp(mParams.maxAnisotropy, 1.0f, maxAnisotropy)));
				}
				if(mTarget == GL_TEXTURE_CUBE_MAP){GL_CHECK(glTexParameteri(textureTarget, GL_TEXTURE_BASE_LEVEL, 0));GL_CHECK(glTexParameteri(textureTarget, GL_TEXTURE_MAX_LEVEL, (GLint)mMipLevels - 1));}
			}
			if (mParams.useMipmaps)
			{
				GL_CHECK(glTexParameteri(textureTarget, GL_TEXTURE_BASE_LEVEL, mParams.lodBaseLevel));
				GL_CHECK(glTexParameteri(textureTarget, GL_TEXTURE_MAX_LEVEL, mParams.lodMaxLevel));
				GL_CHECK(glTexParameterf(textureTarget, GL_TEXTURE_LOD_BIAS, mParams.lodBias));
				GL_CHECK(glGenerateMipmap(textureTarget));
			}

			const GLenum attachment = (GLenum)(GL_COLOR_ATTACHMENT0 + i);
			GL_CHECK(glFramebufferTexture2D(GL_FRAMEBUFFER, attachment, mTarget == GL_TEXTURE_CUBE_MAP ? GL_TEXTURE_CUBE_MAP_POSITIVE_X : textureTarget, texId, 0));
			mTextureIds.push_back(texId);
		}

		if (!mTextureIds.empty())
		{
			setId(mTextureIds.front());
		}
		else
		{
			setId(0);
		}

		mDepthBuffer = 0;
		mDepthTexture = 0;
		switch (mDepthAttachment)
		{
		case RenderTextureDepthAttachment::None:
			break;
		case RenderTextureDepthAttachment::DepthRenderbuffer:
		case RenderTextureDepthAttachment::DepthStencilRenderbuffer:
		{
			GL_CHECK(glGenRenderbuffers(1, &mDepthBuffer));
			GL_CHECK(glBindRenderbuffer(GL_RENDERBUFFER, mDepthBuffer));
			label = "Renderbuffer: " + getName();
			GL_CHECK(glObjectLabel(GL_RENDERBUFFER, mDepthBuffer, -1, label.c_str()));
			const bool stencil = mDepthAttachment == RenderTextureDepthAttachment::DepthStencilRenderbuffer;
			const auto depthInternalFormat = getDepthInternalFormat(mDepthFormat);
			if (mSamples > 1) GL_CHECK(glRenderbufferStorageMultisample(GL_RENDERBUFFER, (GLsizei)mSamples, depthInternalFormat, (GLsizei)width, (GLsizei)height));
			else GL_CHECK(glRenderbufferStorage(GL_RENDERBUFFER, depthInternalFormat, (GLsizei)width, (GLsizei)height));
			GL_CHECK(glFramebufferRenderbuffer(GL_FRAMEBUFFER, stencil ? GL_DEPTH_STENCIL_ATTACHMENT : GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, mDepthBuffer));
			break;
		}
		case RenderTextureDepthAttachment::DepthTexture:
		case RenderTextureDepthAttachment::DepthStencilTexture:
		{
			GL_CHECK(glGenTextures(1, &mDepthTexture));
			GLenum depthTarget = mSamples > 1 ? GL_TEXTURE_2D_MULTISAMPLE : (GLenum)mTarget;
			GL_CHECK(glBindTexture(depthTarget, mDepthTexture));
			label = "Depth texture: " + getName();
			GL_CHECK(glObjectLabel(GL_TEXTURE, mDepthTexture, -1, label.c_str()));
			const bool stencil = mDepthAttachment == RenderTextureDepthAttachment::DepthStencilTexture;
			const auto depthInternalFormat = getDepthInternalFormat(mDepthFormat);
			if (mSamples > 1) GL_CHECK(glTexImage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE, (GLsizei)mSamples, depthInternalFormat, (GLsizei)width, (GLsizei)height, GL_TRUE));
			else if (mTarget == GL_TEXTURE_CUBE_MAP) for (uint32_t face = 0; face < 6; ++face) for (uint32_t mip = 0; mip < mMipLevels; ++mip) { auto dimension = (GLsizei)std::max<size_t>(1, width >> mip); GL_CHECK(glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, mip, depthInternalFormat, dimension, dimension, 0, GL_DEPTH_COMPONENT, getDepthDataType(mDepthFormat), nullptr)); }
			else GL_CHECK(glTexImage2D(GL_TEXTURE_2D, 0, depthInternalFormat, (GLsizei)width, (GLsizei)height, 0, stencil ? GL_DEPTH_STENCIL : GL_DEPTH_COMPONENT, getDepthDataType(mDepthFormat), nullptr));
			if (mSamples == 1) GL_CHECK(glTexParameteri(depthTarget, GL_TEXTURE_MIN_FILTER, mDepthParams.params.minFilter));
			if (mSamples == 1) GL_CHECK(glTexParameteri(depthTarget, GL_TEXTURE_MAG_FILTER, mDepthParams.params.magFilter));
			if (mSamples == 1) GL_CHECK(glTexParameteri(depthTarget, GL_TEXTURE_WRAP_S, mDepthParams.params.wrap));
			if (mSamples == 1) GL_CHECK(glTexParameteri(depthTarget, GL_TEXTURE_WRAP_T, mDepthParams.params.wrap));
			if (mSamples == 1 && mTarget == GL_TEXTURE_CUBE_MAP) GL_CHECK(glTexParameteri(depthTarget, GL_TEXTURE_WRAP_R, mDepthParams.params.wrap));
			if (mDepthParams.params.useMipmaps)
			{
				GL_CHECK(glTexParameteri(depthTarget, GL_TEXTURE_BASE_LEVEL, mDepthParams.params.lodBaseLevel));
				GL_CHECK(glTexParameteri(depthTarget, GL_TEXTURE_MAX_LEVEL, mDepthParams.params.lodMaxLevel));
				GL_CHECK(glTexParameterf(depthTarget, GL_TEXTURE_LOD_BIAS, mDepthParams.params.lodBias));
				GL_CHECK(glGenerateMipmap(depthTarget));
			}
			if (mSamples == 1)
			{
				if (mDepthParams.compareRefToTexture)
				{
					const GLfloat litBorder[] = { 1.0f, 1.0f, 1.0f, 1.0f };
					GL_CHECK(glTexParameteri(depthTarget, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE));
					GL_CHECK(glTexParameteri(depthTarget, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL));
					GL_CHECK(glTexParameterfv(depthTarget, GL_TEXTURE_BORDER_COLOR, litBorder));
				}
				else GL_CHECK(glTexParameteri(depthTarget, GL_TEXTURE_COMPARE_MODE, GL_NONE));
			}
			GL_CHECK(glFramebufferTexture2D(GL_FRAMEBUFFER, stencil ? GL_DEPTH_STENCIL_ATTACHMENT : GL_DEPTH_ATTACHMENT, mTarget == GL_TEXTURE_CUBE_MAP ? GL_TEXTURE_CUBE_MAP_POSITIVE_X : depthTarget, mDepthTexture, 0));
			break;
		}
		}

		if (mNumAttachments == 0)
		{
			GL_CHECK(glDrawBuffer(GL_NONE));
			GL_CHECK(glReadBuffer(GL_NONE));
		}
		else
		{
			vector<GLenum> drawBuffers(mNumAttachments);
			for (size_t i = 0; i < mNumAttachments; ++i)
			{
				drawBuffers[i] = (GLenum)(GL_COLOR_ATTACHMENT0 + i);
			}
			GL_CHECK(glDrawBuffers((GLsizei)drawBuffers.size(), drawBuffers.data()));
		}

		if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
		{
			THROW_MPP("Could not create framebuffer.", __LINE__, __FILE__, __func__);
		}

		GL_CHECK(glBindRenderbuffer(GL_RENDERBUFFER, 0));
		GL_CHECK(glBindTexture(GL_TEXTURE_2D, 0));
		GL_CHECK(glBindTexture(GL_TEXTURE_CUBE_MAP, 0));
		GL_CHECK(glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, 0));
		GL_CHECK(glBindFramebuffer(GL_FRAMEBUFFER, 0));
	}

	void RenderTexture::unloadImpl()
	{
		if (mFrameBuffer != 0)
		{
			GL_CHECK(glDeleteFramebuffers(1, &mFrameBuffer));
			mFrameBuffer = 0;
		}
		if (mDepthBuffer != 0)
		{
			GL_CHECK(glDeleteRenderbuffers(1, &mDepthBuffer));
			mDepthBuffer = 0;
		}
		if (mDepthTexture != 0)
		{
			GL_CHECK(glDeleteTextures(1, &mDepthTexture));
			mDepthTexture = 0;
		}
		for (auto textureId : mTextureIds)
		{
			GLuint id = textureId;
			GL_CHECK(glDeleteTextures(1, &id));
		}
		mTextureIds.clear();
		setId(0);
	}

	void RenderTexture::activate()
	{
		GL_CHECK(glBindFramebuffer(GL_FRAMEBUFFER, mFrameBuffer));
	}

	void RenderTexture::deactivate()
	{
		generateMipMaps();
		GL_CHECK(glBindFramebuffer(GL_FRAMEBUFFER, 0));
	}

	void RenderTexture::applyMipView(uint32_t mipLevel)
	{
		for (auto textureId : mTextureIds)
		{
			GL_CHECK(glBindTexture(mTarget, textureId));
			GL_CHECK(glTexParameteri(mTarget, GL_TEXTURE_BASE_LEVEL, (GLint)mipLevel));
			GL_CHECK(glTexParameteri(mTarget, GL_TEXTURE_MAX_LEVEL, (GLint)mipLevel));
		}
		if (mDepthTexture != 0)
		{
			GL_CHECK(glBindTexture(mTarget, mDepthTexture));
			GL_CHECK(glTexParameteri(mTarget, GL_TEXTURE_BASE_LEVEL, (GLint)mipLevel));
			GL_CHECK(glTexParameteri(mTarget, GL_TEXTURE_MAX_LEVEL, (GLint)mipLevel));
		}
		GL_CHECK(glBindTexture(GL_TEXTURE_2D, 0));
	}

	void RenderTexture::restoreMipView()
	{
		for (auto textureId : mTextureIds)
		{
			GL_CHECK(glBindTexture(mTarget, textureId));
			GL_CHECK(glTexParameteri(mTarget, GL_TEXTURE_BASE_LEVEL, mParams.lodBaseLevel));
			GL_CHECK(glTexParameteri(mTarget, GL_TEXTURE_MAX_LEVEL, mParams.lodMaxLevel));
		}
		if (mDepthTexture != 0)
		{
			GL_CHECK(glBindTexture(mTarget, mDepthTexture));
			GL_CHECK(glTexParameteri(mTarget, GL_TEXTURE_BASE_LEVEL, mDepthParams.params.lodBaseLevel));
			GL_CHECK(glTexParameteri(mTarget, GL_TEXTURE_MAX_LEVEL, mDepthParams.params.lodMaxLevel));
		}
		GL_CHECK(glBindTexture(GL_TEXTURE_2D, 0));
	}

	void RenderTexture::generateMipMaps(bool force)
	{
		if (mParams.useMipmaps || force)
		{
			for (auto textureId : mTextureIds)
			{
				GL_CHECK(glBindTexture(mTarget, textureId));
				GL_CHECK(glGenerateMipmap(mTarget));
			}
		}
		if (mDepthParams.params.useMipmaps && mDepthTexture != 0)
		{
			GL_CHECK(glBindTexture(mTarget, mDepthTexture));
			GL_CHECK(glGenerateMipmap(mTarget));
		}
		GL_CHECK(glBindTexture(GL_TEXTURE_2D, 0));
	}

	size_t RenderTexture::getWidth() const
	{
		return RenderTarget::getWidth();
	}

	size_t RenderTexture::getHeight() const
	{
		return RenderTarget::getHeight();
	}

	size_t RenderTexture::getBitsPerPixel() const
	{
		return mBitsPerPixel;
	}

	bool RenderTexture::hasDepthBuffer() const
	{
		return mDepthAttachment != RenderTextureDepthAttachment::None;
	}

	bool RenderTexture::hasStencilBuffer() const
	{
		return mDepthAttachment == RenderTextureDepthAttachment::DepthStencilRenderbuffer ||
			mDepthAttachment == RenderTextureDepthAttachment::DepthStencilTexture;
	}

	bool RenderTexture::isMultisampled() const
	{
		return mSamples > 1;
	}

	uint32_t RenderTexture::getSamples() const
	{
		return mSamples;
	}

	uint32_t RenderTexture::getAttachmentTextureTarget() const
	{
		return mSamples > 1 ? GL_TEXTURE_2D_MULTISAMPLE : mTarget;
	}

	void RenderTexture::resolveTo(RenderTexture* destination, bool colour, bool depth)
	{
		if (!destination || mSamples <= 1 || destination->mSamples != 1 || destination->getWidth() != getWidth() || destination->getHeight() != getHeight())
		{
			THROW_MPP("Invalid multisample render texture resolve.", __LINE__, __FILE__, __func__);
		}
		GLint previousReadFramebuffer = 0, previousDrawFramebuffer = 0;
		GL_CHECK(glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &previousReadFramebuffer));
		GL_CHECK(glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &previousDrawFramebuffer));
		GLbitfield mask = 0;
		if (colour) mask |= GL_COLOR_BUFFER_BIT;
		if (depth) mask |= GL_DEPTH_BUFFER_BIT;
		GL_CHECK(glBindFramebuffer(GL_READ_FRAMEBUFFER, mFrameBuffer));
		GL_CHECK(glBindFramebuffer(GL_DRAW_FRAMEBUFFER, destination->mFrameBuffer));
		if (colour) { GL_CHECK(glReadBuffer(GL_COLOR_ATTACHMENT0)); GL_CHECK(glDrawBuffer(GL_COLOR_ATTACHMENT0)); }
		GL_CHECK(glBlitFramebuffer(0, 0, (GLint)getWidth(), (GLint)getHeight(), 0, 0, (GLint)getWidth(), (GLint)getHeight(), mask, GL_NEAREST));
		GL_CHECK(glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)previousReadFramebuffer));
		GL_CHECK(glBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)previousDrawFramebuffer));
	}

	void RenderTexture::copyDepthTo(RenderTexture* destination)
	{
		if(!destination||!hasDepthBuffer()||!destination->hasDepthBuffer()||destination->mSamples!=1||destination->getWidth()!=getWidth()||destination->getHeight()!=getHeight())THROW_MPP("Invalid render texture depth-history copy.",__LINE__,__FILE__,__func__);GLint previousRead=0,previousDraw=0;GL_CHECK(glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&previousRead));GL_CHECK(glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&previousDraw));GL_CHECK(glBindFramebuffer(GL_READ_FRAMEBUFFER,mFrameBuffer));GL_CHECK(glBindFramebuffer(GL_DRAW_FRAMEBUFFER,destination->mFrameBuffer));GL_CHECK(glBlitFramebuffer(0,0,(GLint)getWidth(),(GLint)getHeight(),0,0,(GLint)getWidth(),(GLint)getHeight(),GL_DEPTH_BUFFER_BIT,GL_NEAREST));GL_CHECK(glBindFramebuffer(GL_READ_FRAMEBUFFER,(GLuint)previousRead));GL_CHECK(glBindFramebuffer(GL_DRAW_FRAMEBUFFER,(GLuint)previousDraw));
	}

	bool RenderTexture::resize(size_t width, size_t height)
	{
		if (width == 0 || height == 0)
		{
			return false;
		}
		if (width == RenderTarget::mWidth && height == RenderTarget::mHeight)
		{
			return true;
		}

		const bool loaded = isLoaded();
		if(!loaded){Texture::mWidth=RenderTarget::mWidth=width;Texture::mHeight=RenderTarget::mHeight=height;return true;}

		// Allocate a complete candidate backing set without disturbing the active
		// one. This keeps framebuffer users valid if allocation/completeness fails.
		auto oldWidth=Texture::mWidth,oldHeight=Texture::mHeight;auto oldFrameBuffer=mFrameBuffer,oldDepthBuffer=mDepthBuffer,oldDepthTexture=mDepthTexture;auto oldTextures=std::move(mTextureIds);auto oldId=getId();
		mFrameBuffer=0;mDepthBuffer=0;mDepthTexture=0;mTextureIds.clear();setId(0);Texture::mWidth=RenderTarget::mWidth=width;Texture::mHeight=RenderTarget::mHeight=height;
		try{loadImpl();}
		catch(...)
		{
			try{unloadImpl();}catch(...){}
			mFrameBuffer=oldFrameBuffer;mDepthBuffer=oldDepthBuffer;mDepthTexture=oldDepthTexture;mTextureIds=std::move(oldTextures);setId(oldId);Texture::mWidth=RenderTarget::mWidth=oldWidth;Texture::mHeight=RenderTarget::mHeight=oldHeight;throw;
		}
		if(oldFrameBuffer)GL_CHECK(glDeleteFramebuffers(1,&oldFrameBuffer));if(oldDepthBuffer)GL_CHECK(glDeleteRenderbuffers(1,&oldDepthBuffer));if(oldDepthTexture)GL_CHECK(glDeleteTextures(1,&oldDepthTexture));for(auto texture:oldTextures)GL_CHECK(glDeleteTextures(1,&texture));
		return true;
	}

	void RenderTexture::attachColourFace(size_t attachment, uint32_t face, uint32_t mipLevel)
	{
		if (mTarget != GL_TEXTURE_CUBE_MAP) THROW_MPP("Colour-face attachment requires a cubemap render texture.", __LINE__, __FILE__, __func__);
		if (attachment >= mTextureIds.size()) THROW_MPP("Cubemap colour attachment index out of range.", __LINE__, __FILE__, __func__);
		if (face >= 6) THROW_MPP("Cubemap face index must be in the range [0, 5].", __LINE__, __FILE__, __func__);
		if (mipLevel >= mMipLevels) THROW_MPP("Cubemap mip level index out of range.", __LINE__, __FILE__, __func__);
		GLint previous = 0;
		GL_CHECK(glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previous));
		GL_CHECK(glBindFramebuffer(GL_FRAMEBUFFER, mFrameBuffer));
		GL_CHECK(glFramebufferTexture2D(GL_FRAMEBUFFER, (GLenum)(GL_COLOR_ATTACHMENT0 + attachment), GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, mTextureIds[attachment], mipLevel));
		if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
		{
			GL_CHECK(glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)previous));
			THROW_MPP("Cubemap face/mip framebuffer attachment is incomplete.", __LINE__, __FILE__, __func__);
		}
		GL_CHECK(glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)previous));
	}

	void RenderTexture::attachDepthFace(uint32_t face, uint32_t mipLevel)
	{
		if (mTarget != GL_TEXTURE_CUBE_MAP || mDepthTexture == 0) THROW_MPP("Depth-face attachment requires a depth cubemap render texture.", __LINE__, __FILE__, __func__);
		if (face >= 6 || mipLevel >= mMipLevels) THROW_MPP("Depth cubemap face or mip index is out of range.", __LINE__, __FILE__, __func__);
		GLint previous = 0; GL_CHECK(glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previous)); GL_CHECK(glBindFramebuffer(GL_FRAMEBUFFER, mFrameBuffer));
		GL_CHECK(glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, mDepthTexture, mipLevel));
		if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) { GL_CHECK(glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)previous)); THROW_MPP("Depth cubemap face framebuffer attachment is incomplete.", __LINE__, __FILE__, __func__); }
		GL_CHECK(glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)previous));
	}

	void RenderTexture::restoreColourFaces()
	{
		if (mTarget != GL_TEXTURE_CUBE_MAP) return;
		GLint previous = 0;
		GL_CHECK(glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previous));
		GL_CHECK(glBindFramebuffer(GL_FRAMEBUFFER, mFrameBuffer));
		for (size_t attachment = 0; attachment < mTextureIds.size(); ++attachment)
			GL_CHECK(glFramebufferTexture2D(GL_FRAMEBUFFER, (GLenum)(GL_COLOR_ATTACHMENT0 + attachment), GL_TEXTURE_CUBE_MAP_POSITIVE_X, mTextureIds[attachment], 0));
		if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
		{
			GL_CHECK(glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)previous));
			THROW_MPP("Could not restore cubemap level-zero framebuffer attachments.", __LINE__, __FILE__, __func__);
		}
		GL_CHECK(glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)previous));
	}

	uint32_t RenderTexture::getMipLevels() const { return mMipLevels; }

	uint32_t RenderTexture::getDepthTextureId() const
	{
		return mDepthTexture;
	}

	void RenderTexture::bindDepth(uint32_t unit)
	{
		if (mDepthTexture == 0)
		{
			THROW_MPP("Render texture has no depth texture attachment.", __LINE__, __FILE__, __func__);
		}
		GL_CHECK(glActiveTexture(GL_TEXTURE0 + unit));
		GL_CHECK(glBindTexture(mTarget, mDepthTexture));
	}

	size_t RenderTexture::getNumColourAttachments() const
	{
		return mTextureIds.size();
	}

	uint32_t RenderTexture::getColourAttachmentId(size_t attachment) const
	{
		if (attachment >= mTextureIds.size())
		{
			THROW_MPP("Render texture colour attachment index out of range.", __LINE__, __FILE__, __func__);
		}
		return mTextureIds[attachment];
	}
}
