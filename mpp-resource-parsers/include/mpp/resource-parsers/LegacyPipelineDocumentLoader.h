#pragma once

#include <string>

#include "Config.h"
#include "mpp/LegacyPipelineDocument.h"

namespace mpp::resource_parsers
{
	// Dispatches by XML root ("LegacyPipeline").
	class _MPPRESOURCEPARSERSAPI LegacyPipelineDocumentLoader
	{
	public:
		static LegacyPipelineDocument fromFile(std::string const& filepath);
		// True if `filepath`'s XML root is "LegacyPipeline" (vs "PbrPipeline").
		// Lets callers such as PackageScene pick the right document/runtime
		// pair without a PackageManifest schema change.
		static bool isLegacyPipelineFile(std::string const& filepath);
	};
}
