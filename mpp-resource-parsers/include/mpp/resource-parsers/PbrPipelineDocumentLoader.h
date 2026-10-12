#pragma once

#include <string>

#include "Config.h"
#include "mpp/PbrPipelineDocument.h"

namespace mpp::resource_parsers
{
	// Dispatches by XML root. Standalone RenderGraph input is migrated into an
	// unsaved native PbrPipeline document with explicit stable value IDs.
	class _MPPRESOURCEPARSERSAPI PbrPipelineDocumentLoader
	{
	public:
		static PbrPipelineDocument fromFile(std::string const& filepath);
	};
}
