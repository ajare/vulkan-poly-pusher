#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "mpp/ParticleEffectBounds.h"
#include "mpp/ParticleSystem.h"

namespace mpp
{
	// Authored, reusable particle effect data. It deliberately contains no live
	// emitter handles or instance transforms; those belong to ParticleSystem.
	struct _MPPAPI ParticleEffectSpecification
	{
		struct EventRule
		{
			ParticleEventTrigger trigger{ ParticleEventTrigger::Spawn };
			ParticleEventAction action{ ParticleEventAction::SecondaryParticleBurst };
			// Authored emitter-template name; required only for secondary bursts.
			std::string targetEmitter;
			uint32_t count{ 1 };
			float age{ 0.0f };
			uint32_t payload{ 0 };
		};

		struct EmitterTemplate
		{
			std::string name;
			ParticleEmitterTemplate value;
			// Optional ResourceManager name for the appearance's albedo/flipbook.
			std::string albedoTexture;
			// A model selects mesh-particle rendering. material is an optional override;
			// when empty, each model mesh keeps its embedded material.
			std::string meshModel;
			std::string meshMaterial;
			std::vector<EventRule> events;
		};

		struct ChildEffect
		{
			// ResourceManager name of another ParticleEffect asset.
			std::string effect;
			glm::mat4 transform{ 1.0f };
			// Salt mixed into every descendant emitter seed. Equal salts intentionally
			// reproduce equal random streams; use distinct salts for independent copies.
			uint32_t seed{ 0 };
		};

		uint32_t version{ 1 };
		std::string name;
		std::optional<ParticleEffectBounds> bounds;
		// Covers this asset's emitter templates only. Child assets retain and enforce
		// their own independently-authored per-template budgets.
		uint32_t maximumParticleCount{ 0 };
		std::vector<EmitterTemplate> emitterTemplates;
		std::vector<ChildEffect> childEffects;
	};
}
