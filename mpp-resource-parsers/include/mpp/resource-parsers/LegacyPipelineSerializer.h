#pragma once

#include <string>
#include "Config.h"
#include "mpp/LegacyPipelineDocument.h"

namespace mpp::resource_parsers
{
	class _MPPRESOURCEPARSERSAPI LegacyPipelineSerializer
	{
	public:
		static void toFile(LegacyPipelineDocument const& document, std::string const& filepath);
	};
}
