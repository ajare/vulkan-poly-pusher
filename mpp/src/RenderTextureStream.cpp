#include <cassert>

#include "mpp/RenderTextureStream.h"

using namespace std;

namespace mpp
{

	RenderTextureStream::RenderTextureStream(ResourceManager* resourceMgr)
		: ResourceStream(resourceMgr, "RenderTexture")
		, mDepthAttachment(RenderTextureDepthAttachment::None)
		, mNumAttachments(0)

	{
	}

	void RenderTextureStream::loadImpl()
	{
		// Nothing to do here.
	}

	uint32_t RenderTextureStream::getInternalFormat() const
	{
		return mDefinition.internalFormat;
	}

	uint32_t RenderTextureStream::getTarget() const
	{
		return mDefinition.target;
	}

	uint32_t RenderTextureStream::getMipLevels() const
	{
		return mDefinition.mipLevels;
	}

	size_t RenderTextureStream::getWidth() const
	{
		return mDefinition.width;
	}

	size_t RenderTextureStream::getHeight() const
	{
		return mDefinition.height;
	}

	size_t RenderTextureStream::getDepth() const
	{
		return mDefinition.depth;
	}

	size_t RenderTextureStream::getBitsPerPixel() const
	{
		return mDefinition.bitsPerPixel;
	}

	uint32_t RenderTextureStream::getPixelFormat() const
	{
		return mDefinition.pixelFormat;
	}

	uint32_t RenderTextureStream::getPixelDataType() const
	{
		return mDefinition.pixelDataType;
	}

	TextureParams const& RenderTextureStream::getParams() const
	{
		return mDefinition.params;
	}

	string const& RenderTextureStream::getSampler() const
	{
		return mDefinition.sampler;
	}

	bool RenderTextureStream::useDepthBuffer() const
	{
		return mDepthAttachment != RenderTextureDepthAttachment::None;
	}

	RenderTextureDepthAttachment RenderTextureStream::getDepthAttachment() const
	{
		return mDepthAttachment;
	}

	RenderTextureDepthParams const& RenderTextureStream::getDepthParams() const
	{
		return mDepthParams;
	}

	RenderTextureDepthFormat RenderTextureStream::getDepthFormat() const
	{
		return mDepthFormat;
	}

	size_t RenderTextureStream::getNumAttachments() const
	{
		return mNumAttachments;
	}
}