#include <utility>

#include "mpp/ProgrammaticParticleEffectStream.h"

namespace mpp
{
	ProgrammaticParticleEffectStream::ProgrammaticParticleEffectStream(ResourceManager* resourceManager)
		: ParticleEffectStream(resourceManager)
	{
	}

	void ProgrammaticParticleEffectStream::setSpecification(ParticleEffectSpecification const& specification)
	{
		mSpecification = specification;
		rebuildEmitterTemplates();
	}

	void ProgrammaticParticleEffectStream::setName(std::string name) { mSpecification.name = std::move(name); }
	void ProgrammaticParticleEffectStream::setMaximumParticleCount(uint32_t count) { mSpecification.maximumParticleCount = count; }
	void ProgrammaticParticleEffectStream::setBounds(std::optional<ParticleEffectBounds> bounds) { mSpecification.bounds = std::move(bounds); }

	void ProgrammaticParticleEffectStream::addEmitterTemplate(ParticleEffectSpecification::EmitterTemplate const& emitterTemplate)
	{
		mSpecification.emitterTemplates.push_back(emitterTemplate);
		rebuildEmitterTemplates();
	}

	void ProgrammaticParticleEffectStream::addChildEffect(ParticleEffectSpecification::ChildEffect const& childEffect)
	{
		mSpecification.childEffects.push_back(childEffect);
	}
}
