#pragma once

#include <span>
#include <vector>

#include "mpp/ParticleEffectSpecification.h"
#include "mpp/ResourceStream.h"

namespace mpp
{
	class _MPPAPI ParticleEffectStream : public ResourceStream, public ParticleEffectSource
	{
		friend class ParticleEffect;

	protected:
		ParticleEffectSpecification mSpecification;
		std::vector<ParticleEmitterTemplate> mEmitterTemplates;
		void rebuildEmitterTemplates();

	public:
		explicit ParticleEffectStream(ResourceManager* resourceManager);
		ParticleEffectSpecification const& getSpecification() const { return mSpecification; }
		std::span<ParticleEmitterTemplate const> getEmitterTemplates() const override { return mEmitterTemplates; }
		std::optional<ParticleEffectBounds> getBounds() const override { return mSpecification.bounds; }
	};
}
