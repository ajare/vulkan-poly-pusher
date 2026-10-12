#pragma once

#include <string>

#include "mpp/RenderGraph.h"
#include "mpp/resource-parsers/Config.h"

namespace mpp::resource_parsers { class StructuredDataWriteNode; }

namespace mpp
{
	namespace resource_parsers
	{
		class _MPPRESOURCEPARSERSAPI RenderGraphSerializer
		{
		public:
			static void toNode(RenderGraph const& graph, StructuredDataWriteNode* root);
			static void toFile(RenderGraph const& graph, std::string const& filepath);
		};
	}
}
