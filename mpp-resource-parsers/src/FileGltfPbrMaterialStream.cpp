#include "mpp/Logger.h"
#include "mpp/resource-parsers/FileGltfPbrMaterialStream.h"
#include "mpp/resource-parsers/GltfPbrMaterialLoader.h"

namespace mpp::resource_parsers
{
	FileGltfPbrMaterialStream::FileGltfPbrMaterialStream(ResourceManager* resourceMgr, std::string const& filepath, bool relativisePaths)
		: FilePbrMaterialStream(resourceMgr, filepath, GltfPbrMaterialLoader::loadFirstMaterial(filepath).definition, relativisePaths)
	{
		// Warnings are repeated through the parser's normal load diagnostics in a
		// later pass; conversion itself is deliberately shared with all callers.
	}
}
