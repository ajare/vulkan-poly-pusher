#pragma once

#include <vector>

#include "mpp/ParticleSystem.h"
#include "mpp/Resource.h"

namespace mpp
{
	// Resource wrapper for a reusable particle effect asset. Live emitter state is
	// created and owned by ParticleSystem, never by this object.
	class _MPPAPI ParticleEffect : public Resource, public ParticleEffectSource
	{
		std::vector<ParticleEmitterTemplate> mEmitterTemplates;
		std::optional<ParticleEffectBounds> mBounds;

		void createImpl() override;
		void destroyImpl() override;
		void loadImpl() override {}
		void unloadImpl() override {}

	public:
		ParticleEffect(std::string const& name, RenderSystem* renderSystem, ResourceManager* resourceManager, ResourceStreamPtr stream);
		std::span<ParticleEmitterTemplate const> getEmitterTemplates() const override { return mEmitterTemplates; }
		std::optional<ParticleEffectBounds> getBounds() const override { return mBounds; }
	};
}
