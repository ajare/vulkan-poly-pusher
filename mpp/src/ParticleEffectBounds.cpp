#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

#include <glm/common.hpp>
#include <glm/vec4.hpp>

#include "mpp/ParticleEffectBounds.h"

namespace mpp
{
	namespace
	{
		std::array<glm::vec3, 8> corners(ParticleEffectBounds const& bounds)
		{
			auto const halfSize = bounds.size * 0.5f;
			return {{
				bounds.center + glm::vec3(-halfSize.x, -halfSize.y, -halfSize.z),
				bounds.center + glm::vec3( halfSize.x, -halfSize.y, -halfSize.z),
				bounds.center + glm::vec3(-halfSize.x,  halfSize.y, -halfSize.z),
				bounds.center + glm::vec3( halfSize.x,  halfSize.y, -halfSize.z),
				bounds.center + glm::vec3(-halfSize.x, -halfSize.y,  halfSize.z),
				bounds.center + glm::vec3( halfSize.x, -halfSize.y,  halfSize.z),
				bounds.center + glm::vec3(-halfSize.x,  halfSize.y,  halfSize.z),
				bounds.center + glm::vec3( halfSize.x,  halfSize.y,  halfSize.z)
			}};
		}
	}

	ParticleEffectBounds transformParticleEffectBounds(ParticleEffectBounds const& bounds, glm::mat4 const& transform)
	{
		glm::vec3 minimum(std::numeric_limits<float>::max());
		glm::vec3 maximum(std::numeric_limits<float>::lowest());
		for (auto const& corner : corners(bounds))
		{
			auto const transformed = glm::vec3(transform * glm::vec4(corner, 1.0f));
			minimum = glm::min(minimum, transformed);
			maximum = glm::max(maximum, transformed);
		}
		return { (minimum + maximum) * 0.5f, maximum - minimum };
	}

	ParticleEffectBounds combineParticleEffectBounds(ParticleEffectBounds const& first, ParticleEffectBounds const& second)
	{
		auto const firstHalf = first.size * 0.5f;
		auto const secondHalf = second.size * 0.5f;
		auto const minimum = glm::min(first.center - firstHalf, second.center - secondHalf);
		auto const maximum = glm::max(first.center + firstHalf, second.center + secondHalf);
		return { (minimum + maximum) * 0.5f, maximum - minimum };
	}

	bool particleEffectBoundsIntersectFrustum(ParticleEffectBounds const& bounds,
		glm::mat4 const& transform, glm::mat4 const& viewProjection)
	{
		std::array<glm::vec4, 8> clipCorners;
		size_t index = 0;
		for (auto const& corner : corners(bounds))
		{
			auto const clip = viewProjection * transform * glm::vec4(corner, 1.0f);
			for (int component = 0; component < 4; ++component)
				if (!std::isfinite(clip[component])) return true;
			clipCorners[index++] = clip;
		}

		auto allOutside = [&](auto predicate)
		{
			return std::all_of(clipCorners.begin(), clipCorners.end(), predicate);
		};
		if (allOutside([](glm::vec4 const& value) { return value.x < -value.w; })) return false;
		if (allOutside([](glm::vec4 const& value) { return value.x >  value.w; })) return false;
		if (allOutside([](glm::vec4 const& value) { return value.y < -value.w; })) return false;
		if (allOutside([](glm::vec4 const& value) { return value.y >  value.w; })) return false;
		if (allOutside([](glm::vec4 const& value) { return value.z < -value.w; })) return false;
		if (allOutside([](glm::vec4 const& value) { return value.z >  value.w; })) return false;
		return true;
	}
}
