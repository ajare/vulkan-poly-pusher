#include <unordered_map>

#include "mpp/ParticleEffectStream.h"

namespace mpp
{
	ParticleEffectStream::ParticleEffectStream(ResourceManager* resourceManager)
		: ResourceStream(resourceManager, "ParticleEffect")
	{
	}

	void ParticleEffectStream::rebuildEmitterTemplates()
	{
		mEmitterTemplates.clear();
		mEmitterTemplates.reserve(mSpecification.emitterTemplates.size());
		std::unordered_map<std::string, uint32_t> indices;
		for (uint32_t index = 0; index < mSpecification.emitterTemplates.size(); ++index)
			indices.emplace(mSpecification.emitterTemplates[index].name, index);

		for (auto const& authored : mSpecification.emitterTemplates)
		{
			auto emitter = authored.value;
			emitter.events.clear();
			for (auto const& event : authored.events)
			{
				ParticleEventRule rule{ event.trigger, event.action, 0u, event.count, event.age, event.payload };
				if (event.action == ParticleEventAction::SecondaryParticleBurst)
				{
					auto target = indices.find(event.targetEmitter);
					if (target == indices.end()) continue;
					rule.targetEmitterTemplate = target->second;
				}
				emitter.events.push_back(rule);
			}
			mEmitterTemplates.push_back(std::move(emitter));
		}
		invalidateCurveLut();
	}
}
