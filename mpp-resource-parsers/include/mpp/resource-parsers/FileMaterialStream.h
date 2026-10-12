#pragma once

#include <string>

#include "mpp/ResourceStream.h"
#include "mpp/resource-parsers/Config.h"

namespace mpp
{
	class ResourceManager;
	namespace resource_parsers
	{
		// Neutral tag-dispatching entry point. The XML root, never the filename,
		// selects BasicMaterial or PbrMaterial. Legacy Material is read-only input.
		class _MPPRESOURCEPARSERSAPI FileMaterialStream
		{
		public:
			static ResourceStreamPtr fromFile(ResourceManager* resourceMgr, std::string const& filepath, bool relativisePaths = true);
		};
	}
}
