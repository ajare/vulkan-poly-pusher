#pragma once

#include <string>

#include "mpp/Config.h"

namespace mpp
{
	class RenderSystem;

	// Requires an initialized OpenGL RenderSystem. Exercises the GPU-owned
	// particle pool and reads assertions from frame-lagged staging buffers.
	_MPPAPI bool runParticleGpuTests(RenderSystem* renderSystem, std::string* failure = nullptr);
}
