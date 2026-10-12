#include "mpp/BufferRenderer.h"

namespace mpp
{
	BufferRenderer::BufferRenderer(BufferDataProviderPtr dataProvider)
		: mDataProvider(dataProvider)
	{
	}

	void BufferRenderer::render(RenderSystem* renderSystem)
	{
		auto numCmds = mDataProvider->getNumCommands();
		for (uint32_t i = 0; i < numCmds; ++i)
		{
			renderSystem->renderBufferImmediate(
				mDataProvider->getVertexData(i),
				mDataProvider->getVertexStride(i),
				mDataProvider->getVertexCount(i),
				mDataProvider->getIndexData(i),
				mDataProvider->getIndexWidth(i),
				mDataProvider->getIndexCount(i),
				mDataProvider->getRenderCommands(i)
			);
		}
	}
}