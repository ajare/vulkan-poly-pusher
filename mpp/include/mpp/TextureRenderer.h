#pragma once

#include <memory>

#include "mpp/Config.h"
#include "mpp/RenderTexture.h"

namespace mpp
{
	class RenderSystem;
	class ResourceSystem;

	class _MPPAPI TextureRenderer
	{
		std::string mName;

	protected:

		RenderSystem* mRenderSystem{ nullptr };

		ResourceManager* mResourceMgr{ nullptr };

	private:

		virtual void render(size_t width, size_t height) 
		{
			MPP_UNUSED(width);
			MPP_UNUSED(height);
		}

	public:

		TextureRenderer(std::string const& name, RenderSystem* renderSystem, ResourceManager* resourceMgr);

		virtual ~TextureRenderer() = default;

		ResourcePtr createRenderTexture(size_t width, size_t height);

		void updateRenderTexture(ResourcePtr renderTexture);
	};

	typedef std::shared_ptr<TextureRenderer> TextureRendererPtr;
}

