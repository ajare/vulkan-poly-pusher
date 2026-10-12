#pragma once

#include "mpp/resource-parsers/Config.h"
#include "mpp/resource-parsers/FilePbrMaterialStream.h"

namespace mpp::resource_parsers
{
	class _MPPRESOURCEPARSERSAPI FileGltfPbrMaterialStream final : public FilePbrMaterialStream
	{
	public:
		FileGltfPbrMaterialStream(ResourceManager* resourceMgr, std::string const& filepath, bool relativisePaths = true);
	};
}
