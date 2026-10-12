#pragma once

#include <memory>
#include <map>

#include "mpp/Config.h"
#include "mpp/RenderGraph.h"
#include "mpp/Resource.h"

namespace mpp
{
	class _MPPAPI RenderGraphTemplate : public Resource
	{
		std::shared_ptr<RenderGraph> mGraph;
		std::map<uint32_t, ResourcePtr> mPrograms;

	protected:
		void createImpl() override;
		void destroyImpl() override;
		void loadImpl() override;
		void unloadImpl() override;

	public:
		RenderGraphTemplate(std::string const& name, RenderSystem* renderSystem, ResourceManager* resourceMgr, ResourceStreamPtr resourceStream);
		std::shared_ptr<RenderGraph> const& getGraph() const;
		ResourcePtr getProgram(GraphPassHandle pass) const;
	};
}
