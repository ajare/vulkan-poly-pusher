#pragma once

#include <string>

#include "Config.h"
#include "mpp/LegacyPipelineDocument.h"

namespace mpp::resource_parsers
{
	class _MPPRESOURCEPARSERSAPI LegacyPipelineParser
	{
	public:
		static LegacyPipelineDocument fromFile(std::string const& filepath);
	};
}
