#pragma once

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include "mpp/Config.h"

namespace mpp
{
	// Optional authored local-space aggregate for one particle effect. Absence,
	// rather than a flag on this value, represents an unbounded particle effect.
	struct _MPPAPI ParticleEffectBounds
	{
		glm::vec3 center{ 0.0f };
		glm::vec3 size{ 1.0f };
	};

	// Transform an AABB conservatively and return its enclosing AABB.
	_MPPAPI ParticleEffectBounds transformParticleEffectBounds(
		ParticleEffectBounds const& bounds, glm::mat4 const& transform);
	_MPPAPI ParticleEffectBounds combineParticleEffectBounds(
		ParticleEffectBounds const& first, ParticleEffectBounds const& second);
	// Uses OpenGL clip-space planes. Non-finite transformed corners are retained
	// conservatively rather than risking rejection of visible work.
	_MPPAPI bool particleEffectBoundsIntersectFrustum(ParticleEffectBounds const& bounds,
		glm::mat4 const& transform, glm::mat4 const& viewProjection);
}
