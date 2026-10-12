#pragma once

#include <string>

#include "Config.h"
#include "mpp/PbrPipelineDocument.h"

namespace mpp::resource_parsers
{
	class _MPPRESOURCEPARSERSAPI PbrPipelineParser
	{
	public:
		static PbrPipelineDocument fromFile(std::string const& filepath);
	};
}
