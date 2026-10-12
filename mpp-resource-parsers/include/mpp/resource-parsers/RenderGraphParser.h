#pragma once

#include <string>

#include "mpp/RenderGraph.h"

#include "Config.h"

namespace mpp::data { class StructuredData; }

namespace mpp
{
	namespace resource_parsers
	{
		// Parses the nested-element RenderGraph XML template format into the
		// context-free topology/validation layer. Allocation/execution remains
		// the responsibility of a compiled graph runtime.
		class _MPPRESOURCEPARSERSAPI RenderGraphParser
		{
		public:
			static RenderGraph fromFile(std::string const& filepath);
			static RenderGraph fromData(mpp::data::StructuredData const& data, std::string const& sourceName);
		};
	}
}
