#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <utility>
#include <vector>

#include "mpp/ParticleEffect.h"
#include "mpp/ParticleEffectStream.h"
#include "mpp/ResourceManager.h"
#include "mpp/MppException.h"

namespace mpp
{
	namespace
	{
		thread_local std::vector<ParticleEffect const*> creatingEffects;

		struct CreationGuard
		{
			explicit CreationGuard(ParticleEffect const* effect)
			{
				if (std::find(creatingEffects.begin(), creatingEffects.end(), effect) != creatingEffects.end())
					throw std::invalid_argument("Particle effect child references must be acyclic.");
				creatingEffects.push_back(effect);
			}
			~CreationGuard() { creatingEffects.pop_back(); }
		};

		uint32_t deriveSeed(uint32_t seed, uint32_t salt)
		{
			// A stable 32-bit avalanche keeps nested composition deterministic without
			// coupling random streams to child list order.
			uint32_t value = seed ^ (salt + 0x9e3779b9u + (seed << 6u) + (seed >> 2u));
			value ^= value >> 16u;
			value *= 0x7feb352du;
			value ^= value >> 15u;
			value *= 0x846ca68bu;
			return value ^ (value >> 16u);
		}
	}

	ParticleEffect::ParticleEffect(std::string const& name, RenderSystem* renderSystem, ResourceManager* resourceManager, ResourceStreamPtr stream)
		: Resource(name, "ParticleEffect", renderSystem, resourceManager, std::move(stream))
	{
	}

	void ParticleEffect::createImpl()
	{
		CreationGuard guard(this);
		auto stream = dynamic_cast<ParticleEffectStream*>(getResourceStream().get());
		if (!stream) THROW_MPP("ParticleEffect resource requires a ParticleEffectStream.", __LINE__, __FILE__, __func__);
		mEmitterTemplates.assign(stream->getEmitterTemplates().begin(), stream->getEmitterTemplates().end());
		mBounds = stream->getBounds();
		auto const& specification = stream->getSpecification();
		for (size_t index = 0; index < specification.emitterTemplates.size(); ++index)
		{
			auto resolve = [&](std::string const& name, ResourcePtr& destination)
			{
				if (name.empty()) return;
				auto slash = getName().find_last_of('/');
				auto qualified = slash == std::string::npos ? name : getName().substr(0, slash + 1) + name;
				auto resource = getResourceManager()->getResource(qualified, true);
				if (!resource) resource = getResourceManager()->getResource(name);
				destination = resource;
				acquireDependentResource(resource);
			};
			auto const& authored = specification.emitterTemplates[index];
			resolve(authored.albedoTexture, mEmitterTemplates[index].albedoTexture);
			resolve(authored.meshModel, mEmitterTemplates[index].meshModel);
			resolve(authored.meshMaterial, mEmitterTemplates[index].meshMaterial);
		}

		for (auto const& authoredChild : specification.childEffects)
		{
			auto slash = getName().find_last_of('/');
			auto qualified = slash == std::string::npos ? authoredChild.effect : getName().substr(0, slash + 1) + authoredChild.effect;
			auto resource = getResourceManager()->getResource(qualified, true);
			if (!resource) resource = getResourceManager()->getResource(authoredChild.effect);
			auto child = std::dynamic_pointer_cast<ParticleEffect>(resource);
			if (!child)
				throw std::invalid_argument("Particle effect child '" + authoredChild.effect + "' is not a ParticleEffect resource.");
			if (std::find(creatingEffects.begin(), creatingEffects.end(), child.get()) != creatingEffects.end())
				throw std::invalid_argument("Particle effect child references must be acyclic.");
			if (!child->isCreated()) child->create();
			acquireDependentResource(resource);

			if (mBounds)
			{
				auto const childBounds = child->getBounds();
				if (childBounds)
					mBounds = combineParticleEffectBounds(*mBounds,
						transformParticleEffectBounds(*childBounds, authoredChild.transform));
				else mBounds.reset();
			}

			auto const firstChildTemplate = uint32_t(mEmitterTemplates.size());
			for (auto childTemplate : child->getEmitterTemplates())
			{
				childTemplate.localTransform = authoredChild.transform * childTemplate.localTransform;
				childTemplate.simulation.shapeSeedModulesBudget[1] =
					deriveSeed(childTemplate.simulation.shapeSeedModulesBudget[1], authoredChild.seed);
				for (auto& event : childTemplate.events)
					if (event.action == ParticleEventAction::SecondaryParticleBurst)
						event.targetEmitterTemplate += firstChildTemplate;
				mEmitterTemplates.push_back(std::move(childTemplate));
			}
		}
		invalidateCurveLut();
	}

	void ParticleEffect::destroyImpl()
	{
		mEmitterTemplates.clear();
		mBounds.reset();
		invalidateCurveLut();
	}
}
