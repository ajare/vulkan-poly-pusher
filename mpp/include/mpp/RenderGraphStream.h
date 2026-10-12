#pragma once

#include <memory>

#include "mpp/Config.h"
#include "mpp/RenderGraph.h"
#include "mpp/ResourceStream.h"

namespace mpp
{
	// Immutable topology template stream. File/XML streams populate it before
	// resource loading; runtime pipelines provide imports and callbacks.
	class _MPPAPI RenderGraphStream : public ResourceStream
	{
		std::shared_ptr<RenderGraph> mGraph;

	protected:
		void loadImpl() override {}

	public:
		explicit RenderGraphStream(ResourceManager* resourceMgr);
		void setGraph(std::shared_ptr<RenderGraph> graph);
		std::shared_ptr<RenderGraph> const& getGraph() const;
	};
}
