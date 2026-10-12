#pragma once

#include <string>
#include "mpp/AntiAliasing.h"
#include "mpp/Config.h"

namespace mpp
{
	// Names a logical colour output produced by a render pipeline. The image and
	// optional TAA depth source refer to authored render-graph image names.
	struct _MPPAPI RenderPipelineOutput
	{
		std::string name;
		std::string image;
		std::string taaDepth;
		AntiAliasingOverrides antiAliasing;
	};
}
