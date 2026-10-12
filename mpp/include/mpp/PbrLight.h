#pragma once
#include <glm/vec3.hpp>
#include "mpp/Config.h"

namespace mpp
{
	enum class PbrLightType
	{
		Directional,
		Point
	};

	struct _MPPAPI PbrLight
	{
		PbrLightType type{ PbrLightType::Directional };
		glm::vec3 colour{ 1.0f };
		float intensity{ 1.0f };
		glm::vec3 position{ 0.0f };
		float range{ 0.0f }; // Zero means unlimited.
		glm::vec3 direction{ 0.0f, -1.0f, 0.0f };
	};
}
