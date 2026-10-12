#pragma once

#include <string>

#include "mpp/resource-parsers/Config.h"

namespace mpp { class ResourceManager; }

namespace mpp::resource_parsers
{
	// XML/YAML dispatch, single-definition binary round-trip and legacy migration tests.
	_MPPRESOURCEPARSERSAPI bool runMaterialResourceTests(ResourceManager* resourceMgr, std::string* failure = nullptr);
}
