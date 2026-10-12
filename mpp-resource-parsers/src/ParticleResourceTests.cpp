#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <string>

#include "mpp/data/StructuredData.h"
#include "mpp/ParticleData.h"
#include "mpp/ParticleEffectSpecification.h"
#include "mpp/ParticleEffectValidator.h"
#include "mpp/ProgrammaticParticleEffectStream.h"
#include "mpp/resource-parsers/FileParticleEffectStream.h"
#include "mpp/resource-parsers/ParticleEffectParser.h"
#include "mpp/resource-parsers/ParticleEffectSerializer.h"
#include "mpp/resource-parsers/ParticleResourceTests.h"

namespace mpp::resource_parsers
{
	bool runParticleResourceTests(std::string* failure)
	{
		auto fail = [failure](std::string const& message)
		{
			if (failure) *failure = message;
			return false;
		};
		auto diagnosticsMatch = [](DiagnosticBag const& left, DiagnosticBag const& right)
		{
			auto const& leftValues = left.getDiagnostics();
			auto const& rightValues = right.getDiagnostics();
			if (leftValues.size() != rightValues.size()) return false;
			for (size_t index = 0; index < leftValues.size(); ++index)
			{
				auto const& leftValue = leftValues[index];
				auto const& rightValue = rightValues[index];
				if (leftValue.code != rightValue.code || leftValue.severity != rightValue.severity ||
					leftValue.message != rightValue.message || leftValue.location.document != rightValue.location.document ||
					leftValue.location.elementPath != rightValue.location.elementPath || leftValue.objectId != rightValue.objectId)
					return false;
			}
			return true;
		};
		auto hasDiagnostic = [](DiagnosticBag const& diagnostics, std::string const& code, std::string const& path)
		{
			for (auto const& diagnostic : diagnostics.getDiagnostics())
				if (diagnostic.code == code && diagnostic.location.elementPath == path) return true;
			return false;
		};

		// These values are the authored wire values used by particle resources.
		// Pinning them here prevents adding or reordering an enum from silently
		// changing an existing document's meaning.
		std::array<uint32_t, 7> const shapes{
			uint32_t(ParticleSpawnShape::Point), uint32_t(ParticleSpawnShape::Line),
			uint32_t(ParticleSpawnShape::Box), uint32_t(ParticleSpawnShape::Sphere),
			uint32_t(ParticleSpawnShape::Hemisphere), uint32_t(ParticleSpawnShape::Disc),
			uint32_t(ParticleSpawnShape::Cone)
		};
		for (size_t index = 0; index < shapes.size(); ++index)
			if (shapes[index] != index) return fail("particle spawn-shape wire values changed");

		auto const modules = uint32_t(ParticleBehaviourModule::Gravity) |
			uint32_t(ParticleBehaviourModule::Drag) | uint32_t(ParticleBehaviourModule::Noise) |
			uint32_t(ParticleBehaviourModule::Collision) | uint32_t(ParticleBehaviourModule::CurlNoise) |
			uint32_t(ParticleBehaviourModule::Turbulence) | uint32_t(ParticleBehaviourModule::VectorField);
		if (modules != 0x7fu) return fail("particle behaviour-module flags overlap or changed");
		if (uint32_t(ParticleCollisionSource::ScreenSpace | ParticleCollisionSource::Analytical |
			ParticleCollisionSource::SignedDistanceField) != 0x7u)
			return fail("particle collision-source flags overlap or changed");
		std::array<uint32_t, 5> const responses{
			uint32_t(ParticleCollisionResponse::Bounce), uint32_t(ParticleCollisionResponse::Slide),
			uint32_t(ParticleCollisionResponse::Stop), uint32_t(ParticleCollisionResponse::Kill),
			uint32_t(ParticleCollisionResponse::SpawnSecondaryEffect)
		};
		for (size_t index = 0; index < responses.size(); ++index)
			if (responses[index] != index) return fail("particle collision-response wire values changed");
		std::array<uint32_t, 4> const colliderShapes{
			uint32_t(ParticleColliderShape::Plane), uint32_t(ParticleColliderShape::Sphere),
			uint32_t(ParticleColliderShape::Box), uint32_t(ParticleColliderShape::Capsule)
		};
		for (size_t index = 0; index < colliderShapes.size(); ++index)
			if (colliderShapes[index] != index) return fail("particle collider-shape wire values changed");
		if (uint32_t(ParticleTextureAnimation::FrameOverLife | ParticleTextureAnimation::RandomStart) != 0x101u)
			return fail("particle texture-animation flags are no longer composable");
		std::array<uint32_t, 6> const billboards{
			uint32_t(ParticleBillboardMode::CameraFacing), uint32_t(ParticleBillboardMode::ScreenAligned),
			uint32_t(ParticleBillboardMode::Cylindrical), uint32_t(ParticleBillboardMode::AxisLocked),
			uint32_t(ParticleBillboardMode::VelocityAligned), uint32_t(ParticleBillboardMode::VelocityStretched)
		};
		for (size_t index = 0; index < billboards.size(); ++index)
			if (billboards[index] != index) return fail("particle billboard-mode wire values changed");
		std::array<uint32_t, 3> const blendClasses{
			uint32_t(ParticleBlendClass::Additive), uint32_t(ParticleBlendClass::Alpha),
			uint32_t(ParticleBlendClass::WeightedOit)
		};
		for (size_t index = 0; index < blendClasses.size(); ++index)
			if (blendClasses[index] != index) return fail("particle blend-class wire values changed");
		if (uint32_t(ParticleSortMode::None) != 0u || uint32_t(ParticleSortMode::BackToFront) != 1u)
			return fail("particle sort-mode wire values changed");
		if (uint32_t(ParticleLightingFlag::ProxyLight | ParticleLightingFlag::PbrLightInjection |
			ParticleLightingFlag::VolumetricContribution) != 0x7u)
			return fail("particle lighting flags overlap or changed");
		if (uint32_t(ParticleRenderMode::Billboard) != 0u || uint32_t(ParticleRenderMode::Mesh) != 1u)
			return fail("particle render-mode wire values changed");
		std::array<uint32_t, 4> const eventTriggers{
			uint32_t(ParticleEventTrigger::Spawn), uint32_t(ParticleEventTrigger::Death),
			uint32_t(ParticleEventTrigger::Collision), uint32_t(ParticleEventTrigger::Age)
		};
		for (size_t index = 0; index < eventTriggers.size(); ++index)
			if (eventTriggers[index] != index) return fail("particle event-trigger wire values changed");
		std::array<uint32_t, 5> const eventActions{
			uint32_t(ParticleEventAction::SecondaryParticleBurst), uint32_t(ParticleEventAction::Decal),
			uint32_t(ParticleEventAction::Audio), uint32_t(ParticleEventAction::Light),
			uint32_t(ParticleEventAction::GameplayCallback)
		};
		for (size_t index = 0; index < eventActions.size(); ++index)
			if (eventActions[index] != index) return fail("particle event-action wire values changed");

		ParticleEmitterTemplate emitter;
		if (emitter.simulation.emissionState[1] != 1u ||
			uint32_t(emitter.simulation.emissionRateAndPadding[1]) != uint32_t(ParticleEffectVisibilityFlag::Visible) ||
			emitter.simulation.parameterMultipliers0 != std::array<float, 4>{ 1.0f, 1.0f, 1.0f, 1.0f } ||
			emitter.appearance.textureAndAtlas[2] != 1u || emitter.appearance.textureAndAtlas[3] != 1u ||
			emitter.appearance.modes[0] != 1u || emitter.appearance.modes[2] != uint32_t(ParticleBillboardMode::CameraFacing) ||
			emitter.appearance.modes[3] != uint32_t(ParticleBlendClass::Additive) ||
			emitter.appearance.sorting[0] != uint32_t(ParticleSortMode::None) ||
			emitter.lighting.flagsAndPadding[0] != 0u ||
			emitter.lighting.colourAndIntensity != std::array<float, 4>{ 1.0f, 1.0f, 1.0f, 1.0f } ||
			emitter.lighting.rangeAndVolumetric != std::array<float, 4>{ 1.0f, 1.0f, 0.0f, 0.0f } ||
			particleAppearanceWritesDistortion(emitter.appearance) ||
			emitter.simulation.collisionConfiguration[0] != uint32_t(ParticleCollisionSource::Analytical) ||
			emitter.simulation.collisionConfiguration[1] != uint32_t(ParticleCollisionResponse::Bounce) ||
			emitter.simulation.collisionParameters != std::array<float, 4>{ 0.5f, 0.0f, 1.0f, 0.1f } ||
			emitter.simulation.turbulenceOctavesLacunarityGain != std::array<float, 4>{ 1.0f, 2.0f, 0.5f, 0.0f })
			return fail("particle emitter-template resource defaults changed");

		// Resource construction must copy templates. A reusable particle effect
		// cannot share mutable emitter data between live instances.
		auto copy = emitter;
		copy.simulation.emissionRateAndPadding[0] = 42.0f;
		copy.appearance.tintAndAlpha[0] = 0.25f;
		if (emitter.simulation.emissionRateAndPadding[0] != 0.0f || emitter.appearance.tintAndAlpha[0] != 1.0f)
			return fail("particle emitter-template copies share mutable state");

		ParticleEffectSpecification specification;
		specification.version = 2u;
		specification.name = "RoundTrip";
		specification.bounds = ParticleEffectBounds{ { 1.5f, -2.0f, 3.25f }, { 8.0f, 6.0f, 4.0f } };
		specification.maximumParticleCount = 96;
		ParticleEffectSpecification::EmitterTemplate authored;
		authored.name = "Smoke";
		authored.albedoTexture = "Textures/Smoke";
		authored.meshModel = "Models/Rock";
		authored.meshMaterial = "Materials/Rock";
		authored.value.localTransform[3][0] = 2.0f;
		auto& simulation = authored.value.simulation;
		simulation.shapeSeedModulesBudget = { uint32_t(ParticleSpawnShape::Cone), 77u,
			uint32_t(ParticleBehaviourModule::Gravity) | uint32_t(ParticleBehaviourModule::Drag) |
			uint32_t(ParticleBehaviourModule::Collision) | uint32_t(ParticleBehaviourModule::CurlNoise) |
			uint32_t(ParticleBehaviourModule::Turbulence) | uint32_t(ParticleBehaviourModule::VectorField), 96u };
		simulation.collisionConfiguration = { uint32_t(ParticleCollisionSource::ScreenSpace) |
			uint32_t(ParticleCollisionSource::Analytical) | uint32_t(ParticleCollisionSource::SignedDistanceField),
			uint32_t(ParticleCollisionResponse::SpawnSecondaryEffect), 0u, 0u };
		simulation.collisionParameters = { 0.75f, 0.2f, 0.6f, 0.15f };
		simulation.shapeParameters = { 1.0f, 2.0f, 0.5f, 0.0f };
		simulation.emissionRateAndPadding[0] = 24.0f;
		simulation.gravityAndDrag = { 0.0f, -9.81f, 0.0f, 0.2f };
		simulation.curlNoiseFrequencyStrength = { 0.5f, 0.75f, 1.0f, 2.0f };
		simulation.curlNoiseScrollAndTimeScale = { 0.1f, 0.2f, 0.3f, 1.5f };
		simulation.turbulenceFrequencyStrength = { 1.0f, 2.0f, 3.0f, 4.0f };
		simulation.turbulenceScrollAndTimeScale = { 0.4f, 0.5f, 0.6f, 0.75f };
		simulation.turbulenceOctavesLacunarityGain = { 4.0f, 2.25f, 0.4f, 0.0f };
		simulation.vectorFieldFrequencyStrength = { 0.2f, 0.3f, 0.4f, 5.0f };
		simulation.vectorFieldScrollAndTimeScale = { 0.7f, 0.8f, 0.9f, 0.5f };
		authored.value.appearance.textureAndAtlas = { 0u, 0u, 4u, 4u };
		authored.value.appearance.culling = { 250.0f, 1.5f, 0.0f, 0.035f };
		authored.value.appearance.modes = { 16u, uint32_t(ParticleTextureAnimation::FrameOverLife) | ParticleTextureRandomStartBit,
			uint32_t(ParticleBillboardMode::VelocityStretched), uint32_t(ParticleBlendClass::Alpha) };
		authored.value.appearance.sorting = { uint32_t(ParticleSortMode::BackToFront), uint32_t(ParticleRenderMode::Mesh), 1u, 0u };
		authored.value.lighting.colourAndIntensity = { 1.0f, 0.4f, 0.1f, 12.0f };
		authored.value.lighting.rangeAndVolumetric = { 8.0f, 0.35f, 0.0f, 0.0f };
		authored.value.lighting.flagsAndPadding[0] = uint32_t(ParticleLightingFlag::ProxyLight |
			ParticleLightingFlag::PbrLightInjection | ParticleLightingFlag::VolumetricContribution);
		TemplateRenderData distortionAppearance;
		distortionAppearance.sorting[2] = 1u;
		distortionAppearance.culling[3] = 0.035f;
		if (!particleAppearanceWritesDistortion(distortionAppearance))
			return fail("enabled billboard distortion appearance was not selected for distortion output");
		authored.value.curves[size_t(ParticleScalarCurve::Size)].keys = { { 0.0f, 0.25f }, { 1.0f, 2.0f } };
		authored.value.colourGradient.keys = { { 0.0f, { 1.0f, 0.5f, 0.1f } }, { 1.0f, { 0.1f, 0.2f, 0.3f } } };
		authored.events = {
			{ ParticleEventTrigger::Death, ParticleEventAction::SecondaryParticleBurst, "Sparks", 3u, 0.0f, 7u },
			{ ParticleEventTrigger::Age, ParticleEventAction::GameplayCallback, {}, 1u, 0.5f, 99u }
		};
		specification.emitterTemplates.push_back(authored);
		ParticleEffectSpecification::EmitterTemplate secondary;
		secondary.name = "Sparks";
		secondary.value.simulation.emissionState = { 1u, 0u, 0u, 0u };
		specification.emitterTemplates.push_back(secondary);
		ParticleEffectSpecification::ChildEffect child;
		child.effect = "Effects/Embers";
		child.transform[3][1] = 5.0f;
		child.seed = 1234u;
		specification.childEffects.push_back(child);

		// Semantic validation is available directly on authored in-memory data. It
		// does not require serialization, a ResourceManager, or a GL context.
		if (ParticleEffectValidator::validate(specification, "in-memory.particle.yaml").hasErrors())
			return fail("valid in-memory particle effect failed semantic validation");

		auto temporary = std::filesystem::temp_directory_path() / "mpp-particle-resource-round-trip.particle.yaml";
		try { ParticleEffectSerializer::toFile(specification, temporary.string()); }
		catch (std::exception const& error) { return fail("could not write particle effect round trip: " + std::string(error.what())); }
		auto parsed = ParticleEffectParser::fromFile(temporary.string());
		FileParticleEffectStream fileBacked(nullptr, temporary.string());
		fileBacked.load();
		std::error_code ignored; std::filesystem::remove(temporary, ignored);
		if (!parsed.succeeded() || fileBacked.getDiagnostics().hasErrors() || !fileBacked.getBounds() ||
			fileBacked.getBounds()->center != specification.bounds->center ||
			fileBacked.getBounds()->size != specification.bounds->size)
			return fail("serialized particle effect did not parse with equivalent file-backed bounds");
		auto const& restored = parsed.specification;
		if (restored.version != 2u || restored.name != specification.name || restored.maximumParticleCount != 96 ||
			!restored.bounds || restored.bounds->center != specification.bounds->center ||
			restored.bounds->size != specification.bounds->size || restored.emitterTemplates.size() != 2 ||
			restored.emitterTemplates[0].name != authored.name || restored.emitterTemplates[0].albedoTexture != authored.albedoTexture ||
			restored.emitterTemplates[0].meshModel != authored.meshModel || restored.emitterTemplates[0].meshMaterial != authored.meshMaterial ||
			restored.emitterTemplates[0].value.simulation.shapeSeedModulesBudget != simulation.shapeSeedModulesBudget ||
			restored.emitterTemplates[0].value.simulation.collisionConfiguration != simulation.collisionConfiguration ||
			restored.emitterTemplates[0].value.simulation.collisionParameters != simulation.collisionParameters ||
			restored.emitterTemplates[0].value.simulation.curlNoiseFrequencyStrength != simulation.curlNoiseFrequencyStrength ||
			restored.emitterTemplates[0].value.simulation.curlNoiseScrollAndTimeScale != simulation.curlNoiseScrollAndTimeScale ||
			restored.emitterTemplates[0].value.simulation.turbulenceFrequencyStrength != simulation.turbulenceFrequencyStrength ||
			restored.emitterTemplates[0].value.simulation.turbulenceScrollAndTimeScale != simulation.turbulenceScrollAndTimeScale ||
			restored.emitterTemplates[0].value.simulation.turbulenceOctavesLacunarityGain != simulation.turbulenceOctavesLacunarityGain ||
			restored.emitterTemplates[0].value.simulation.vectorFieldFrequencyStrength != simulation.vectorFieldFrequencyStrength ||
			restored.emitterTemplates[0].value.simulation.vectorFieldScrollAndTimeScale != simulation.vectorFieldScrollAndTimeScale ||
			restored.emitterTemplates[0].value.appearance.modes != authored.value.appearance.modes ||
			restored.emitterTemplates[0].value.appearance.culling != authored.value.appearance.culling ||
			restored.emitterTemplates[0].value.appearance.sorting != authored.value.appearance.sorting ||
			restored.emitterTemplates[0].value.lighting.colourAndIntensity != authored.value.lighting.colourAndIntensity ||
			restored.emitterTemplates[0].value.lighting.rangeAndVolumetric != authored.value.lighting.rangeAndVolumetric ||
			restored.emitterTemplates[0].value.lighting.flagsAndPadding != authored.value.lighting.flagsAndPadding ||
			restored.emitterTemplates[0].value.localTransform[3][0] != 2.0f ||
			restored.emitterTemplates[0].events.size() != 2u ||
			restored.emitterTemplates[0].events[0].targetEmitter != "Sparks" ||
			restored.emitterTemplates[0].events[0].count != 3u ||
			restored.emitterTemplates[0].events[1].trigger != ParticleEventTrigger::Age ||
			restored.emitterTemplates[0].events[1].action != ParticleEventAction::GameplayCallback ||
			restored.emitterTemplates[0].events[1].age != 0.5f ||
			restored.emitterTemplates[0].events[1].payload != 99u ||
			restored.emitterTemplates[0].value.events.size() != 2u ||
			restored.emitterTemplates[0].value.events[0].targetEmitterTemplate != 1u ||
			restored.emitterTemplates[0].value.curves[size_t(ParticleScalarCurve::Size)].keys.size() != 2 ||
			restored.emitterTemplates[0].value.colourGradient.keys.size() != 2 ||
			restored.childEffects.size() != 1u || restored.childEffects[0].effect != "Effects/Embers" ||
			restored.childEffects[0].transform[3][1] != 5.0f || restored.childEffects[0].seed != 1234u)
			return fail("particle effect did not round-trip through *.particle.yaml");

		// The C++ stream consumes the same plain specification without creating a
		// GL object or a live emitter.
		ProgrammaticParticleEffectStream programmatic(nullptr);
		programmatic.setSpecification(specification);
		if (programmatic.getSpecification().maximumParticleCount != restored.maximumParticleCount ||
			!programmatic.getBounds() || programmatic.getBounds()->center != restored.bounds->center ||
			programmatic.getBounds()->size != restored.bounds->size ||
			programmatic.getEmitterTemplates().size() != restored.emitterTemplates.size() ||
			programmatic.getEmitterTemplates()[0].simulation.shapeSeedModulesBudget != restored.emitterTemplates[0].value.simulation.shapeSeedModulesBudget ||
			programmatic.getEmitterTemplates()[0].events.size() != 2u ||
			programmatic.getEmitterTemplates()[0].events[0].targetEmitterTemplate != 1u ||
			programmatic.getSpecification().childEffects.size() != 1u)
			return fail("programmatic particle effect stream was not equivalent to parsed authoring");

		mpp::data::StructuredData childOnly("ParticleEffect");
		childOnly.addEntry("name", "ChildOnly"); childOnly.addEntry("maximumParticleCount", "0");
		mpp::data::StructuredData children("ChildEffects"), childNode("ChildEffect");
		childNode.addEntry("effect", "Effects/Leaf"); children.addEntry("ChildEffect", childNode);
		childOnly.addEntry("ChildEffects", children);
		auto childOnlyResult = ParticleEffectParser::fromData(childOnly, "child-only.particle.yaml");
		if (!childOnlyResult.succeeded() || childOnlyResult.specification.version != 1u ||
			childOnlyResult.specification.bounds || childOnlyResult.specification.childEffects.size() != 1u)
			return fail("version-1 child-only particle effect did not parse as unbounded");
		auto versionOneWithBounds = childOnly;
		mpp::data::StructuredData forbiddenBounds("Bounds");
		forbiddenBounds.addEntry("center", "0 0 0"); forbiddenBounds.addEntry("size", "1 1 1");
		versionOneWithBounds.addEntry("Bounds", forbiddenBounds);
		auto forbiddenBoundsResult = ParticleEffectParser::fromData(versionOneWithBounds, "v1-bounds.particle.yaml");
		if (!hasDiagnostic(forbiddenBoundsResult.diagnostics, "MPP-PARTICLE-002", "/ParticleEffect/Bounds"))
			return fail("version-1 particle effects accepted the version-2 Bounds block");

		mpp::data::StructuredData unboundedV2("ParticleEffect");
		unboundedV2.addEntry("version", "2"); unboundedV2.addEntry("name", "UnboundedV2");
		unboundedV2.addEntry("maximumParticleCount", "0"); unboundedV2.addEntry("ChildEffects", children);
		auto unboundedV2Result = ParticleEffectParser::fromData(unboundedV2, "unbounded-v2.particle.yaml");
		if (!unboundedV2Result.succeeded() || unboundedV2Result.specification.bounds)
			return fail("an absent version-2 Bounds block did not remain unbounded");

		mpp::data::StructuredData invalidBoundsDocument("ParticleEffect");
		invalidBoundsDocument.addEntry("version", "2"); invalidBoundsDocument.addEntry("name", "BadBounds");
		invalidBoundsDocument.addEntry("maximumParticleCount", "0"); invalidBoundsDocument.addEntry("ChildEffects", children);
		mpp::data::StructuredData invalidBounds("Bounds");
		invalidBounds.addEntry("center", "0 nan 0"); invalidBounds.addEntry("size", "1 0 -2");
		invalidBoundsDocument.addEntry("Bounds", invalidBounds);
		auto invalidBoundsResult = ParticleEffectParser::fromData(invalidBoundsDocument, "bad-bounds.particle.yaml");
		if (!hasDiagnostic(invalidBoundsResult.diagnostics, "MPP-PARTICLE-008", "/ParticleEffect/Bounds/center") ||
			!hasDiagnostic(invalidBoundsResult.diagnostics, "MPP-PARTICLE-019", "/ParticleEffect/Bounds/size"))
			return fail("version-2 non-finite centers or non-positive sizes were not diagnosed");
		auto invalidInMemoryBounds = specification;
		invalidInMemoryBounds.bounds = ParticleEffectBounds{
			{ std::numeric_limits<float>::infinity(), 0.0f, 0.0f }, { 1.0f, 1.0f, 1.0f } };
		if (!hasDiagnostic(ParticleEffectValidator::validate(invalidInMemoryBounds), "MPP-PARTICLE-019",
			"/ParticleEffect/Bounds/center"))
			return fail("in-memory non-finite particle effect bounds were not diagnosed");

		// Ordered behaviour lists are intentionally outside the schema, and a bad
		// effect-level budget is an authoring diagnostic. Neither case may throw.
		mpp::data::StructuredData malformed("ParticleEffect");
		malformed.addEntry("name", "Bad"); malformed.addEntry("maximumParticleCount", "4");
		mpp::data::StructuredData emitters("Emitters"), badEmitter("Emitter"), spawn("Spawn"), behaviours("Behaviours");
		badEmitter.addEntry("name", "BadEmitter"); badEmitter.addEntry("maximumParticleCount", "3"); spawn.addEntry("shape", "point"); badEmitter.addEntry("Spawn", spawn);
		behaviours.addEntry("Module", "gravity"); badEmitter.addEntry("Behaviours", behaviours); emitters.addEntry("Emitter", badEmitter); malformed.addEntry("Emitters", emitters);
		auto invalid = ParticleEffectParser::fromData(malformed, "malformed.particle.yaml");
		if (!invalid.diagnostics.hasErrors() || invalid.diagnostics.count(DiagnosticSeverity::Error) < 2)
			return fail("malformed particle effect did not return behaviour-schema and budget diagnostics");
		try
		{
			FileParticleEffectStream malformedStream(nullptr, "malformed.particle.yaml", malformed);
			malformedStream.load();
			if (!malformedStream.getDiagnostics().hasErrors()) return fail("file particle stream discarded malformed-document diagnostics");
			FileParticleEffectStream unreadableStream(nullptr, "missing.particle.yaml");
			unreadableStream.load();
			if (!unreadableStream.getDiagnostics().hasErrors()) return fail("unreadable particle document did not produce diagnostics");
		}
		catch (...) { return fail("malformed particle file stream threw instead of returning diagnostics"); }

		// The parser and file-backed resource stream must expose exactly the shared
		// validator's semantic codes and document/object locations.
		auto semanticallyInvalid = specification;
		semanticallyInvalid.maximumParticleCount = 95u;
		auto& invalidEmitter = semanticallyInvalid.emitterTemplates[0];
		invalidEmitter.value.simulation.emissionRateAndPadding[0] = -1.0f;
		invalidEmitter.value.curves[size_t(ParticleScalarCurve::Size)].keys = { { 0.75f, 1.0f }, { 0.25f, 2.0f } };
		invalidEmitter.value.appearance.modes[0] = 17u;
		invalidEmitter.value.lighting.flagsAndPadding[0] = uint32_t(ParticleLightingFlag::PbrLightInjection);
		invalidEmitter.value.lighting.rangeAndVolumetric[0] = 0.0f;
		invalidEmitter.value.lighting.colourAndIntensity[0] = -1.0f;
		invalidEmitter.events[0].targetEmitter = "Missing";
		invalidEmitter.events[0].count = 0u;

		auto invalidTemporary = std::filesystem::temp_directory_path() / "mpp-particle-resource-invalid.particle.yaml";
		auto directDiagnostics = ParticleEffectValidator::validate(semanticallyInvalid, invalidTemporary.string());
		if (!hasDiagnostic(directDiagnostics, "MPP-PARTICLE-011", "/ParticleEffect/Emitters/Emitter[0]/Spawn") ||
			!hasDiagnostic(directDiagnostics, "MPP-PARTICLE-015", "/ParticleEffect/Emitters/Emitter[0]/Events/Event[0]/count") ||
			!hasDiagnostic(directDiagnostics, "MPP-PARTICLE-010", "/ParticleEffect/Emitters/Emitter[0]/Curves/Size/Keys/Key/time") ||
			!hasDiagnostic(directDiagnostics, "MPP-PARTICLE-018", "/ParticleEffect/Emitters/Emitter[0]/Lighting/lightInjection") ||
			!hasDiagnostic(directDiagnostics, "MPP-PARTICLE-012", "/ParticleEffect/Emitters/Emitter[0]/Appearance") ||
			!hasDiagnostic(directDiagnostics, "MPP-PARTICLE-017", "/ParticleEffect/Emitters/Emitter[0]/Events/Event[0]/targetEmitter") ||
			!hasDiagnostic(directDiagnostics, "MPP-PARTICLE-014", "/ParticleEffect/maximumParticleCount"))
			return fail("direct particle semantic validation did not report the expected diagnostics");
		try { ParticleEffectSerializer::toFile(semanticallyInvalid, invalidTemporary.string()); }
		catch (std::exception const& error) { return fail("could not write invalid particle effect fixture: " + std::string(error.what())); }
		auto invalidParsed = ParticleEffectParser::fromFile(invalidTemporary.string());
		if (!directDiagnostics.hasErrors() || !diagnosticsMatch(directDiagnostics, invalidParsed.diagnostics))
		{
			std::filesystem::remove(invalidTemporary, ignored);
			return fail("particle parser diagnostics differed from direct semantic validation");
		}
		try
		{
			FileParticleEffectStream invalidStream(nullptr, invalidTemporary.string());
			invalidStream.load();
			if (!diagnosticsMatch(directDiagnostics, invalidStream.getDiagnostics()))
			{
				std::filesystem::remove(invalidTemporary, ignored);
				return fail("file particle stream diagnostics differed from direct semantic validation");
			}
		}
		catch (...)
		{
			std::filesystem::remove(invalidTemporary, ignored);
			return fail("semantically invalid particle file stream threw instead of returning diagnostics");
		}
		std::filesystem::remove(invalidTemporary, ignored);

		return true;
	}
}
