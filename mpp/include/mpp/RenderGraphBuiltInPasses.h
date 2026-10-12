#pragma once

#include <string>

#include "mpp/Config.h"
#include "mpp/UniformCollection.h"

namespace mpp
{
	class RenderGraphPassFactoryRegistry;

	// Registers reusable scene factories used by XML graph templates.
	_MPPAPI void registerBuiltInRenderGraphPasses(RenderGraphPassFactoryRegistry& registry);
}
