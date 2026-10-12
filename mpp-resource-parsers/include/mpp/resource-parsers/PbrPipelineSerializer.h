#pragma once

#include <string>
#include "Config.h"
#include "mpp/PbrPipelineDocument.h"

namespace mpp::resource_parsers
{
	class _MPPRESOURCEPARSERSAPI PbrPipelineSerializer
	{
	public:
		static void toFile(PbrPipelineDocument const& document, std::string const& filepath);
	};
}
