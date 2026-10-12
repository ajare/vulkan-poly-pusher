#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <unordered_set>
#include <utility>

#include "mpp/ParticleEffectValidator.h"

namespace mpp
{
	namespace
	{
		std::string emitterPath(size_t index)
		{
			return "/ParticleEffect/Emitters/Emitter[" + std::to_string(index) + "]";
		}

		void validateCurve(DiagnosticBag& diagnostics, ParticleCurve const& curve,
			std::string const& sourceName, std::string const& path)
		{
			float previous = -1.0f;
			for (auto const& key : curve.keys)
			{
				if (key.time < 0.0f || key.time > 1.0f || key.time < previous)
					diagnostics.error("MPP-PARTICLE-010", "Curve key times must be ordered in [0, 1].", { sourceName, path + "/Keys/Key/time" });
				previous = key.time;
			}
		}

		void validateGradient(DiagnosticBag& diagnostics, ParticleGradient const& gradient,
			std::string const& sourceName, std::string const& path)
		{
			float previous = -1.0f;
			for (auto const& key : gradient.keys)
			{
				if (key.time < 0.0f || key.time > 1.0f || key.time < previous)
					diagnostics.error("MPP-PARTICLE-010", "Gradient key times must be ordered in [0, 1].", { sourceName, path + "/Keys/Key/time" });
				previous = key.time;
			}
		}
	}

	DiagnosticBag ParticleEffectValidator::validate(ParticleEffectSpecification const& specification, std::string const& sourceName)
	{
		DiagnosticBag diagnostics;
		auto error = [&](std::string code, std::string message, std::string path)
		{
			diagnostics.error(std::move(code), std::move(message), { sourceName, std::move(path) });
		};

		if (specification.version != 1 && specification.version != 2)
			error("MPP-PARTICLE-013", "Unsupported particle effect version; expected 1 or 2.", "/ParticleEffect/version");
		if (specification.bounds)
		{
			if (specification.version != 2)
				error("MPP-PARTICLE-019", "Particle effect bounds require version 2.", "/ParticleEffect/Bounds");
			auto const& bounds = *specification.bounds;
			if (!std::isfinite(bounds.center.x) || !std::isfinite(bounds.center.y) || !std::isfinite(bounds.center.z))
				error("MPP-PARTICLE-019", "Particle effect bounds center must contain three finite values.", "/ParticleEffect/Bounds/center");
			if (!std::isfinite(bounds.size.x) || !std::isfinite(bounds.size.y) || !std::isfinite(bounds.size.z) ||
				bounds.size.x <= 0.0f || bounds.size.y <= 0.0f || bounds.size.z <= 0.0f)
				error("MPP-PARTICLE-019", "Particle effect bounds size must contain three finite, strictly positive values.", "/ParticleEffect/Bounds/size");
		}

		std::array<std::pair<char const*, ParticleScalarCurve>, 6> const curveNames{{
			{ "Size", ParticleScalarCurve::Size },
			{ "Alpha", ParticleScalarCurve::Alpha },
			{ "VelocityMultiplier", ParticleScalarCurve::VelocityMultiplier },
			{ "Drag", ParticleScalarCurve::Drag },
			{ "RotationSpeed", ParticleScalarCurve::RotationSpeed },
			{ "EmissiveIntensity", ParticleScalarCurve::EmissiveIntensity }
		}};

		for (size_t index = 0; index < specification.emitterTemplates.size(); ++index)
		{
			auto const& authored = specification.emitterTemplates[index];
			auto const& emitter = authored.value;
			auto const& simulation = emitter.simulation;
			auto const path = emitterPath(index);
			auto const spawnPath = path + "/Spawn";

			auto finite = [](auto const& values)
			{
				return std::all_of(values.begin(), values.end(), [](float value) { return std::isfinite(value); });
			};
			auto ordered = [](auto const& minimum, auto const& maximum, size_t count)
			{
				for (size_t component = 0; component < count; ++component)
					if (minimum[component] > maximum[component]) return false;
				return true;
			};
			auto const shape = simulation.shapeSeedModulesBudget[0];
			bool shapeParametersValid = finite(simulation.shapeParameters);
			if (shape == uint32_t(ParticleSpawnShape::Box))
				shapeParametersValid = shapeParametersValid && simulation.shapeParameters[0] >= 0.0f &&
					simulation.shapeParameters[1] >= 0.0f && simulation.shapeParameters[2] >= 0.0f;
			else if (shape >= uint32_t(ParticleSpawnShape::Sphere) && shape <= uint32_t(ParticleSpawnShape::Cone))
				shapeParametersValid = shapeParametersValid && simulation.shapeParameters[0] >= 0.0f &&
					(shape != uint32_t(ParticleSpawnShape::Cone) || simulation.shapeParameters[1] >= 0.0f);
			if (shape > uint32_t(ParticleSpawnShape::Cone) || !shapeParametersValid)
				error("MPP-PARTICLE-011", "Spawn shape and shape parameters must be finite and valid for the selected shape.", spawnPath);
			if (simulation.emissionState[0] > 1u || simulation.emissionState[1] > 1u ||
				!finite(simulation.emissionRateAndPadding) || simulation.emissionRateAndPadding[0] < 0.0f ||
				!finite(simulation.initialVelocityMin) || !finite(simulation.initialVelocityMax) ||
				!ordered(simulation.initialVelocityMin, simulation.initialVelocityMax, 3u) ||
				!finite(simulation.colourMin) || !finite(simulation.colourMax) ||
				!ordered(simulation.colourMin, simulation.colourMax, 4u) ||
				!finite(simulation.lifetimeSizeRanges) || simulation.lifetimeSizeRanges[0] < 0.0f ||
				simulation.lifetimeSizeRanges[1] < simulation.lifetimeSizeRanges[0] ||
				simulation.lifetimeSizeRanges[2] < 0.0f ||
				simulation.lifetimeSizeRanges[3] < simulation.lifetimeSizeRanges[2] ||
				!finite(simulation.rotationRanges) || simulation.rotationRanges[1] < simulation.rotationRanges[0] ||
				simulation.rotationRanges[3] < simulation.rotationRanges[2])
			{
				error("MPP-PARTICLE-011", "Spawn modes and ranges must be finite, valid, and ordered.", spawnPath);
			}

			auto const modules = simulation.shapeSeedModulesBudget[2];
			constexpr uint32_t knownModules = uint32_t(ParticleBehaviourModule::Gravity) |
				uint32_t(ParticleBehaviourModule::Drag) | uint32_t(ParticleBehaviourModule::Noise) |
				uint32_t(ParticleBehaviourModule::Collision) | uint32_t(ParticleBehaviourModule::CurlNoise) |
				uint32_t(ParticleBehaviourModule::Turbulence) | uint32_t(ParticleBehaviourModule::VectorField);
			if ((modules & ~knownModules) != 0u)
				error("MPP-PARTICLE-011", "Behaviour module mask contains unrecognized flags.", path + "/Behaviours");
			if ((modules & uint32_t(ParticleBehaviourModule::Gravity)) != 0u &&
				(!std::isfinite(simulation.gravityAndDrag[0]) || !std::isfinite(simulation.gravityAndDrag[1]) ||
				 !std::isfinite(simulation.gravityAndDrag[2])))
				error("MPP-PARTICLE-011", "Gravity acceleration must contain three finite values.", path + "/Behaviours/Gravity");
			if ((modules & uint32_t(ParticleBehaviourModule::Drag)) != 0u &&
				(!std::isfinite(simulation.gravityAndDrag[3]) || simulation.gravityAndDrag[3] < 0.0f))
				error("MPP-PARTICLE-011", "Drag coefficient must be finite and non-negative.", path + "/Behaviours/Drag");

			auto validateField = [&](ParticleBehaviourModule module, auto const& frequencyStrength,
				auto const& scrollTimeScale, char const* name)
			{
				if ((modules & uint32_t(module)) != 0u && (!finite(frequencyStrength) || !finite(scrollTimeScale)))
					error("MPP-PARTICLE-011", std::string(name) + " frequency, strength, scroll, and time scale must be finite.",
						path + "/Behaviours/" + name);
			};
			validateField(ParticleBehaviourModule::Noise, simulation.noiseFrequencyStrength,
				simulation.noiseScrollAndTimeScale, "Noise");
			validateField(ParticleBehaviourModule::CurlNoise, simulation.curlNoiseFrequencyStrength,
				simulation.curlNoiseScrollAndTimeScale, "CurlNoise");
			validateField(ParticleBehaviourModule::Turbulence, simulation.turbulenceFrequencyStrength,
				simulation.turbulenceScrollAndTimeScale, "Turbulence");
			validateField(ParticleBehaviourModule::VectorField, simulation.vectorFieldFrequencyStrength,
				simulation.vectorFieldScrollAndTimeScale, "VectorField");

			if ((modules & uint32_t(ParticleBehaviourModule::Turbulence)) != 0u)
			{
				auto const& values = simulation.turbulenceOctavesLacunarityGain;
				if (!finite(values) || values[0] < 1.0f || values[0] > 8.0f || std::floor(values[0]) != values[0] ||
					values[1] < 1.0f || values[2] < 0.0f || values[2] > 1.0f)
				{
					error("MPP-PARTICLE-011", "Turbulence octaves must be an integer in [1, 8], lacunarity at least 1, and gain in [0, 1].",
						path + "/Behaviours/Turbulence");
				}
			}

			if ((modules & uint32_t(ParticleBehaviourModule::Collision)) != 0u)
			{
				auto const sources = simulation.collisionConfiguration[0];
				constexpr uint32_t knownSources = uint32_t(ParticleCollisionSource::ScreenSpace) |
					uint32_t(ParticleCollisionSource::Analytical) | uint32_t(ParticleCollisionSource::SignedDistanceField);
				if (sources == 0u || (sources & ~knownSources) != 0u)
					error("MPP-PARTICLE-011", "Collision sources must select one or more recognized collision-source flags.",
						path + "/Behaviours/Collision/sources");
				if (simulation.collisionConfiguration[1] > uint32_t(ParticleCollisionResponse::SpawnSecondaryEffect))
					error("MPP-PARTICLE-011", "Collision response must be bounce, slide, stop, kill, or spawnSecondaryEffect.",
						path + "/Behaviours/Collision/response");
				auto const& values = simulation.collisionParameters;
				if (!finite(values) || values[0] < 0.0f || values[1] < 0.0f || values[1] > 1.0f ||
					values[2] < 0.0f || values[3] < 0.0f)
				{
					error("MPP-PARTICLE-011", "Collision restitution, radius scale, and thickness must be finite and non-negative; friction must be in [0, 1].",
						path + "/Behaviours/Collision");
				}
			}

			for (size_t eventIndex = 0; eventIndex < authored.events.size(); ++eventIndex)
			{
				auto const& event = authored.events[eventIndex];
				auto const eventPath = path + "/Events/Event[" + std::to_string(eventIndex) + "]";
				if (event.action == ParticleEventAction::SecondaryParticleBurst && event.count == 0u)
					error("MPP-PARTICLE-015", "A secondary particle burst count must be greater than zero.", eventPath + "/count");
				if (event.age < 0.0f)
					error("MPP-PARTICLE-015", "A particle age event must have a non-negative age.", eventPath + "/age");
			}

			auto const curvePath = path + "/Curves";
			for (auto const& named : curveNames)
				validateCurve(diagnostics, emitter.curves[size_t(named.second)], sourceName, curvePath + "/" + named.first);
			validateGradient(diagnostics, emitter.colourGradient, sourceName, curvePath + "/Colour");

			auto const lightingPath = path + "/Lighting";
			auto const& lighting = emitter.lighting;
			auto const lightingFlags = lighting.flagsAndPadding[0];
			constexpr uint32_t knownLightingFlags = uint32_t(ParticleLightingFlag::ProxyLight) |
				uint32_t(ParticleLightingFlag::PbrLightInjection) |
				uint32_t(ParticleLightingFlag::VolumetricContribution);
			if ((lightingFlags & ~knownLightingFlags) != 0u)
				error("MPP-PARTICLE-018", "Particle lighting contains unrecognized flags.", lightingPath);
			bool const proxy = (lightingFlags & uint32_t(ParticleLightingFlag::ProxyLight)) != 0u;
			bool const injection = (lightingFlags & uint32_t(ParticleLightingFlag::PbrLightInjection)) != 0u;
			bool const volumetric = (lightingFlags & uint32_t(ParticleLightingFlag::VolumetricContribution)) != 0u;
			if (injection && !proxy)
				error("MPP-PARTICLE-018", "Particle light injection requires proxyLight: true.", lightingPath + "/lightInjection");
			if ((proxy || injection || volumetric) && lighting.rangeAndVolumetric[0] <= 0.0f)
				error("MPP-PARTICLE-018", "Enabled particle lighting requires a positive range.", lightingPath + "/range");
			if (!finite(lighting.colourAndIntensity) || !finite(lighting.rangeAndVolumetric) ||
				lighting.colourAndIntensity[0] < 0.0f || lighting.colourAndIntensity[1] < 0.0f ||
				lighting.colourAndIntensity[2] < 0.0f || lighting.colourAndIntensity[3] < 0.0f ||
				lighting.rangeAndVolumetric[1] < 0.0f)
			{
				error("MPP-PARTICLE-018", "Particle lighting colour and intensities must be non-negative.", lightingPath);
			}

			auto const appearancePath = path + "/Appearance";
			auto const& appearance = emitter.appearance;
			if (appearance.textureAndAtlas[2] == 0u || appearance.textureAndAtlas[3] == 0u ||
				appearance.modes[0] == 0u || appearance.modes[0] > appearance.textureAndAtlas[2] * appearance.textureAndAtlas[3])
			{
				error("MPP-PARTICLE-012", "Atlas dimensions must be positive and contain frameCount.", appearancePath);
			}
			if (!finite(appearance.tintAndAlpha) || !finite(appearance.appearance) || !finite(appearance.culling) ||
				appearance.tintAndAlpha[3] < 0.0f || appearance.appearance[0] < 0.0f ||
				appearance.appearance[1] < 0.0f || appearance.appearance[2] < 0.0f ||
				appearance.culling[0] < 0.0f || appearance.culling[1] < 0.0f || appearance.culling[3] < 0.0f)
				error("MPP-PARTICLE-011", "Particle appearance values must be finite and non-negative where required.", appearancePath);
			auto const animationBits = appearance.modes[1];
			auto const playback = animationBits & ParticleTexturePlaybackMask;
			if (playback > uint32_t(ParticleTextureAnimation::FixedRate) ||
				(animationBits & ~(ParticleTexturePlaybackMask | ParticleTextureRandomStartBit)) != 0u ||
				appearance.modes[2] > uint32_t(ParticleBillboardMode::VelocityStretched) ||
				appearance.modes[3] > uint32_t(ParticleBlendClass::WeightedOit) ||
				appearance.sorting[0] > uint32_t(ParticleSortMode::BackToFront) ||
				appearance.sorting[1] > uint32_t(ParticleRenderMode::Mesh) || appearance.sorting[2] > 1u)
				error("MPP-PARTICLE-011", "Particle animation, billboard, blend-class, render, distortion, and sorting values must be recognized.", appearancePath);
			if (authored.meshModel.empty() && !authored.meshMaterial.empty())
				error("MPP-PARTICLE-011", "A mesh material override requires a mesh model.", path + "/Mesh/material");
		}

		if (specification.emitterTemplates.empty() && specification.childEffects.empty())
			error("MPP-PARTICLE-003", "Particle effect requires at least one emitter template or child particle effect.", "/ParticleEffect");

		std::unordered_set<std::string> emitterNames;
		for (size_t index = 0; index < specification.emitterTemplates.size(); ++index)
		{
			auto const& emitter = specification.emitterTemplates[index];
			auto const path = emitterPath(index);
			if (emitter.name.empty() || !emitterNames.emplace(emitter.name).second)
				error("MPP-PARTICLE-016", "Emitter template names must be non-empty and unique.", path + "/name");
			for (size_t eventIndex = 0; eventIndex < emitter.events.size(); ++eventIndex)
			{
				auto const& event = emitter.events[eventIndex];
				if (event.action != ParticleEventAction::SecondaryParticleBurst) continue;
				auto const found = std::find_if(specification.emitterTemplates.begin(), specification.emitterTemplates.end(),
					[&](auto const& candidate) { return candidate.name == event.targetEmitter; });
				if (found == specification.emitterTemplates.end())
				{
					error("MPP-PARTICLE-017", "Secondary particle event target emitter '" + event.targetEmitter + "' does not exist.",
						path + "/Events/Event[" + std::to_string(eventIndex) + "]/targetEmitter");
				}
			}
		}

		uint64_t total = 0;
		for (auto const& emitter : specification.emitterTemplates)
			total += emitter.value.simulation.shapeSeedModulesBudget[3];
		if (total > std::numeric_limits<uint32_t>::max() || total != specification.maximumParticleCount)
		{
			error("MPP-PARTICLE-014", "maximumParticleCount must equal the sum of emitter-template budgets (" + std::to_string(total) + ").",
				"/ParticleEffect/maximumParticleCount");
		}

		return diagnostics;
	}
}
