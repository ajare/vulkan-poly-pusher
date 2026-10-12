#include "mpp/TextureRenderer.h"
#include "mpp/ProgrammaticRenderTextureStream.h"
#include "mpp/RenderSystem.h"
#include "mpp/ResourceManager.h"

using namespace std;

namespace mpp
{

	TextureRenderer::TextureRenderer(string const& name, RenderSystem* renderSystem, ResourceManager* resourceMgr)
		: mName(name)
		, mRenderSystem(renderSystem)
		, mResourceMgr(resourceMgr)
	{
	}

	ResourcePtr TextureRenderer::createRenderTexture(size_t width, size_t height)
	{
		auto rtStream = new ProgrammaticRenderTextureStream(mResourceMgr);

		rtStream->setTarget(TextureTarget::Texture2D);
		rtStream->setInternalFormat(TextureInternalType::UnsignedInteger, true, 8, 4);
		
		rtStream->setWidth(width);
		rtStream->setHeight(height);
		rtStream->setDepthBuffer(true);
		rtStream->setNumAttachments(1);

		auto rt = mResourceMgr->declareResource(mName, ResourceStreamPtr(rtStream)).first;

		rt->load();

		updateRenderTexture(rt);
		return rt;
	}

	void TextureRenderer::updateRenderTexture(ResourcePtr renderTexture)
	{
		auto rt = dynamic_pointer_cast<RenderTarget, Resource>(renderTexture);

		auto width = rt->getWidth();
		auto height = rt->getHeight();

		mRenderSystem->pushRenderTarget(rt);
		mRenderSystem->pushModelMatrix();
		mRenderSystem->resetTransform();

		render(width, height);

		mRenderSystem->popModelMatrix();
		mRenderSystem->popRenderTarget();
	}

}
