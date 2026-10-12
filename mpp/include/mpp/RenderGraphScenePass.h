#pragma once

#include "mpp/Config.h"

namespace mpp
{
	class RenderGraphExecutionContext;

	// Base for application-owned scene work referenced by an XML factory name.
	// Implementations keep their live scene/camera/application state in C++;
	// XML serializes only the registered factory identifier.
	class _MPPAPI RenderGraphScenePass
	{
	public:
		virtual ~RenderGraphScenePass() = default;
		virtual void execute(RenderGraphExecutionContext const& context) = 0;
	};
}
