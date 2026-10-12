#include <memory>

#include "mpp/resource-parsers/MppResourceParsersException.h"
#include "mpp/resource-parsers/LegacyPipelineDocumentLoader.h"
#include "mpp/resource-parsers/LegacyPipelineParser.h"
#include "StructuredDataAdapter.h"

namespace mpp::resource_parsers
{
	LegacyPipelineDocument LegacyPipelineDocumentLoader::fromFile(std::string const& filepath)
	{
		auto root = detail::readDocumentRoot(filepath).getName();
		if (root == "LegacyPipeline") return LegacyPipelineParser::fromFile(filepath);
		THROW_MPP_RESOURCE_PARSERS("Expected LegacyPipeline XML root: " + filepath, __LINE__, __FILE__, __func__);
	}

	bool LegacyPipelineDocumentLoader::isLegacyPipelineFile(std::string const& filepath)
	{
		return detail::readDocumentRoot(filepath).getName() == "LegacyPipeline";
	}
}
