#pragma once

#include <string>
#include <functional>

#include "mpp/Scene.h"
#include "mpp/Config.h"

namespace mpp
{
	class RenderSystem;

	typedef std::function<ScenePtr(RenderSystem*)> SceneFactory;

}