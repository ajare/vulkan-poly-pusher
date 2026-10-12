#pragma once

#include <string>

#include "mpp/Config.h"

namespace mpp
{
	class RenderSystem;

	// Requires an initialized OpenGL RenderSystem. Exercises real framebuffer
	// allocation, execution, MRT, resize planning, and target release.
	_MPPAPI bool runRenderGraphGpuTests(RenderSystem* renderSystem, std::string* failure = nullptr);
}
