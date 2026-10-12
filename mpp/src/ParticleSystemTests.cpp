#include <array>
#include <cmath>
#include <string>

#include <glm/gtc/matrix_transform.hpp>

#include "mpp/Logger.h"
#include "mpp/ParticleCurveLut.h"
#include "mpp/ParticleEffect.h"
#include "mpp/ParticleEffectSpecification.h"
#include "mpp/ParticleSystem.h"
#include "mpp/ParticleSystemTests.h"
#include "mpp/ProgrammaticParticleEffectStream.h"
#include "mpp/ResourceManager.h"
#include "mpp/TrailSystem.h"

using namespace std;

namespace mpp
{
	bool runParticleSystemCpuTests(string* failure)
	{
		auto fail = [failure](string const& message)
		{
			if (failure) *failure = message;
			return false;
		};
		auto step = [](ParticleSystem& system, float dt)
		{
			system.mSimulationSeconds += dt;
			system.buildSpawnCommands(dt);
			system.mSpawnCommands.clear();
			system.retireCompletedEmitters();
		};
		auto burst = [](float lifetime)
		{
			ParticleEmitterTemplate value;
			value.simulation.emissionState = { 1u, 1u, 1u, 0u };
			value.simulation.lifetimeSizeRanges[0] = lifetime;
			value.simulation.lifetimeSizeRanges[1] = lifetime;
			return value;
		};

		ParticleSystem timeControls(nullptr, nullptr);
		auto defaultDelta = timeControls.resolveSimulationDelta(0.02f);
		if (timeControls.isSimulationPaused() || timeControls.getSimulationTimeScale() != 1.0f ||
			!defaultDelta || *defaultDelta != 0.02f)
			return fail("particle simulation did not default to unscaled real time");
		timeControls.pauseSimulation();
		if (!timeControls.isSimulationPaused() || timeControls.resolveSimulationDelta(0.02f))
			return fail("paused particle simulation still produced a delta");
		timeControls.requestSimulationStep(0.025f);
		auto requestedStep = timeControls.resolveSimulationDelta(1.0f);
		if (!requestedStep || *requestedStep != 0.025f || !timeControls.isSimulationPaused() ||
			timeControls.resolveSimulationDelta(0.02f))
			return fail("a particle simulation step was not consumed exactly once while remaining paused");
		timeControls.resumeSimulation();
		timeControls.setSimulationTimeScale(0.5f);
		auto halfSpeed = timeControls.resolveSimulationDelta(0.08f);
		timeControls.setSimulationTimeScale(2.0f);
		auto doubleSpeed = timeControls.resolveSimulationDelta(0.03f);
		if (timeControls.isSimulationPaused() || !halfSpeed || std::abs(*halfSpeed - 0.04f) > 0.000001f ||
			!doubleSpeed || std::abs(*doubleSpeed - 0.06f) > 0.000001f)
			return fail("particle simulation time scale did not predictably scale real deltas");
		timeControls.setSimulationTimeScale(0.0f);
		if (timeControls.resolveSimulationDelta(0.02f))
			return fail("zero particle simulation time scale still produced a delta");
		bool rejectedScale = false, rejectedStep = false;
		try { timeControls.setSimulationTimeScale(-1.0f); }
		catch (invalid_argument const&) { rejectedScale = true; }
		try { timeControls.requestSimulationStep(MaximumParticleDeltaSeconds + 0.01f); }
		catch (invalid_argument const&) { rejectedStep = true; }
		if (!rejectedScale || !rejectedStep)
			return fail("invalid particle simulation time controls were accepted");

		ParticleEmitterTemplate curved;
		curved.curves[size_t(ParticleScalarCurve::Size)].keys = { { 0.0f, 0.5f }, { 1.0f, 2.0f } };
		curved.curves[size_t(ParticleScalarCurve::EmissiveIntensity)].keys = { { 0.0f, 1.0f }, { 1.0f, 6.0f } };
		curved.colourGradient.keys = { { 0.0f, { 1.0f, 0.0f, 0.0f } }, { 1.0f, { 0.0f, 0.5f, 3.0f } } };
		array<ParticleEmitterTemplate, 2> lutTemplates{ curved, ParticleEmitterTemplate{} };
		auto lut = ParticleEffectCurveLut::bake(lutTemplates);
		if (!lut || lut->getWidth() != ParticleEffectCurveLut::SampleCount ||
			lut->getHeight() != ParticleEffectCurveLut::RowsPerTemplate * lutTemplates.size())
			return fail("particle effect LUT dimensions did not partition rows by emitter template");
		if (lut->getRowOffset(0) != 0u || lut->getRowOffset(1) != ParticleEffectCurveLut::RowsPerTemplate)
			return fail("particle effect LUT row offsets were allocated at runtime instead of baked by template order");
		auto const& texels = lut->getFloatTexels();
		auto sample = [&](uint32_t x, uint32_t row, uint32_t channel)
			{ return texels[(size_t(row) * ParticleEffectCurveLut::SampleCount + x) * 4u + channel]; };
		if (abs(sample(ParticleEffectCurveLut::SampleCount - 1u, 0u, 0u) - 2.0f) > 0.0001f ||
			abs(sample(ParticleEffectCurveLut::SampleCount - 1u, 1u, 1u) - 6.0f) > 0.0001f ||
			abs(sample(ParticleEffectCurveLut::SampleCount - 1u, 2u, 2u) - 3.0f) > 0.0001f)
			return fail("size, emissive, or HDR colour values did not survive the particle effect LUT bake");

		auto const randomOverLife = uint32_t(ParticleTextureAnimation::FrameOverLife | ParticleTextureAnimation::RandomStart);
		auto const randomFixedRate = uint32_t(ParticleTextureAnimation::FixedRate | ParticleTextureAnimation::RandomStart);
		if (particleFlipbookFrame(8u, uint32_t(ParticleTextureAnimation::FrameOverLife), 0.5f, 2.0f, 0.0f, 0u) != 2u ||
			particleFlipbookFrame(8u, uint32_t(ParticleTextureAnimation::FixedRate), 1.25f, 2.0f, 4.0f, 0u) != 5u ||
			particleFlipbookFrame(8u, randomOverLife, 0.5f, 2.0f, 0.0f, 11u) != 5u ||
			particleFlipbookFrame(8u, randomFixedRate, 1.25f, 2.0f, 4.0f, 11u) != 0u)
			return fail("flipbook playback or combinable random start selected the wrong frame");

		TrailSpecification trailSpecification;
		trailSpecification.maximumPointCount = 64u;
		trailSpecification.pointLifetime = 0.5f;
		trailSpecification.minimumPointDistance = 0.2f;
		trailSpecification.width = 2.0f;
		trailSpecification.uvScale = 3.0f;
		trailSpecification.widthOverLife.keys = { { 0.0f, 0.25f }, { 1.0f, 2.0f } };
		trailSpecification.colourOverLife.keys = {
			{ 0.0f, { 1.0f, 0.0f, 0.0f } }, { 1.0f, { 0.0f, 0.5f, 4.0f } }
		};
		auto trailRows = TrailSystem::bakeCurveRows(trailSpecification);
		if (trailRows.size() != size_t(TrailSystem::CurveSampleCount) * 2u * 4u ||
			abs(trailRows[0] - 0.25f) > 0.0001f ||
			abs(trailRows[(size_t(TrailSystem::CurveSampleCount) - 1u) * 4u] - 2.0f) > 0.0001f ||
			abs(trailRows.back() - 1.0f) > 0.0001f ||
			abs(trailRows[(size_t(TrailSystem::CurveSampleCount) * 2u - 1u) * 4u + 2u] - 4.0f) > 0.0001f)
			return fail("trail width-over-life or HDR colour-over-life did not bake into separate LUT rows");
		TrailSystem trails(nullptr, nullptr);
		auto oldTrail = trails.createTrail(trailSpecification, { 1.0f, 2.0f, 3.0f });
		if (!oldTrail || trails.mControls[oldTrail.index].lifetimeDistanceUvWidth !=
			array<float, 4>{ 0.5f, 0.2f, 3.0f, 2.0f })
			return fail("trail creation did not retain point lifetime, spacing, UV scale, and width");
		auto historyGeneration = trails.mControls[oldTrail.index].modes[2];
		trails.clearTrail(oldTrail);
		if (trails.mControls[oldTrail.index].modes[2] == historyGeneration)
			return fail("clearing a trail did not invalidate its GPU position history");
		trails.stopTrail(oldTrail);
		for (size_t frame = 0; frame < 4u; ++frame) trails.simulate(0.1f);
		if (!trails.isAlive(oldTrail)) return fail("a stopped trail retired before its points could fade");
		for (size_t frame = 0; frame < 2u; ++frame) trails.simulate(0.1f);
		if (trails.isAlive(oldTrail)) return fail("a stopped trail did not retire after its point lifetime");
		auto replacementTrail = trails.createTrail(trailSpecification);
		if (replacementTrail.index != oldTrail.index || replacementTrail.generation == oldTrail.generation)
			return fail("a reused trail slot did not advance its generational handle");
		trails.stopTrail(oldTrail);
		if (trails.mControls[replacementTrail.index].positionEnabled[3] == 0.0f)
			return fail("a stale trail handle retargeted its replacement");

		TemplateRenderData sortingAppearance;
		sortingAppearance.sorting[0] = uint32_t(ParticleSortMode::BackToFront);
		if (particleAppearanceRequiresDepthSort(sortingAppearance))
			return fail("an additive particle appearance entered the depth-sort path");
		sortingAppearance.modes[3] = uint32_t(ParticleBlendClass::WeightedOit);
		if (particleAppearanceRequiresDepthSort(sortingAppearance))
			return fail("a weighted OIT particle appearance entered the depth-sort path");
		sortingAppearance.modes[3] = uint32_t(ParticleBlendClass::Alpha);
		if (!particleAppearanceRequiresDepthSort(sortingAppearance))
			return fail("an opted-in alpha particle appearance did not require depth sorting");

		class TestParticleEffect final : public ParticleEffectSource
		{
			array<ParticleEmitterTemplate, 2> mTemplates;
			optional<ParticleEffectBounds> mBounds;
		public:
			explicit TestParticleEffect(array<ParticleEmitterTemplate, 2> templates,
				optional<ParticleEffectBounds> bounds = nullopt)
				: mTemplates(std::move(templates)), mBounds(std::move(bounds)) {}
			span<ParticleEmitterTemplate const> getEmitterTemplates() const override { return mTemplates; }
			optional<ParticleEffectBounds> getBounds() const override { return mBounds; }
		};
		ParticleSystem system(nullptr, nullptr);
		ParticleEffectBounds unitBounds{ { 0.0f, 0.0f, 0.0f }, { 2.0f, 2.0f, 2.0f } };
		auto const projection = glm::perspective(glm::radians(60.0f), 1.0f, 0.1f, 100.0f);
		auto const boundedTransform = glm::translate(glm::mat4(1.0f), { 0.0f, 0.0f, -5.0f });
		if (!particleEffectBoundsIntersectFrustum(unitBounds, boundedTransform, projection) ||
			particleEffectBoundsIntersectFrustum(unitBounds, boundedTransform,
				projection * glm::rotate(glm::mat4(1.0f), glm::radians(180.0f), { 0.0f, 1.0f, 0.0f })))
			return fail("particle effect bounds were not tested independently against distinct render views");
		auto transformedBounds = transformParticleEffectBounds(
			{ { 1.0f, 0.0f, 0.0f }, { 2.0f, 4.0f, 6.0f } },
			glm::translate(glm::mat4(1.0f), { 3.0f, 4.0f, 5.0f }) *
			glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), { 0.0f, 0.0f, 1.0f }));
		if (glm::distance(transformedBounds.center, glm::vec3(3.0f, 5.0f, 5.0f)) > 0.0001f ||
			glm::distance(transformedBounds.size, glm::vec3(4.0f, 2.0f, 6.0f)) > 0.0001f)
			return fail("rotated particle effect bounds were not transformed conservatively");

		auto rawEffect = system.createEffect(lutTemplates);
		if (system.mEffectSlots[rawEffect.index].bounds)
			return fail("raw emitter-span creation unexpectedly acquired particle effect bounds");
		auto liveEmitter = system.getEmitter(rawEffect, 0u);
		system.setEmitterTransform(liveEmitter, glm::translate(glm::mat4(1.0f), { 3.0f, 2.0f, 1.0f }));
		system.setEmitterParameter(liveEmitter, ParticleParameter::AlphaScale, 0.25f);
		system.mTemplateRenderData[liveEmitter.index].textureAndAtlas[0] = 7u;
		system.mTemplateRenderData[liveEmitter.index].appearance[3] = 8.0f;
		auto liveSimulation = lutTemplates[0].simulation;
		liveSimulation.shapeSeedModulesBudget[0] = uint32_t(ParticleSpawnShape::Cone);
		liveSimulation.emissionRateAndPadding[0] = 37.0f;
		auto liveAppearance = lutTemplates[0].appearance;
		liveAppearance.tintAndAlpha = { 0.2f, 0.3f, 0.4f, 0.5f };
		liveAppearance.modes[2] = uint32_t(ParticleBillboardMode::VelocityAligned);
		system.updateEmitterTemplateRuntime(liveEmitter, liveSimulation, liveAppearance);
		if (system.getEmitter(rawEffect, 0u) != liveEmitter ||
			system.mEmitters[liveEmitter.index].shapeSeedModulesBudget[0] != uint32_t(ParticleSpawnShape::Cone) ||
			system.mEmitters[liveEmitter.index].emissionRateAndPadding[0] != 37.0f ||
			system.mEmitters[liveEmitter.index].transform[12] != 3.0f ||
			system.mEmitters[liveEmitter.index].parameterMultipliers1[0] != 0.25f ||
			system.mTemplateRenderData[liveEmitter.index].tintAndAlpha != liveAppearance.tintAndAlpha ||
			system.mTemplateRenderData[liveEmitter.index].modes[2] != uint32_t(ParticleBillboardMode::VelocityAligned) ||
			system.mTemplateRenderData[liveEmitter.index].textureAndAtlas[0] != 7u ||
			system.mTemplateRenderData[liveEmitter.index].appearance[3] != 8.0f)
			return fail("safe emitter-template edits restarted the emitter or overwrote runtime-owned state");
		system.destroyEffect(rawEffect);
		auto boundedRawEffect = system.createEffect(lutTemplates, unitBounds,
			glm::translate(glm::mat4(1.0f), { 0.0f, 0.0f, -5.0f }));
		if (!system.mEffectSlots[boundedRawEffect.index].bounds ||
			!system.isEffectBoundsCullingEnabled(boundedRawEffect))
			return fail("explicit bounds were discarded or culling was not enabled by default");
		system.setEffectBoundsCullingEnabled(boundedRawEffect, false);
		if (system.isEffectBoundsCullingEnabled(boundedRawEffect) || system.mEffectSlots[boundedRawEffect.index].bounds == std::nullopt)
			return fail("the runtime bounds-culling override modified authored bounds or was not retained");
		system.destroyEffect(boundedRawEffect);
		TestParticleEffect boundedSource(lutTemplates, unitBounds);
		auto boundedSourceEffect = system.createEffect(boundedSource);
		if (!system.mEffectSlots[boundedSourceEffect.index].bounds)
			return fail("programmatic ParticleEffectSource bounds did not reach a live particle effect");
		system.destroyEffect(boundedSourceEffect);
		{
			Logger logger;
			ResourceManager resources(nullptr, &logger);
			ParticleEffectSpecification childSpecification;
			childSpecification.version = 2u;
			childSpecification.name = "Child";
			childSpecification.bounds = ParticleEffectBounds{ { 2.0f, 0.0f, 0.0f }, { 2.0f, 2.0f, 2.0f } };
			childSpecification.maximumParticleCount = 2u;
			ParticleEffectSpecification::EmitterTemplate childFirst;
			childFirst.name = "ChildFirst";
			childFirst.value = burst(0.25f);
			childFirst.value.simulation.shapeSeedModulesBudget[3] = 1u;
			childFirst.value.localTransform = glm::translate(glm::mat4(1.0f), { 2.0f, 0.0f, 0.0f });
			childFirst.value.simulation.shapeSeedModulesBudget[1] = 7u;
			childFirst.events = { { ParticleEventTrigger::Death,
				ParticleEventAction::SecondaryParticleBurst, "ChildSecond", 1u } };
			ParticleEffectSpecification::EmitterTemplate childSecond;
			childSecond.name = "ChildSecond";
			childSecond.value = burst(2.0f);
			childSecond.value.simulation.shapeSeedModulesBudget[3] = 1u;
			childSpecification.emitterTemplates = { childFirst, childSecond };
			auto childStream = make_shared<ProgrammaticParticleEffectStream>(&resources);
			childStream->setSpecification(childSpecification);
			resources.declareResource("Effects/Child", childStream);

			ParticleEffectSpecification parentSpecification;
			parentSpecification.version = 2u;
			parentSpecification.name = "Parent";
			parentSpecification.bounds = unitBounds;
			parentSpecification.maximumParticleCount = 1u;
			ParticleEffectSpecification::EmitterTemplate parentEmitter;
			parentEmitter.name = "ParentEmitter";
			parentEmitter.value = burst(0.1f);
			parentEmitter.value.simulation.shapeSeedModulesBudget[3] = 1u;
			parentSpecification.emitterTemplates.push_back(parentEmitter);
			ParticleEffectSpecification::ChildEffect firstChild;
			firstChild.effect = "Effects/Child";
			firstChild.transform = glm::translate(glm::mat4(1.0f), { 0.0f, 3.0f, 0.0f });
			firstChild.seed = 11u;
			auto secondChild = firstChild;
			secondChild.transform = glm::translate(glm::mat4(1.0f), { 0.0f, 0.0f, 4.0f });
			secondChild.seed = 12u;
			parentSpecification.childEffects = { firstChild, secondChild };
			auto parentStream = make_shared<ProgrammaticParticleEffectStream>(&resources);
			parentStream->setSpecification(parentSpecification);
			auto parentAsset = resources.declareResource("Effects/Parent", parentStream).first;
			parentAsset->create();
			auto parent = dynamic_pointer_cast<ParticleEffect>(parentAsset);
			auto flattened = parent->getEmitterTemplates();
			auto aggregateBounds = parent->getBounds();
			if (!aggregateBounds || glm::distance(aggregateBounds->center, glm::vec3(1.0f, 1.5f, 2.0f)) > 0.0001f ||
				glm::distance(aggregateBounds->size, glm::vec3(4.0f, 5.0f, 6.0f)) > 0.0001f)
				return fail("transformed child particle effect bounds were not conservatively aggregated");
			if (flattened.size() != 5u || flattened[1].localTransform[3][0] != 2.0f ||
				flattened[1].localTransform[3][1] != 3.0f || flattened[3].localTransform[3][2] != 4.0f)
				return fail("child particle effect transforms were not flattened relative to their parent");
			if (flattened[1].simulation.shapeSeedModulesBudget[1] == flattened[3].simulation.shapeSeedModulesBudget[1] ||
				flattened[1].events[0].targetEmitterTemplate != 2u || flattened[3].events[0].targetEmitterTemplate != 4u)
				return fail("child seeds or intra-child event targets were not derived independently");
			ParticleSystem composedSystem(nullptr, nullptr);
			auto composedEffect = composedSystem.createEffect(parentAsset);
			if (!composedEffect || !composedSystem.getEmitter(composedEffect, 4u))
				return fail("flattened child particle effects did not create one live group");
			step(composedSystem, 0.01f);
			step(composedSystem, 0.5f);
			if (!composedSystem.isAlive(composedEffect) || !composedSystem.getEmitter(composedEffect, 2u) ||
				composedSystem.getEmitter(composedEffect, 0u))
				return fail("child particle effect group did not live as long as its longest descendant");
			step(composedSystem, 20.0f);
			if (composedSystem.isAlive(composedEffect))
				return fail("child particle effect group did not retire with its descendants");

			ParticleEffectSpecification unboundedRoot = parentSpecification;
			unboundedRoot.name = "UnboundedRoot";
			unboundedRoot.bounds.reset();
			auto unboundedRootStream = make_shared<ProgrammaticParticleEffectStream>(&resources);
			unboundedRootStream->setSpecification(unboundedRoot);
			auto unboundedRootAsset = resources.declareResource("Effects/UnboundedRoot", unboundedRootStream).first;
			unboundedRootAsset->create();
			if (dynamic_pointer_cast<ParticleEffect>(unboundedRootAsset)->getBounds())
				return fail("a missing root bound did not make the aggregate particle effect unbounded");

			ParticleEffectSpecification unboundedChild = childSpecification;
			unboundedChild.name = "UnboundedChild";
			unboundedChild.bounds.reset();
			auto unboundedChildStream = make_shared<ProgrammaticParticleEffectStream>(&resources);
			unboundedChildStream->setSpecification(unboundedChild);
			resources.declareResource("Effects/UnboundedChild", unboundedChildStream);
			ParticleEffectSpecification incompletelyBoundedParent = parentSpecification;
			incompletelyBoundedParent.name = "IncompletelyBoundedParent";
			incompletelyBoundedParent.childEffects = { { "Effects/UnboundedChild" } };
			auto incompletelyBoundedStream = make_shared<ProgrammaticParticleEffectStream>(&resources);
			incompletelyBoundedStream->setSpecification(incompletelyBoundedParent);
			auto incompletelyBoundedAsset = resources.declareResource(
				"Effects/IncompletelyBoundedParent", incompletelyBoundedStream).first;
			incompletelyBoundedAsset->create();
			if (dynamic_pointer_cast<ParticleEffect>(incompletelyBoundedAsset)->getBounds())
				return fail("an unbounded child branch did not make the aggregate particle effect unbounded");

			ParticleEffectSpecification cycleA, cycleB;
			cycleA.name = "CycleA"; cycleA.childEffects.push_back({ "Effects/CycleB" });
			cycleB.name = "CycleB"; cycleB.childEffects.push_back({ "Effects/CycleA" });
			auto cycleAStream = make_shared<ProgrammaticParticleEffectStream>(&resources);
			auto cycleBStream = make_shared<ProgrammaticParticleEffectStream>(&resources);
			cycleAStream->setSpecification(cycleA); cycleBStream->setSpecification(cycleB);
			auto cycleAAsset = resources.declareResource("Effects/CycleA", cycleAStream).first;
			resources.declareResource("Effects/CycleB", cycleBStream);
			bool rejectedChildCycle = false;
			try { cycleAAsset->create(); }
			catch (invalid_argument const&) { rejectedChildCycle = true; }
			if (!rejectedChildCycle) return fail("cyclic child particle effects were accepted");
		}
		ParticleCollider plane;
		plane.shapeAndPadding[0] = uint32_t(ParticleColliderShape::Plane);
		plane.first = { 0.0f, 1.0f, 0.0f, 0.0f };
		ParticleCollider capsule;
		capsule.shapeAndPadding[0] = uint32_t(ParticleColliderShape::Capsule);
		capsule.first = { -1.0f, 0.0f, 0.0f, 0.5f };
		capsule.second = { 1.0f, 0.0f, 0.0f, 0.0f };
		array collisionWorld{ plane, capsule };
		system.setColliders(collisionWorld);
		if (system.getColliders().size() != 2u || system.getColliders()[1].second[0] != 1.0f)
			return fail("analytical collider world did not retain plane and capsule records");
		system.setColliders({});
		if (!system.getColliders().empty()) return fail("analytical collider world did not clear");
		if (uint32_t(ParticleFlag::Colliding) != 1u || uint32_t(ParticleFlag::CollisionEvent) != 2u ||
			uint32_t(ParticleFlag::SpawnSecondaryEffect) != 4u)
			return fail("particle collision-state flag bits overlap or changed");

		system.setEventCallback(ParticleEventAction::Audio, [](ParticleEvent const&) {});
		if (!system.hasEventCallback(ParticleEventAction::Audio))
			return fail("particle external event callback did not register");
		system.clearEventCallback(ParticleEventAction::Audio);
		if (system.hasEventCallback(ParticleEventAction::Audio))
			return fail("particle external event callback did not clear");
		bool rejectedGpuCallback = false;
		try { system.setEventCallback(ParticleEventAction::SecondaryParticleBurst, [](ParticleEvent const&) {}); }
		catch (invalid_argument const&) { rejectedGpuCallback = true; }
		if (!rejectedGpuCallback) return fail("GPU secondary particle bursts accepted a CPU callback");

		array<ParticleEmitterTemplate, 2> eventTemplates{ burst(1.0f), burst(2.0f) };
		eventTemplates[0].events = {
			{ ParticleEventTrigger::Death, ParticleEventAction::SecondaryParticleBurst, 1u, 4u, 0.0f, 7u },
			{ ParticleEventTrigger::Age, ParticleEventAction::GameplayCallback, 0u, 1u, 0.5f, 99u }
		};
		auto eventEffect = system.createEffect(eventTemplates);
		auto eventSource = system.getEmitter(eventEffect, 0u);
		auto eventTarget = system.getEmitter(eventEffect, 1u);
		if (system.mEmitterEventRules[eventSource.index].size() != 2u ||
			system.mEmitterEventRules[eventSource.index][0].targetEmitterTemplate != eventTarget.index ||
			system.mEmitterEventRules[eventSource.index][0].targetEmitterGeneration != eventTarget.generation ||
			!system.mEmitterSlots[eventTarget.index].eventTarget)
			return fail("particle event rules did not remap an asset target to its live emitter");
		system.mEventRulesDirty = false;
		system.destroyEmitter(eventTarget);
		if (!system.mEventRulesDirty ||
			system.mEmitterEventRules[eventSource.index][0].targetEmitterGeneration ==
				system.mEmitterSlots[eventTarget.index].generation ||
			system.mEmitterSlots[eventTarget.index].eventGeneration != eventTarget.generation)
			return fail("destroying an event target did not invalidate queued work before slot reuse");
		system.destroyEffect(eventEffect);
		step(system, 20.0f);

		auto continuousSource = burst(1.0f);
		continuousSource.simulation.emissionState = { 0u, 1u, 0u, 0u };
		continuousSource.events = { { ParticleEventTrigger::Spawn,
			ParticleEventAction::SecondaryParticleBurst, 1u, 1u } };
		auto dormantTarget = burst(1.0f);
		dormantTarget.simulation.emissionState[1] = 0u;
		array continuousEventTemplates{ continuousSource, dormantTarget };
		auto continuousEventEffect = system.createEffect(continuousEventTemplates);
		auto persistentTarget = system.getEmitter(continuousEventEffect, 1u);
		step(system, 20.0f);
		if (!system.isAlive(persistentTarget))
			return fail("a live continuous event source retired its secondary target");
		system.destroyEffect(continuousEventEffect);
		step(system, 20.0f);

		auto cyclic = burst(1.0f);
		cyclic.events = { { ParticleEventTrigger::Spawn, ParticleEventAction::SecondaryParticleBurst, 0u, 1u } };
		bool rejectedCycle = false;
		try { array cycleTemplates{ cyclic }; (void)system.createEffect(cycleTemplates); }
		catch (invalid_argument const&) { rejectedCycle = true; }
		if (!rejectedCycle) return fail("cyclic secondary particle bursts were accepted");

		weak_ptr<ParticleEffectCurveLut> assetLut;
		ParticleEffectHandle curvedEffect;
		{
			TestParticleEffect source(lutTemplates);
			if (source.getCurveLut() != source.getCurveLut())
				return fail("a particle effect asset baked more than one LUT");
			assetLut = source.getCurveLut();
			curvedEffect = system.createEffect(source);
		}
		if (assetLut.expired()) return fail("curve LUT died while its particle effect instance was alive");
		auto curvedEmitter = system.getEmitter(curvedEffect, 1);
		if (system.mTemplateRenderData[curvedEmitter.index].appearance[3] !=
			float(ParticleEffectCurveLut::RowsPerTemplate))
			return fail("baked LUT row offset did not reach TemplateRenderData");
		system.destroyEffect(curvedEffect);
		if (!assetLut.expired()) return fail("curve LUT outlived both its asset and particle effect instance");

		// Parent x local transform composition and per-emitter addressing.
		array<ParticleEmitterTemplate, 2> transforms{ burst(1.0f), burst(1.0f) };
		transforms[0].lighting.colourAndIntensity = { 1.0f, 0.25f, 0.1f, 4.0f };
		transforms[0].lighting.rangeAndVolumetric = { 6.0f, 0.5f, 0.0f, 0.0f };
		transforms[0].lighting.flagsAndPadding[0] = uint32_t(ParticleLightingFlag::ProxyLight |
			ParticleLightingFlag::PbrLightInjection | ParticleLightingFlag::VolumetricContribution);
		transforms[1].lighting.rangeAndVolumetric[0] = 3.0f;
		transforms[1].lighting.flagsAndPadding[0] = uint32_t(ParticleLightingFlag::ProxyLight);
		transforms[0].localTransform = glm::translate(glm::mat4(1.0f), { 2.0f, 0.0f, 0.0f });
		transforms[1].localTransform = glm::translate(glm::mat4(1.0f), { 0.0f, 3.0f, 0.0f });
		auto effect = system.createEffect(transforms, glm::translate(glm::mat4(1.0f), { 10.0f, 20.0f, 0.0f }));
		auto first = system.getEmitter(effect, 0);
		auto second = system.getEmitter(effect, 1);
		if (!first || !second || first.index == second.index) return fail("effect emitter span was not created");
		if (system.mEmitters[first.index].transform[12] != 12.0f || system.mEmitters[first.index].transform[13] != 20.0f ||
			system.mEmitters[second.index].transform[12] != 10.0f || system.mEmitters[second.index].transform[13] != 23.0f)
			return fail("effect parent and emitter-local transforms were not composed");
		auto proxyLights = system.getProxyLights();
		auto injectedLights = system.getProxyLights(ParticleSystem::MaxEmitterCount, true);
		if (proxyLights.size() != 2u || injectedLights.size() != 1u ||
			injectedLights[0].emitter != first || injectedLights[0].light.type != PbrLightType::Point ||
			injectedLights[0].light.position != glm::vec3(12.0f, 20.0f, 0.0f) ||
			injectedLights[0].light.range != 6.0f || injectedLights[0].light.intensity != 4.0f)
			return fail("emitter-level proxy lights were not transformed, filtered, or bounded independently of particles");
		system.setEmitterParameter(first, ParticleParameter::EmissiveScale, 0.5f);
		if (system.getProxyLights(1u, true)[0].light.intensity != 2.0f)
			return fail("emitter emissive scaling did not reach its proxy light");
		system.setEffectVisible(effect, false);
		if (system.mEmitters[first.index].emissionRateAndPadding[1] != 0.0f ||
			system.mEmitters[second.index].emissionRateAndPadding[1] != 0.0f || !system.getProxyLights().empty())
			return fail("effect visibility did not reach every emitter and proxy light");
		system.setEffectVisibilityFlags(effect, uint32_t(ParticleEffectVisibilityFlag::Visible));
		if (system.mEmitters[first.index].emissionRateAndPadding[1] != 1.0f ||
			system.mEmitters[first.index].emissionState[1] == 0u)
			return fail("effect visibility flags changed emitter simulation state");
		// The runtime parameter surface is deliberately closed. Each value is a
		// multiplier on authored data and belongs solely to the addressed Emitter.
		system.setEmitterParameter(first, ParticleParameter::SpawnRate, 2.5f);
		system.setEmitterParameter(first, ParticleParameter::SizeScale, 3.5f);
		system.setEmitterParameter(first, ParticleParameter::SpeedScale, 4.5f);
		system.setEmitterParameter(first, ParticleParameter::LifetimeScale, 5.5f);
		system.setEmitterParameter(first, ParticleParameter::AlphaScale, 6.5f);
		system.setEmitterParameter(first, ParticleParameter::EmissiveScale, 7.5f);
		system.stopEmitter(first);
		system.startEmitter(first);
		if (system.mEmitters[first.index].parameterMultipliers0 != array<float, 4>{ 2.5f, 3.5f, 4.5f, 5.5f } ||
			system.mEmitters[first.index].parameterMultipliers1[0] != 6.5f ||
			system.mEmitters[first.index].parameterMultipliers1[1] != 7.5f)
			return fail("runtime particle parameter multipliers were not mapped to their authored values");
		auto const authoredEmission = system.mEmitters[first.index].emissionState;
		system.requestEmitterBurst(first, 3u);
		system.requestEmitterBurst(first, 4u);
		if (system.mSpawnCommands.size() != 1u || system.mSpawnCommands[0].emitterIndex != first.index ||
			system.mSpawnCommands[0].count != 7u || system.mEmitters[first.index].emissionState != authoredEmission)
			return fail("runtime manual burst changed authored emitter state or did not coalesce deterministically");
		auto const queuedBurstCount = system.mSpawnCommands.size();
		system.requestEmitterBurst({}, 5u);
		system.requestEmitterBurst(first, 0u);
		if (system.mSpawnCommands.size() != queuedBurstCount)
			return fail("runtime manual burst accepted a stale emitter or zero count");
		system.mSpawnCommands.clear();
		system.setEmitterParameter(first, ParticleParameter::AlphaScale, -1.0f);
		if (system.mEmitters[first.index].parameterMultipliers1[0] != 0.0f)
			return fail("a negative runtime particle parameter multiplier was not clamped");
		if (system.mEmitters[second.index].parameterMultipliers0[0] != 1.0f ||
			system.mEmitters[second.index].parameterMultipliers1 != array<float, 4>{ 1.0f, 1.0f, 0.0f, 0.0f })
			return fail("a per-emitter operation changed another emitter");
		system.destroyEffect(effect);

		auto invalidLighting = burst(1.0f);
		invalidLighting.lighting.flagsAndPadding[0] = uint32_t(ParticleLightingFlag::PbrLightInjection);
		bool rejectedUnboundedInjection = false;
		try { array invalidTemplates{ invalidLighting }; (void)system.createEffect(invalidTemplates); }
		catch (invalid_argument const&) { rejectedUnboundedInjection = true; }
		if (!rejectedUnboundedInjection)
			return fail("particle light injection without an emitter-level proxy was accepted");

		// A reclaimed index must get a different generation, leaving stale handles inert.
		array<ParticleEmitterTemplate, 1> one{ burst(0.0f) };
		auto oldEffect = system.createEffect(one);
		auto stale = system.getEmitter(oldEffect, 0);
		step(system, 0.01f);
		if (system.isAlive(stale) || system.isAlive(oldEffect)) return fail("completed burst did not retire");
		auto replacementEffect = system.createEffect(one);
		auto replacement = system.getEmitter(replacementEffect, 0);
		if (replacement.index != stale.index || replacement.generation == stale.generation)
			return fail("reused emitter slot did not advance its generation");
		system.stopEmitter(stale);
		if (system.mEmitters[replacement.index].emissionState[1] == 0u)
			return fail("stale handle retargeted a replacement emitter");
		step(system, 0.01f);

		// Fire-and-forget bursts reclaim both emitter and effect slots without readback.
		for (size_t index = 0; index < 10000; ++index)
		{
			system.spawnEffect(one);
			step(system, 0.001f);
		}
		if (system.getLiveEmitterCount() != 0 || system.getLiveEffectCount() != 0 || system.mEmitterSlots.size() > 2)
			return fail("ten thousand fire-and-forget bursts leaked CPU slots");

		// An effect remains alive until its longest emitter has exhausted its bound.
		array<ParticleEmitterTemplate, 2> mixed{ burst(0.1f), burst(2.0f) };
		auto mixedEffect = system.createEffect(mixed);
		step(system, 0.01f); // submit both bursts
		step(system, 0.2f);
		if (system.isAlive(system.getEmitter(mixedEffect, 0))) return fail("short burst outlived its authored maximum lifetime");
		if (!system.isAlive(system.getEmitter(mixedEffect, 1)) || !system.isAlive(mixedEffect))
			return fail("effect retired before its long plume");
		auto unrelatedEffect = system.createEffect(one, glm::translate(glm::mat4(1.0f), { 7.0f, 0.0f, 0.0f }));
		auto unrelated = system.getEmitter(unrelatedEffect, 0);
		system.setEffectTransform(mixedEffect, glm::translate(glm::mat4(1.0f), { 99.0f, 0.0f, 0.0f }));
		if (system.mEmitters[unrelated.index].transform[12] != 7.0f)
			return fail("effect emitter span retargeted a reused slot");
		step(system, 2.0f);
		if (system.isAlive(mixedEffect)) return fail("effect did not retire with its long plume");

		return true;
	}
}
