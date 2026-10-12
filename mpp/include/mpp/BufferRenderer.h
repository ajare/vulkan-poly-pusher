#pragma once

#include <memory>

#include "mpp/Config.h"
#include "mpp/RenderSystem.h"
#include "mpp/BufferDataProvider.h"

namespace mpp
{
	class _MPPAPI BufferRenderer
	{
		BufferDataProviderPtr mDataProvider;

	public:

		explicit BufferRenderer(BufferDataProviderPtr dataProvider);

		virtual ~BufferRenderer() = default;

		void render(RenderSystem* renderSystem);

	};

	typedef std::shared_ptr<BufferRenderer> BufferRendererPtr;
}