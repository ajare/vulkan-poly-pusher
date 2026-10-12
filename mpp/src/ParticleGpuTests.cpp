#include <GL/glew.h>

#include <array>
#include <cstdint>
#include <cmath>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include <glm/gtc/matrix_transform.hpp>

#include "mpp/BoxModelStream.h"
#include "mpp/DefaultShaders.h"
#include "mpp/GLErrorCheck.h"
#include "mpp/Model.h"
#include "mpp/ParticleCurveLut.h"
#include "mpp/ParticleData.h"
#include "mpp/ParticleGpuTests.h"
#include "mpp/ParticleSystem.h"
#include "mpp/Program.h"
#include "mpp/ProgrammaticBasicMaterialStream.h"
#include "mpp/ProgrammaticProgramStream.h"
#include "mpp/ProgrammaticTextureStream.h"
#include "mpp/RawShaderProgram.h"
#include "mpp/RenderSystem.h"
#include "mpp/ResourceManager.h"
#include "mpp/ShaderStorageBuffer.h"
#include "mpp/TrailSystem.h"

namespace mpp
{
	namespace
	{
		struct CounterSnapshot
		{
			ParticleCounterHeader counters{};
			uint32_t activeListIndex{ 0 };
		};

		// Test-only version of the statistics readback contract: copies are queued
		// into staging buffers and fenced. read() only consumes an already-signalled
		// result, so none of the assertions reads the live counter SSBO or waits for
		// a result from the frame that produced it.
		class LaggedCounterReadback
		{
			struct Slot
			{
				GLuint buffer{ 0 };
				GLsync fence{ nullptr };
				uint32_t activeListIndex{ 0 };
			};
			std::vector<Slot> mSlots;

		public:
			~LaggedCounterReadback()
			{
				for (auto& slot : mSlots)
				{
					if (slot.fence) glDeleteSync(slot.fence);
					if (slot.buffer) glDeleteBuffers(1, &slot.buffer);
				}
			}

			size_t enqueue(GLuint source, uint32_t activeListIndex)
			{
				Slot slot;
				slot.activeListIndex = activeListIndex;
				GL_CHECK(glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT));
				GL_CHECK(glGenBuffers(1, &slot.buffer));
				GL_CHECK(glBindBuffer(GL_COPY_WRITE_BUFFER, slot.buffer));
				GL_CHECK(glBufferData(GL_COPY_WRITE_BUFFER, sizeof(ParticleCounterHeader), nullptr, GL_STREAM_READ));
				GL_CHECK(glBindBuffer(GL_COPY_READ_BUFFER, source));
				GL_CHECK(glCopyBufferSubData(GL_COPY_READ_BUFFER, GL_COPY_WRITE_BUFFER, 0, 0, sizeof(ParticleCounterHeader)));
				slot.fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
				GL_CHECK(glBindBuffer(GL_COPY_READ_BUFFER, 0));
				GL_CHECK(glBindBuffer(GL_COPY_WRITE_BUFFER, 0));
				mSlots.push_back(slot);
				return mSlots.size() - 1u;
			}

			bool read(size_t index, CounterSnapshot& snapshot) const
			{
				if (index >= mSlots.size()) return false;
				auto const& slot = mSlots[index];
				auto const status = glClientWaitSync(slot.fence, 0, 0);
				if (status != GL_ALREADY_SIGNALED && status != GL_CONDITION_SATISFIED) return false;
				GL_CHECK(glBindBuffer(GL_COPY_READ_BUFFER, slot.buffer));
				GL_CHECK(glGetBufferSubData(GL_COPY_READ_BUFFER, 0, sizeof(snapshot.counters), &snapshot.counters));
				GL_CHECK(glBindBuffer(GL_COPY_READ_BUFFER, 0));
				snapshot.activeListIndex = slot.activeListIndex;
				return true;
			}
		};

		ParticleEmitterTemplate burst(uint32_t count, uint32_t budget, float lifetime)
		{
			ParticleEmitterTemplate emitter;
			emitter.simulation.emissionState = { 1u, 1u, count, 0u };
			emitter.simulation.shapeSeedModulesBudget[3] = budget;
			emitter.simulation.lifetimeSizeRanges[0] = lifetime;
			emitter.simulation.lifetimeSizeRanges[1] = lifetime;
			return emitter;
		}

		uint32_t activeCount(CounterSnapshot const& snapshot)
		{
			return snapshot.activeListIndex == 0u ? snapshot.counters.activeCountA : snapshot.counters.activeCountB;
		}

		ParticleEmitterTemplate cullingBurst(glm::vec3 position, float size = 1.0f)
		{
			auto emitter = burst(1u, 1u, 10.0f);
			emitter.localTransform = glm::translate(glm::mat4(1.0f), position);
			emitter.simulation.lifetimeSizeRanges[2] = size;
			emitter.simulation.lifetimeSizeRanges[3] = size;
			return emitter;
		}
	}

	bool runParticleGpuTests(RenderSystem* renderSystem, std::string* failure)
	{
		auto fail = [failure](std::string const& message)
		{
			if (failure) *failure = message;
			return false;
		};
		if (!renderSystem) return fail("RenderSystem is null");

		std::string stage = "particle system initialization";
		try
		{
			stage = "RGBA16F particle effect curve LUT";
			ParticleEmitterTemplate hdrTemplate;
			hdrTemplate.curves[size_t(ParticleScalarCurve::EmissiveIntensity)].keys = {
				{ 0.0f, 1.0f }, { 1.0f, 8.5f }
			};
			std::array hdrTemplates{ hdrTemplate };
			auto hdrLut = ParticleEffectCurveLut::bake(hdrTemplates);
			hdrLut->bind(1u);
			GLint internalFormat = 0;
			GL_CHECK(glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_INTERNAL_FORMAT, &internalFormat));
			if (internalFormat != GL_RGBA16F) return fail("particle effect LUT was not allocated as RGBA16F");
			std::vector<float> lutReadback(size_t(hdrLut->getWidth()) * hdrLut->getHeight() * 4u);
			GL_CHECK(glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_FLOAT, lutReadback.data()));
			size_t const emissiveEndpoint =
				(size_t(ParticleEffectCurveLut::SampleCount) + ParticleEffectCurveLut::SampleCount - 1u) * 4u + 1u;
			if (std::abs(lutReadback[emissiveEndpoint] - 8.5f) > 0.01f)
				return fail("emissive intensity above one did not survive the RGBA16F bake and upload");
			GL_CHECK(glBindTexture(GL_TEXTURE_2D, 0));
			hdrLut->unload();

			LaggedCounterReadback readback;
			struct PendingSnapshot { size_t index; std::string name; };
			std::vector<PendingSnapshot> pending;

			auto initialise = [&](ParticleSystem& system)
			{
				system.initialise();
				if (!system.mAvailable) return false;
				return true;
			};
			auto runFrame = [&](ParticleSystem& system, float dt)
			{
				system.pollEventReadback();
				system.advanceStatisticsFrame();
				system.mSimulationSeconds += dt;
				system.buildSpawnCommands(dt);
				system.ensurePoolAllocated();
				system.uploadFrameData();
				system.dispatchStatisticsPrepare();
				system.dispatchEventPrepare(0u);
				system.beginStatisticsSample();
				system.dispatchSpawnCommands();
				system.dispatchSimulation(dt);
				system.dispatchParticleEvents();
				system.dispatchCompaction();
				system.finishSimulationTiming();
				system.dispatchDepthSorts();
				system.retireCompletedEmitters();
				system.finishStatisticsSample();
			};
			auto queue = [&](ParticleSystem& system, std::string name)
			{
				pending.push_back({ readback.enqueue(system.mCounters->getBuffer(), system.mActiveListIndex), std::move(name) });
			};

			ParticleSystem& system = renderSystem->getParticleSystem();
			if (!initialise(system)) return fail("particle GPU path is unavailable");
			if (system.isStatisticsEnabled() || system.getStats().valid)
				return fail("particle statistics were not off by default");
			if (system.hasOccupiedEmitters() || system.mPoolAllocated)
				return fail("particle GPU tests require a fresh particle pool");

			stage = "particle simulation time controls and frame guard";
			system.pauseSimulation();
			float const controlStart = system.mSimulationSeconds;
			system.requestSimulationStep(0.025f);
			renderSystem->startStatsCollection();
			renderSystem->simulateParticles();
			if (std::abs(system.mSimulationSeconds - (controlStart + 0.025f)) > 0.000001f ||
				!system.isSimulationPaused())
				return fail("deterministic particle step did not advance once and remain paused");
			system.requestSimulationStep(0.05f);
			renderSystem->simulateParticles();
			if (std::abs(system.mSimulationSeconds - (controlStart + 0.025f)) > 0.000001f)
				return fail("particle simulation dispatched twice in one rendered frame");
			renderSystem->startStatsCollection();
			renderSystem->simulateParticles();
			if (std::abs(system.mSimulationSeconds - (controlStart + 0.075f)) > 0.000001f)
				return fail("a frame-guarded particle step was not retained for the next rendered frame");
			renderSystem->startStatsCollection();
			renderSystem->simulateParticles();
			if (std::abs(system.mSimulationSeconds - (controlStart + 0.075f)) > 0.000001f)
				return fail("paused particle simulation advanced without a requested step");
			system.setSimulationTimeScale(1.0f);
			system.resumeSimulation();

			system.setStatisticsEnabled(true);

			stage = "spawn and expiry counters";
			std::array spawnEmitters{ burst(37u, 37u, 0.05f) };
			system.createEffect(spawnEmitters);
			runFrame(system, 0.0f);
			queue(system, "spawn");
			runFrame(system, 0.1f);
			queue(system, "expiry");

			stage = "over-spawn clamp";
			auto const capacity = renderSystem->getOptions().particlePoolCapacity;
			std::array overSpawnEmitters{ burst(capacity + 17u, capacity + 17u, 0.05f) };
			system.createEffect(overSpawnEmitters);
			runFrame(system, 0.0f);
			queue(system, "over-spawn");
			// Empty the pool before checking a small survivor list.
			runFrame(system, 0.1f);

			stage = "double-buffered active lists";
			std::array swapEmitters{ burst(23u, 23u, 10.0f) };
			system.createEffect(swapEmitters);
			runFrame(system, 0.0f);
			queue(system, "active-list frame one");
			runFrame(system, 0.01f);
			queue(system, "active-list frame two");
			// Leave the renderer-owned particle system empty for the graph GPU suite
			// that runs next under the same flag.
			runFrame(system, 10.0f);

			// Complete all queued test work before polling. The readback itself still
			// has a zero timeout and will fail rather than block on an unsignalled slot.
			GL_CHECK(glFinish());
			system.advanceStatisticsFrame();
			auto const stats = system.getStats();
			if (!stats.valid || stats.framesLagged < 2u || stats.activeParticles != 23u ||
				stats.freeParticles != capacity - 23u || stats.spawnedParticles != 0u ||
				stats.killedParticles != 0u || stats.droppedParticles != 0u ||
				stats.renderedParticles != 23u || stats.culledParticles != 0u ||
				stats.activeEmitters != 1u || stats.capacity != capacity || stats.capacityUsage <= 0.0f ||
				stats.simulationGpuMilliseconds < 0.0 || stats.sortingGpuMilliseconds != 0.0 || stats.renderGpuMilliseconds != 0.0)
				return fail("public ParticleStats did not publish the complete frame-lagged GPU snapshot");
			system.setStatisticsEnabled(false);

			std::vector<CounterSnapshot> snapshots(pending.size());
			for (size_t index = 0; index < pending.size(); ++index)
				if (!readback.read(pending[index].index, snapshots[index]))
					return fail(pending[index].name + " frame-lagged counter snapshot was not ready");

			if (activeCount(snapshots[0]) != 37u) return fail("spawned active count was not N");
			if (snapshots[1].counters.freeCount != capacity || activeCount(snapshots[1]) != 0u)
				return fail("aged particles did not return every slot to the free list");
			if (activeCount(snapshots[2]) != capacity || snapshots[2].counters.droppedSpawnCount < 17u)
				return fail("over-spawn did not clamp to capacity and increment the dropped counter");
			if (snapshots[3].activeListIndex != 1u || snapshots[3].counters.activeCountB != 23u ||
				snapshots[4].activeListIndex != 0u || snapshots[4].counters.activeCountA != 23u)
				return fail("double-buffered active particle lists did not swap across two frames (frame one index/A/B " +
					std::to_string(snapshots[3].activeListIndex) + "/" + std::to_string(snapshots[3].counters.activeCountA) + "/" +
					std::to_string(snapshots[3].counters.activeCountB) + ", frame two " + std::to_string(snapshots[4].activeListIndex) + "/" +
					std::to_string(snapshots[4].counters.activeCountA) + "/" + std::to_string(snapshots[4].counters.activeCountB) + ")");

			stage = "paused GPU age and emission";
			system.pauseSimulation();
			std::array pausedEmitter{ burst(1u, 1u, 0.05f) };
			system.createEffect(pausedEmitter);
			renderSystem->startStatsCollection();
			renderSystem->simulateParticles();
			auto pausedBeforeStepIndex = readback.enqueue(system.mCounters->getBuffer(), system.mActiveListIndex);
			GL_CHECK(glFinish());
			CounterSnapshot pausedBeforeStep;
			if (!readback.read(pausedBeforeStepIndex, pausedBeforeStep) || activeCount(pausedBeforeStep) != 0u)
				return fail("paused particle simulation emitted a burst");
			system.requestSimulationStep(0.025f);
			renderSystem->startStatsCollection();
			renderSystem->simulateParticles();
			auto steppedIndex = readback.enqueue(system.mCounters->getBuffer(), system.mActiveListIndex);
			GL_CHECK(glFinish());
			CounterSnapshot stepped;
			if (!readback.read(steppedIndex, stepped) || activeCount(stepped) != 1u)
				return fail("a deterministic particle step did not emit on the GPU");
			uint32_t particleIndex = 0u;
			auto* activeIndices = system.mActiveListIndex == 0u ? system.mActiveIndicesA.get() : system.mActiveIndicesB.get();
			GL_CHECK(glBindBuffer(GL_COPY_READ_BUFFER, activeIndices->getBuffer()));
			GL_CHECK(glGetBufferSubData(GL_COPY_READ_BUFFER, 0, sizeof(particleIndex), &particleIndex));
			ParticleRecord steppedParticle{};
			GL_CHECK(glBindBuffer(GL_COPY_READ_BUFFER, system.mParticlePool->getBuffer()));
			GL_CHECK(glGetBufferSubData(GL_COPY_READ_BUFFER, GLintptr(particleIndex * sizeof(ParticleRecord)),
				sizeof(steppedParticle), &steppedParticle));
			renderSystem->startStatsCollection();
			renderSystem->simulateParticles();
			ParticleRecord pausedParticle{};
			GL_CHECK(glGetBufferSubData(GL_COPY_READ_BUFFER, GLintptr(particleIndex * sizeof(ParticleRecord)),
				sizeof(pausedParticle), &pausedParticle));
			GL_CHECK(glBindBuffer(GL_COPY_READ_BUFFER, 0));
			if (pausedParticle.positionAge[3] != steppedParticle.positionAge[3])
				return fail("paused particle simulation advanced GPU particle age");
			system.requestSimulationStep(0.05f);
			renderSystem->startStatsCollection();
			renderSystem->simulateParticles();
			system.resumeSimulation();

			stage = "GPU visibility compaction";
			renderSystem->setCameraFrame(glm::mat4(1.0f),
				glm::perspective(glm::radians(60.0f), 4.0f / 3.0f, 0.1f, 100.0f),
				{ 800.0f, 600.0f }, 0.1f, 100.0f, 0.0f);
			auto visible = cullingBurst({ 0.0f, 0.0f, -5.0f });
			auto outsideFrustum = cullingBurst({ 100.0f, 0.0f, -5.0f });
			auto beyondDistance = cullingBurst({ 0.0f, 0.0f, -50.0f });
			beyondDistance.appearance.culling[0] = 10.0f;
			auto belowProjectedSize = cullingBurst({ 0.0f, 0.0f, -20.0f }, 0.001f);
			belowProjectedSize.appearance.culling[1] = 2.0f;
			auto hidden = cullingBurst({ 0.0f, 0.0f, -5.0f });
			std::array<ParticleEmitterTemplate, 1> oneVisible{ visible }, oneFrustum{ outsideFrustum },
				oneDistance{ beyondDistance }, oneProjected{ belowProjectedSize }, oneHidden{ hidden };
			system.createEffect(oneVisible);
			system.createEffect(oneFrustum);
			system.createEffect(oneDistance);
			system.createEffect(oneProjected);
			auto hiddenEffect = system.createEffect(oneHidden);
			system.setEffectVisible(hiddenEffect, false);
			runFrame(system, 0.0f);
			auto cullingSnapshotIndex = readback.enqueue(system.mCounters->getBuffer(), system.mActiveListIndex);
			GL_CHECK(glFinish());
			CounterSnapshot cullingSnapshot;
			if (!readback.read(cullingSnapshotIndex, cullingSnapshot)) return fail("GPU culling snapshot was not ready");
			if (activeCount(cullingSnapshot) != 5u || cullingSnapshot.counters.renderedCount != 1u ||
				cullingSnapshot.counters.culledCount != 4u)
				return fail("frustum, distance, projected-size, or effect visibility culling did not compact five survivors to one draw");
			runFrame(system, 10.0f);

			stage = "GPU screen-space depth collision";
			RenderTextureOptions collisionDepthOptions;
			collisionDepthOptions.numAttachments = 0u;
			collisionDepthOptions.depthAttachment = RenderTextureDepthAttachment::DepthTexture;
			collisionDepthOptions.depthParams.params.minFilter = GL_NEAREST;
			collisionDepthOptions.depthParams.params.magFilter = GL_NEAREST;
			collisionDepthOptions.depthParams.params.wrap = GL_CLAMP_TO_EDGE;
			auto collisionDepthTarget = renderSystem->createRenderTexture(
				"__mpp_particle_test_collision_depth__", 16u, 16u, collisionDepthOptions);
			auto collisionDepth = std::dynamic_pointer_cast<RenderTexture>(collisionDepthTarget);
			if (!collisionDepth) return fail("screen-space collision depth target was not a render texture");
			auto projection = glm::perspective(glm::radians(60.0f), 4.0f / 3.0f, 0.1f, 100.0f);
			renderSystem->setCameraFrame(glm::mat4(1.0f), projection, { 16.0f, 16.0f }, 0.1f, 100.0f, 0.0f);
			glm::vec4 surfaceClip = projection * glm::vec4(0.0f, 0.0f, -5.0f, 1.0f);
			double surfaceDepth = double(surfaceClip.z / surfaceClip.w * 0.5f + 0.5f);
			renderSystem->pushRenderTarget(collisionDepthTarget);
			GL_CHECK(glClearDepth(surfaceDepth));
			GL_CHECK(glClear(GL_DEPTH_BUFFER_BIT));
			renderSystem->popRenderTarget();
			system.setScreenSpaceCollisionDepth(std::static_pointer_cast<Resource>(collisionDepth));
			auto screenEmitter = burst(1u, 1u, 10.0f);
			screenEmitter.localTransform = glm::translate(glm::mat4(1.0f), { 0.0f, 0.0f, -4.0f });
			screenEmitter.simulation.initialVelocityMin = { 0.0f, 0.0f, -2.0f, 0.0f };
			screenEmitter.simulation.initialVelocityMax = screenEmitter.simulation.initialVelocityMin;
			screenEmitter.simulation.lifetimeSizeRanges[2] = 0.1f;
			screenEmitter.simulation.lifetimeSizeRanges[3] = 0.1f;
			screenEmitter.simulation.shapeSeedModulesBudget[2] = uint32_t(ParticleBehaviourModule::Collision);
			screenEmitter.simulation.collisionConfiguration = { uint32_t(ParticleCollisionSource::ScreenSpace),
				uint32_t(ParticleCollisionResponse::Bounce), 0u, 0u };
			screenEmitter.simulation.collisionParameters = { 1.0f, 0.0f, 1.0f, 0.25f };
			std::array screenEmitters{ screenEmitter };
			auto screenEffect = system.createEffect(screenEmitters);
			auto screenHandle = system.getEmitter(screenEffect, 0u);
			runFrame(system, 0.0f);
			runFrame(system, 0.6f);
			GL_CHECK(glFinish());
			ParticleDrawArraysIndirectCommand screenCommand;
			GL_CHECK(glBindBuffer(GL_COPY_READ_BUFFER, system.mIndirectCommands->getBuffer()));
			GL_CHECK(glGetBufferSubData(GL_COPY_READ_BUFFER,
				GLintptr(screenHandle.index * sizeof(ParticleDrawArraysIndirectCommand)), sizeof(screenCommand), &screenCommand));
			if (screenCommand.instanceCount != 1u) return fail("screen-space collision removed a bouncing particle");
			uint32_t screenParticleIndex = 0u;
			GL_CHECK(glBindBuffer(GL_COPY_READ_BUFFER, system.mRenderIndices->getBuffer()));
			GL_CHECK(glGetBufferSubData(GL_COPY_READ_BUFFER, GLintptr(size_t(screenCommand.first / 4u) * sizeof(uint32_t)),
				sizeof(screenParticleIndex), &screenParticleIndex));
			ParticleRecord screenParticle;
			GL_CHECK(glBindBuffer(GL_COPY_READ_BUFFER, system.mParticlePool->getBuffer()));
			GL_CHECK(glGetBufferSubData(GL_COPY_READ_BUFFER, GLintptr(size_t(screenParticleIndex) * sizeof(ParticleRecord)),
				sizeof(screenParticle), &screenParticle));
			GL_CHECK(glBindBuffer(GL_COPY_READ_BUFFER, 0));
			if (screenParticle.velocityLifetime[2] < 1.9f ||
				(screenParticle.flags & uint32_t(ParticleFlag::CollisionEvent)) == 0u)
				return fail("screen-space depth collision did not reconstruct the surface and bounce");
			runFrame(system, 10.0f);
			system.setScreenSpaceCollisionDepth({});
			collisionDepth.reset();
			collisionDepthTarget.reset();
			renderSystem->getResourceManager()->deleteResourceTree("__mpp_particle_test_collision_depth__");

			stage = "GPU analytical collision responses";
			ParticleCollider floor;
			floor.shapeAndPadding[0] = uint32_t(ParticleColliderShape::Plane);
			floor.first = { 0.0f, 1.0f, 0.0f, 0.0f };
			std::array collisionWorld{ floor };
			system.setColliders(collisionWorld);
			std::array<ParticleEmitterTemplate, 5> collisionEmitters;
			for (uint32_t response = 0u; response < collisionEmitters.size(); ++response)
			{
				auto& emitter = collisionEmitters[response];
				emitter = burst(1u, 1u, 10.0f);
				emitter.localTransform = glm::translate(glm::mat4(1.0f), { 0.0f, 1.0f, -5.0f });
				emitter.simulation.initialVelocityMin = { 1.0f, -2.0f, 0.0f, 0.0f };
				emitter.simulation.initialVelocityMax = emitter.simulation.initialVelocityMin;
				emitter.simulation.lifetimeSizeRanges[2] = 0.1f;
				emitter.simulation.lifetimeSizeRanges[3] = 0.1f;
				emitter.simulation.shapeSeedModulesBudget[2] = uint32_t(ParticleBehaviourModule::Collision);
				emitter.simulation.collisionConfiguration = { uint32_t(ParticleCollisionSource::Analytical), response, 0u, 0u };
				emitter.simulation.collisionParameters = { 1.0f, 0.25f, 1.0f, 0.1f };
			}
			auto collisionEffect = system.createEffect(collisionEmitters);
			runFrame(system, 0.0f);
			runFrame(system, 0.6f);
			GL_CHECK(glFinish());
			std::array<ParticleRecord, 5> collidedParticles;
			for (uint32_t response = 0u; response < collisionEmitters.size(); ++response)
			{
				auto handle = system.getEmitter(collisionEffect, response);
				ParticleDrawArraysIndirectCommand command;
				GL_CHECK(glBindBuffer(GL_COPY_READ_BUFFER, system.mIndirectCommands->getBuffer()));
				GL_CHECK(glGetBufferSubData(GL_COPY_READ_BUFFER,
					GLintptr(handle.index * sizeof(ParticleDrawArraysIndirectCommand)), sizeof(command), &command));
				if (response == uint32_t(ParticleCollisionResponse::Kill))
				{
					if (command.instanceCount != 0u) return fail("kill collision response retained its particle");
					continue;
				}
				if (command.instanceCount != 1u) return fail("non-kill collision response removed its particle");
				uint32_t particleIndex = 0u;
				GL_CHECK(glBindBuffer(GL_COPY_READ_BUFFER, system.mRenderIndices->getBuffer()));
				GL_CHECK(glGetBufferSubData(GL_COPY_READ_BUFFER, GLintptr(size_t(command.first / 4u) * sizeof(uint32_t)),
					sizeof(particleIndex), &particleIndex));
				GL_CHECK(glBindBuffer(GL_COPY_READ_BUFFER, system.mParticlePool->getBuffer()));
				GL_CHECK(glGetBufferSubData(GL_COPY_READ_BUFFER, GLintptr(size_t(particleIndex) * sizeof(ParticleRecord)),
					sizeof(ParticleRecord), &collidedParticles[response]));
			}
			GL_CHECK(glBindBuffer(GL_COPY_READ_BUFFER, 0));
			auto collisionBits = uint32_t(ParticleFlag::Colliding) | uint32_t(ParticleFlag::CollisionEvent);
			if ((collidedParticles[0].flags & collisionBits) != collisionBits ||
				collidedParticles[0].positionAge[1] < 0.099f || collidedParticles[0].velocityLifetime[1] < 1.9f)
				return fail("bounce collision did not depenetrate and reflect velocity");
			if (std::abs(collidedParticles[1].velocityLifetime[1]) > 0.001f ||
				std::abs(collidedParticles[1].velocityLifetime[0] - 0.75f) > 0.01f)
				return fail("slide collision did not remove normal velocity and apply friction");
			if (std::abs(collidedParticles[2].velocityLifetime[0]) > 0.001f ||
				std::abs(collidedParticles[2].velocityLifetime[1]) > 0.001f)
				return fail("stop collision response retained velocity");
			if ((collidedParticles[4].flags & uint32_t(ParticleFlag::SpawnSecondaryEffect)) == 0u ||
				(collidedParticles[4].flags & uint32_t(ParticleFlag::CollisionEvent)) == 0u)
				return fail("spawn-secondary collision response did not publish a one-frame GPU event flag");
			runFrame(system, 10.0f);
			system.setColliders({});

			stage = "GPU secondary bursts and asynchronous typed events";
			system.setColliders(collisionWorld);
			auto eventSourceTemplate = burst(1u, 1u, 1.0f);
			eventSourceTemplate.localTransform = glm::translate(glm::mat4(1.0f), { 0.0f, 1.0f, -5.0f });
			eventSourceTemplate.simulation.initialVelocityMin = { 0.0f, -2.0f, 0.0f, 0.0f };
			eventSourceTemplate.simulation.initialVelocityMax = eventSourceTemplate.simulation.initialVelocityMin;
			eventSourceTemplate.simulation.shapeSeedModulesBudget[2] = uint32_t(ParticleBehaviourModule::Collision);
			eventSourceTemplate.simulation.collisionConfiguration = { uint32_t(ParticleCollisionSource::Analytical),
				uint32_t(ParticleCollisionResponse::Stop), 0u, 0u };
			eventSourceTemplate.lighting.colourAndIntensity = { 1.0f, 0.25f, 0.05f, 2.0f };
			eventSourceTemplate.lighting.rangeAndVolumetric = { 2.0f, 0.5f, 0.0f, 0.0f };
			eventSourceTemplate.lighting.flagsAndPadding[0] = uint32_t(ParticleLightingFlag::ProxyLight |
				ParticleLightingFlag::PbrLightInjection | ParticleLightingFlag::VolumetricContribution);
			eventSourceTemplate.appearance.sorting[2] = 1u;
			eventSourceTemplate.events = {
				{ ParticleEventTrigger::Spawn, ParticleEventAction::SecondaryParticleBurst, 1u, 7u, 0.0f, 10u },
				{ ParticleEventTrigger::Spawn, ParticleEventAction::Audio, 0u, 1u, 0.0f, 11u },
				{ ParticleEventTrigger::Age, ParticleEventAction::Decal, 0u, 1u, 0.5f, 12u },
				{ ParticleEventTrigger::Death, ParticleEventAction::GameplayCallback, 0u, 1u, 0.0f, 13u },
				{ ParticleEventTrigger::Collision, ParticleEventAction::Light, 0u, 1u, 0.0f, 14u }
			};
			auto eventTargetTemplate = burst(0u, 7u, 2.0f);
			eventTargetTemplate.simulation.emissionState[1] = 0u;
			std::array eventTemplates{ eventSourceTemplate, eventTargetTemplate };
			std::array<bool, 5> delivered{};
			std::array<uint32_t, 5> const expectedPayloads{ 0u, 12u, 11u, 14u, 13u };
			auto receiveEvent = [&](ParticleEvent const& event)
			{
				auto action = uint32_t(event.getAction());
				if (action < delivered.size() && event.getPayload() == expectedPayloads[action] &&
					(action != uint32_t(ParticleEventAction::Light) || event.normalAndPadding[1] > 0.9f))
					delivered[action] = true;
			};
			system.setEventCallback(ParticleEventAction::Decal, receiveEvent);
			system.setEventCallback(ParticleEventAction::Audio, receiveEvent);
			system.setEventCallback(ParticleEventAction::Light, receiveEvent);
			system.setEventCallback(ParticleEventAction::GameplayCallback, receiveEvent);
			auto const particleProjection = glm::perspective(glm::radians(60.0f), 1.0f, 0.1f, 100.0f);
			renderSystem->setCameraFrame(glm::mat4(1.0f), particleProjection,
				{ 32.0f, 32.0f }, 0.1f, 100.0f, 0.0f);
			system.setStatisticsEnabled(true);
			auto eventEffect = system.createEffect(eventTemplates,
				ParticleEffectBounds{ { 0.0f, 1.0f, -5.0f }, { 4.0f, 4.0f, 4.0f } });
			auto eventSourceHandle = system.getEmitter(eventEffect, 0u);
			auto eventTargetHandle = system.getEmitter(eventEffect, 1u);
			runFrame(system, 0.0f);
			GL_CHECK(glFinish());
			ParticleDrawArraysIndirectCommand eventSourceCommand, eventTargetCommand;
			GL_CHECK(glBindBuffer(GL_COPY_READ_BUFFER, system.mIndirectCommands->getBuffer()));
			GL_CHECK(glGetBufferSubData(GL_COPY_READ_BUFFER,
				GLintptr(eventSourceHandle.index * sizeof(ParticleDrawArraysIndirectCommand)), sizeof(eventSourceCommand), &eventSourceCommand));
			GL_CHECK(glGetBufferSubData(GL_COPY_READ_BUFFER,
				GLintptr(eventTargetHandle.index * sizeof(ParticleDrawArraysIndirectCommand)), sizeof(eventTargetCommand), &eventTargetCommand));
			GL_CHECK(glBindBuffer(GL_COPY_READ_BUFFER, 0));
			if (eventSourceCommand.instanceCount != 1u || eventTargetCommand.instanceCount != 7u)
				return fail("spawn event did not create its secondary particle burst entirely on the GPU");
			auto proxies = system.getProxyLights(ParticleSystem::MaxEmitterCount, true);
			if (proxies.size() != 1u || proxies[0].emitter != eventSourceHandle ||
				proxies[0].light.position != glm::vec3(0.0f, 1.0f, -5.0f))
				return fail("a particle emitter did not remain exactly one transformed dynamic-light proxy");
			system.setEffectTransform(eventEffect,
				glm::translate(glm::mat4(1.0f), { 100.0f, 0.0f, 0.0f }));
			if (system.isEmitterSubmittedForCurrentView(eventSourceHandle.index))
				return fail("a live particle effect transform did not move its bounds out of the current frustum");
			system.setEffectTransform(eventEffect, glm::mat4(1.0f));
			if (!system.isEmitterSubmittedForCurrentView(eventSourceHandle.index))
				return fail("a live particle effect transform did not restore its bounds to the current frustum");

			auto const awayView = glm::rotate(glm::mat4(1.0f), glm::radians(180.0f), { 0.0f, 1.0f, 0.0f });
			renderSystem->setCameraFrame(awayView, particleProjection,
				{ 32.0f, 32.0f }, 0.1f, 100.0f, 0.0f);
			if (!system.getProxyLights().empty() || system.isEmitterSubmittedForCurrentView(eventSourceHandle.index))
				return fail("an off-view bounded particle effect still submitted proxy-light or PBR-injection contributions");
			system.advanceStatisticsFrame();
			GL_CHECK(glFinish());
			system.advanceStatisticsFrame();
			auto const boundsStats = system.getStats();
			if (!boundsStats.valid || boundsStats.submittedEffects == 0u || boundsStats.boundsCulledEffects == 0u)
				return fail("public particle statistics did not distinguish submitted and bounds-culled effects across views");
			// Effect-level rejection is render-only: the GPU-authored particle ranges
			// above and the lagged events below continue while this view rejects it.
			system.setStatisticsEnabled(false);
			RenderTextureOptions volumetricTargetOptions;
			volumetricTargetOptions.numAttachments = 1u;
			volumetricTargetOptions.colourInternalFormat = GL_RGBA16F;
			auto volumetricTarget = renderSystem->createRenderTexture(
				"ParticleGpuTest.VolumetricLighting", 32u, 32u, volumetricTargetOptions);
			renderSystem->pushRenderTarget(volumetricTarget);
			renderSystem->setViewport(0u, 0u, 32u, 32u);
			GL_CHECK(glDisable(GL_DEPTH_TEST));
			GL_CHECK(glDisable(GL_CULL_FACE));
			GL_CHECK(glDisable(GL_BLEND));
			GL_CHECK(glClearColor(0.0f, 0.0f, 0.0f, 0.0f));
			GL_CHECK(glClear(GL_COLOR_BUFFER_BIT));
			system.render(ParticleBlendClass::Additive, static_cast<RenderTexture*>(nullptr));
			system.renderDistortion(static_cast<RenderTexture*>(nullptr));
			system.renderVolumetricLighting(static_cast<RenderTexture*>(nullptr));
			std::array<float, 4> rejectedPixel{};
			GL_CHECK(glReadPixels(16, 16, 1, 1, GL_RGBA, GL_FLOAT, rejectedPixel.data()));
			if (rejectedPixel != std::array<float, 4>{})
				return fail("bounds-culled billboard, distortion, or volumetric contributions reached the render target");

			renderSystem->setCameraFrame(glm::mat4(1.0f), particleProjection,
				{ 32.0f, 32.0f }, 0.1f, 100.0f, 0.0f);
			GL_CHECK(glClear(GL_COLOR_BUFFER_BIT));
			system.renderVolumetricLighting(static_cast<RenderTexture*>(nullptr));
			std::array<float, 4> volumetricPixel{};
			GL_CHECK(glReadPixels(16, 16, 1, 1, GL_RGBA, GL_FLOAT, volumetricPixel.data()));
			renderSystem->popRenderTarget();
			renderSystem->setDefaultState();
			if (volumetricPixel[0] <= 0.5f || volumetricPixel[1] <= 0.1f || volumetricPixel[3] != 0.0f)
				return fail("emitter-level volumetric lighting did not add HDR depth-aware radiance");
			runFrame(system, 0.6f);
			runFrame(system, 0.5f);
			GL_CHECK(glFinish());
			system.pollEventReadback();
			system.pollEventReadback();
			for (uint32_t action = 1u; action < delivered.size(); ++action)
				if (!delivered[action]) return fail("spawn, age, collision, or death event did not reach its typed lagged callback");
			system.clearEventCallback(ParticleEventAction::Decal);
			system.clearEventCallback(ParticleEventAction::Audio);
			system.clearEventCallback(ParticleEventAction::Light);
			system.clearEventCallback(ParticleEventAction::GameplayCallback);
			system.setColliders({});
			runFrame(system, 100.0f);
			// Later GPU stages append non-additive templates and require the global
			// command table to remain blend-class ordered. Occupy every recycled
			// additive slot before they do so this test does not perturb that contract.
			while (!system.mFreeEmitterIndices.empty())
			{
				auto dormant = burst(0u, 0u, 0.0f);
				dormant.simulation.emissionState = { 0u, 0u, 0u, 0u };
				std::array dormantTemplates{ dormant };
				(void)system.createEffect(dormantTemplates);
			}

			stage = "GPU signed distance field collision";
			auto sdfStream = std::make_shared<ProgrammaticTextureStream>(renderSystem->getResourceManager());
			sdfStream->setTarget(TextureTarget::Texture3D);
			sdfStream->setInternalFormat(TextureInternalType::Float, false, 32u, 1u);
			sdfStream->setFiltering(TextureParams::MinFilter::Linear, TextureParams::MagFilter::Linear);
			sdfStream->setWrapping(TextureParams::Wrapping::ClampToEdge);
			sdfStream->setData([](std::string const&)
			{
				constexpr size_t size = 5u;
				auto* data = new uint8_t[size * size * size * sizeof(float)];
				auto* values = reinterpret_cast<float*>(data);
				for (size_t z = 0; z < size; ++z)
					for (size_t y = 0; y < size; ++y)
						for (size_t x = 0; x < size; ++x)
							values[(z * size + y) * size + x] = float(y) - 2.0f;
				return TextureData(data, size, size, size, 32u, GL_RED, GL_FLOAT);
			});
			auto sdfTexture = renderSystem->getResourceManager()->declareResource("__mpp_particle_test_sdf__", sdfStream).first;
			sdfTexture->load();
			glm::mat4 worldToTexture(1.0f);
			worldToTexture[0][0] = 0.25f;
			worldToTexture[1][1] = 0.25f;
			worldToTexture[2][2] = 0.25f;
			worldToTexture[3] = { 0.5f, 0.5f, 1.75f, 1.0f };
			system.setSignedDistanceField(sdfTexture, worldToTexture);
			auto sdfEmitter = burst(1u, 1u, 10.0f);
			sdfEmitter.localTransform = glm::translate(glm::mat4(1.0f), { 0.0f, 1.0f, -5.0f });
			sdfEmitter.simulation.initialVelocityMin = { 0.0f, -2.0f, 0.0f, 0.0f };
			sdfEmitter.simulation.initialVelocityMax = sdfEmitter.simulation.initialVelocityMin;
			sdfEmitter.simulation.lifetimeSizeRanges[2] = 0.1f;
			sdfEmitter.simulation.lifetimeSizeRanges[3] = 0.1f;
			sdfEmitter.simulation.shapeSeedModulesBudget[2] = uint32_t(ParticleBehaviourModule::Collision);
			sdfEmitter.simulation.collisionConfiguration = { uint32_t(ParticleCollisionSource::SignedDistanceField),
				uint32_t(ParticleCollisionResponse::Bounce), 0u, 0u };
			sdfEmitter.simulation.collisionParameters[0] = 1.0f;
			std::array sdfEmitters{ sdfEmitter };
			auto sdfEffect = system.createEffect(sdfEmitters);
			auto sdfHandle = system.getEmitter(sdfEffect, 0u);
			runFrame(system, 0.0f);
			runFrame(system, 0.6f);
			GL_CHECK(glFinish());
			ParticleDrawArraysIndirectCommand sdfCommand;
			GL_CHECK(glBindBuffer(GL_COPY_READ_BUFFER, system.mIndirectCommands->getBuffer()));
			GL_CHECK(glGetBufferSubData(GL_COPY_READ_BUFFER,
				GLintptr(sdfHandle.index * sizeof(ParticleDrawArraysIndirectCommand)), sizeof(sdfCommand), &sdfCommand));
			if (sdfCommand.instanceCount != 1u) return fail("SDF collision removed a bouncing particle");
			uint32_t sdfParticleIndex = 0u;
			GL_CHECK(glBindBuffer(GL_COPY_READ_BUFFER, system.mRenderIndices->getBuffer()));
			GL_CHECK(glGetBufferSubData(GL_COPY_READ_BUFFER, GLintptr(size_t(sdfCommand.first / 4u) * sizeof(uint32_t)),
				sizeof(sdfParticleIndex), &sdfParticleIndex));
			ParticleRecord sdfParticle;
			GL_CHECK(glBindBuffer(GL_COPY_READ_BUFFER, system.mParticlePool->getBuffer()));
			GL_CHECK(glGetBufferSubData(GL_COPY_READ_BUFFER, GLintptr(size_t(sdfParticleIndex) * sizeof(ParticleRecord)),
				sizeof(sdfParticle), &sdfParticle));
			GL_CHECK(glBindBuffer(GL_COPY_READ_BUFFER, 0));
			if (sdfParticle.positionAge[1] < 0.099f || sdfParticle.velocityLifetime[1] < 1.9f ||
				(sdfParticle.flags & uint32_t(ParticleFlag::CollisionEvent)) == 0u)
				return fail("SDF collision did not sample, depenetrate, and bounce (y=" +
					std::to_string(sdfParticle.positionAge[1]) + ", vy=" + std::to_string(sdfParticle.velocityLifetime[1]) +
					", flags=" + std::to_string(sdfParticle.flags) + ")");
			runFrame(system, 10.0f);
			system.clearSignedDistanceField();
			sdfTexture.reset();
			renderSystem->getResourceManager()->deleteResourceTree("__mpp_particle_test_sdf__");

			stage = "GPU alpha depth radix sort";
			if (system.mSortRecordsA || system.mSortRecordsB || system.mRadixHistogram || system.mSortDispatchCommand ||
				renderSystem->getResourceManager()->getResource("__mpp_particle_sort_keys__", true))
				return fail("additive and weighted OIT appearances allocated depth-sort resources");
			auto sorted = burst(257u, 257u, 10.0f);
			sorted.simulation.shapeSeedModulesBudget[0] = uint32_t(ParticleSpawnShape::Line);
			sorted.simulation.shapeParameters = { 0.0f, 0.0f, 8.0f, 0.0f };
			sorted.localTransform = glm::translate(glm::mat4(1.0f), { 0.0f, 0.0f, -20.0f });
			sorted.appearance.modes[3] = uint32_t(ParticleBlendClass::Alpha);
			sorted.appearance.sorting[0] = uint32_t(ParticleSortMode::BackToFront);
			std::array sortedEmitters{ sorted };
			auto sortedEffect = system.createEffect(sortedEmitters);
			auto sortedEmitter = system.getEmitter(sortedEffect, 0u);
			runFrame(system, 0.0f);
			GL_CHECK(glFinish());
			if (!system.mSortRecordsA || !system.mSortRecordsB || !system.mRadixHistogram || !system.mSortDispatchCommand ||
				!renderSystem->getResourceManager()->getResource("__mpp_particle_sort_keys__", true))
				return fail("an opted-in alpha appearance did not allocate depth-sort resources");
			ParticleDrawArraysIndirectCommand sortedCommand;
			GL_CHECK(glBindBuffer(GL_COPY_READ_BUFFER, system.mIndirectCommands->getBuffer()));
			GL_CHECK(glGetBufferSubData(GL_COPY_READ_BUFFER,
				GLintptr(sortedEmitter.index * sizeof(ParticleDrawArraysIndirectCommand)), sizeof(sortedCommand), &sortedCommand));
			if (sortedCommand.instanceCount != 257u) return fail("depth-sort test appearance did not retain every visible particle");
			std::vector<uint32_t> sortedIndices(sortedCommand.instanceCount);
			uint32_t const sortedOffset = sortedCommand.first / 4u;
			GL_CHECK(glBindBuffer(GL_COPY_READ_BUFFER, system.mRenderIndices->getBuffer()));
			GL_CHECK(glGetBufferSubData(GL_COPY_READ_BUFFER, GLintptr(size_t(sortedOffset) * sizeof(uint32_t)),
				GLsizeiptr(sortedIndices.size() * sizeof(uint32_t)), sortedIndices.data()));
			float previousDepth = std::numeric_limits<float>::infinity();
			for (uint32_t particleIndex : sortedIndices)
			{
				ParticleRecord particle;
				GL_CHECK(glBindBuffer(GL_COPY_READ_BUFFER, system.mParticlePool->getBuffer()));
				GL_CHECK(glGetBufferSubData(GL_COPY_READ_BUFFER, GLintptr(size_t(particleIndex) * sizeof(ParticleRecord)),
					sizeof(particle), &particle));
				float const depth = -particle.positionAge[2];
				if (depth > previousDepth) return fail("alpha render indices were not radix-sorted back-to-front");
				previousDepth = depth;
			}
			GL_CHECK(glBindBuffer(GL_COPY_READ_BUFFER, 0));
			runFrame(system, 10.0f);

			stage = "GPU real-mesh particle instancing";
			mesh::MeshSpecification meshSpecification(mesh::Primitive::Type::Triangles);
			auto meshLayout = meshSpecification.createVertexBufferAttributeLayout(false);
			meshLayout->createAttribute(mesh::Vertex::Component::Position3, mesh::Vertex::DataType::Float, false);
			meshLayout->createAttribute(mesh::Vertex::Component::Normal3, mesh::Vertex::DataType::Float, false);
			meshLayout->createAttribute(mesh::Vertex::Component::TexCoord2, mesh::Vertex::DataType::Float, false);
			meshLayout->createAttribute(mesh::Vertex::Component::Colour4, mesh::Vertex::DataType::Float, false);
			auto meshParser = std::make_shared<program::Parser>();
			meshParser->setMeshSpecification(meshSpecification);
			meshParser->setVertexSource(VertexShader3dTemplate);
			meshParser->setFragmentSource(R"(
@@Version
@@Uniform(vec4 MESH_PARTICLE_TEST_COLOUR);
void main()
{
    @Out(vec4 COLOUR) = @Uniform(MESH_PARTICLE_TEST_COLOUR);
}
)");
			auto meshProgramStream = std::make_shared<ProgrammaticProgramStream>(renderSystem->getResourceManager());
			meshProgramStream->setParser(meshParser);
			auto meshProgram = renderSystem->getResourceManager()->declareResource("ParticleGpuTest.MeshProgram", meshProgramStream).first;
			meshProgram->load();
			auto* generatedMeshProgram = dynamic_cast<Program*>(meshProgram.get());
			if (!generatedMeshProgram || generatedMeshProgram->getUniformId("MPP_PARTICLE_MESH_ENABLED") < 0 ||
				generatedMeshProgram->getUniformId("MPP_PARTICLE_MESH_TEMPLATE") < 0)
				return fail("3D material program did not expose the opt-in mesh-particle instance path");
			auto meshMaterialStream = std::make_shared<ProgrammaticBasicMaterialStream>(renderSystem->getResourceManager());
			meshMaterialStream->setProgram(meshProgram->getName());
			meshMaterialStream->setUniform("MESH_PARTICLE_TEST_COLOUR", glm::vec4(0.2f, 0.9f, 0.3f, 1.0f));
			auto meshMaterial = renderSystem->getResourceManager()->declareResource("ParticleGpuTest.MeshMaterial", meshMaterialStream).first;
			meshMaterial->load();
			auto meshModelStream = std::make_shared<BoxModelStream>(renderSystem->getResourceManager(), meshSpecification,
				meshMaterial->getName(), 1.0f, 1.0f, 1.0f);
			auto meshModel = renderSystem->getResourceManager()->declareResource("ParticleGpuTest.MeshModel", meshModelStream).first;
			meshModel->load();

			auto meshEmitter = burst(3u, 3u, 100.0f);
			meshEmitter.meshModel = meshModel;
			meshEmitter.meshMaterial = meshMaterial;
			meshEmitter.localTransform = glm::translate(glm::mat4(1.0f), { 0.0f, 0.0f, -5.0f });
			meshEmitter.simulation.lifetimeSizeRanges[2] = 0.5f;
			meshEmitter.simulation.lifetimeSizeRanges[3] = 0.5f;
			meshEmitter.simulation.rotationRanges = { 0.5f, 0.5f, 0.25f, 0.25f };
			std::array meshEmitters{ meshEmitter };
			auto meshEffect = system.createEffect(meshEmitters,
				ParticleEffectBounds{ { 0.0f, 0.0f, -5.0f }, { 2.0f, 2.0f, 2.0f } });
			auto meshHandle = system.getEmitter(meshEffect, 0u);
			renderSystem->setCameraFrame(glm::mat4(1.0f),
				glm::perspective(glm::radians(60.0f), 1.0f, 0.1f, 100.0f), { 32.0f, 32.0f }, 0.1f, 100.0f, 0.0f);
			runFrame(system, 0.0f);
			GL_CHECK(glFinish());
			if (system.mMeshDrawRecords.size() != 1u || !system.mMeshIndirectCommands)
				return fail("mesh particle model did not produce one dedicated real-mesh command");
			ParticleMeshDrawIndirectCommand meshCommand;
			GL_CHECK(glBindBuffer(GL_COPY_READ_BUFFER, system.mMeshIndirectCommands->getBuffer()));
			GL_CHECK(glGetBufferSubData(GL_COPY_READ_BUFFER, 0, sizeof(meshCommand), &meshCommand));
			ParticleDrawArraysIndirectCommand billboardCommand;
			GL_CHECK(glBindBuffer(GL_COPY_READ_BUFFER, system.mIndirectCommands->getBuffer()));
			GL_CHECK(glGetBufferSubData(GL_COPY_READ_BUFFER,
				GLintptr(meshHandle.index * sizeof(ParticleDrawArraysIndirectCommand)), sizeof(billboardCommand), &billboardCommand));
			GL_CHECK(glBindBuffer(GL_COPY_READ_BUFFER, 0));
			if (meshCommand.count == 0u || meshCommand.instanceCount != 3u || billboardCommand.instanceCount != 0u)
				return fail("mesh particle instances did not stay GPU-authored and exclusive to the mesh pass");

			RenderTextureOptions meshTargetOptions;
			meshTargetOptions.numAttachments = 1u;
			meshTargetOptions.depthAttachment = RenderTextureDepthAttachment::DepthRenderbuffer;
			auto meshTarget = renderSystem->createRenderTexture("ParticleGpuTest.MeshTarget", 32u, 32u, meshTargetOptions);
			renderSystem->pushRenderTarget(meshTarget);
			renderSystem->setViewport(0u, 0u, 32u, 32u);
			GL_CHECK(glClearColor(0.0f, 0.0f, 0.0f, 1.0f));
			GL_CHECK(glClearDepth(1.0));
			GL_CHECK(glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT));
			renderSystem->setCameraFrame(glm::rotate(glm::mat4(1.0f), glm::radians(180.0f),
				{ 0.0f, 1.0f, 0.0f }), particleProjection, { 32.0f, 32.0f }, 0.1f, 100.0f, 0.0f);
			system.renderMeshes();
			std::array<uint8_t, 4> rejectedMeshPixel{};
			GL_CHECK(glReadPixels(16, 16, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, rejectedMeshPixel.data()));
			if (rejectedMeshPixel[0] != 0u || rejectedMeshPixel[1] != 0u || rejectedMeshPixel[2] != 0u)
				return fail("a bounds-culled mesh-particle contribution reached the render target");
			renderSystem->setCameraFrame(glm::mat4(1.0f), particleProjection,
				{ 32.0f, 32.0f }, 0.1f, 100.0f, 0.0f);
			GL_CHECK(glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT));
			system.renderMeshes();
			std::array<uint8_t, 4> meshPixel{};
			GL_CHECK(glReadPixels(16, 16, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, meshPixel.data()));
			renderSystem->popRenderTarget();
			if (meshPixel[1] < 128u || meshPixel[0] > 128u)
				return fail("mesh particle material did not shade GPU-instanced real geometry (pixel " +
					std::to_string(meshPixel[0]) + "," + std::to_string(meshPixel[1]) + "," +
					std::to_string(meshPixel[2]) + "," + std::to_string(meshPixel[3]) + ")");

			stage = "GPU trail position history and ribbon commands";
			auto& trails = renderSystem->getTrailSystem();
			trails.initialise();
			if (!trails.mAvailable) return fail("trail GPU path is unavailable");
			TrailSpecification trailSpecification;
			trailSpecification.maximumPointCount = 8u;
			trailSpecification.pointLifetime = 0.5f;
			trailSpecification.minimumPointDistance = 0.1f;
			trailSpecification.width = 0.5f;
			trailSpecification.uvScale = 2.0f;
			trailSpecification.widthOverLife.keys = { { 0.0f, 1.0f }, { 1.0f, 0.0f } };
			trailSpecification.colourOverLife.keys = {
				{ 0.0f, { 2.0f, 0.0f, 0.0f } }, { 1.0f, { 0.0f, 0.0f, 3.0f } }
			};
			auto trail = trails.createTrail(trailSpecification, { -0.5f, 0.0f, -5.0f });
			trails.simulate(0.0f);
			trails.setTrailPosition(trail, { 0.5f, 0.0f, -5.0f });
			trails.simulate(0.1f);
			trails.stopTrail(trail);
			GL_CHECK(glFinish());
			TrailState trailState;
			GL_CHECK(glBindBuffer(GL_COPY_READ_BUFFER, trails.mStates->getBuffer()));
			GL_CHECK(glGetBufferSubData(GL_COPY_READ_BUFFER, GLintptr(size_t(trail.index) * sizeof(TrailState)),
				sizeof(trailState), &trailState));
			if (trailState.ring[1] != 2u) return fail("trail did not retain two independent position-history points");
			std::array<TrailPointRecord, 2> trailPoints;
			GL_CHECK(glBindBuffer(GL_COPY_READ_BUFFER, trails.mPoints->getBuffer()));
			GL_CHECK(glGetBufferSubData(GL_COPY_READ_BUFFER,
				GLintptr((size_t(trail.index) * TrailSystem::MaxPointCount + trailState.ring[0]) * sizeof(TrailPointRecord)),
				sizeof(trailPoints), trailPoints.data()));
			if (std::abs(trailPoints[0].positionAge[0] + 0.5f) > 0.001f ||
				std::abs(trailPoints[1].positionAge[0] - 0.5f) > 0.001f ||
				std::abs(trailPoints[0].positionAge[3] - 0.1f) > 0.001f ||
				std::abs(trailPoints[0].lifetimeDistance[0] - 0.5f) > 0.001f ||
				std::abs(trailPoints[1].lifetimeDistance[0] - 0.5f) > 0.001f ||
				std::abs(trailPoints[1].lifetimeDistance[1] - 1.0f) > 0.001f)
				return fail("trail points lost position, per-point lifetime, age, or cumulative UV distance");
			ParticleDrawArraysIndirectCommand trailCommand;
			size_t const trailCommandIndex = size_t(uint32_t(ParticleBlendClass::Additive)) * TrailSystem::MaxTrailCount + trail.index;
			GL_CHECK(glBindBuffer(GL_COPY_READ_BUFFER, trails.mIndirectCommands->getBuffer()));
			GL_CHECK(glGetBufferSubData(GL_COPY_READ_BUFFER,
				GLintptr(trailCommandIndex * sizeof(ParticleDrawArraysIndirectCommand)), sizeof(trailCommand), &trailCommand));
			GL_CHECK(glBindBuffer(GL_COPY_READ_BUFFER, 0));
			if (trailCommand.count != 4u || trailCommand.first != trail.index * TrailSystem::MaxPointCount * 2u)
				return fail("trail did not author its separate indirect triangle-strip command");
			std::vector<float> trailLut(size_t(TrailSystem::CurveSampleCount) * TrailSystem::MaxTrailCount * 2u * 4u);
			GL_CHECK(glBindTexture(GL_TEXTURE_2D, trails.mCurveLut));
			GL_CHECK(glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_FLOAT, trailLut.data()));
			GL_CHECK(glBindTexture(GL_TEXTURE_2D, 0));
			size_t const trailRow = size_t(trail.index) * 2u;
			auto trailSample = [&](uint32_t row, uint32_t x, uint32_t channel)
			{
				return trailLut[((trailRow + row) * TrailSystem::CurveSampleCount + x) * 4u + channel];
			};
			if (std::abs(trailSample(0u, 0u, 0u) - 1.0f) > 0.01f ||
				std::abs(trailSample(0u, TrailSystem::CurveSampleCount - 1u, 0u)) > 0.01f ||
				std::abs(trailSample(1u, 0u, 0u) - 2.0f) > 0.01f ||
				std::abs(trailSample(1u, TrailSystem::CurveSampleCount - 1u, 2u) - 3.0f) > 0.01f)
				return fail("trail width-over-life or HDR colour-over-life did not reach the GPU LUT");
			for (size_t frame = 0; frame < 6u; ++frame) trails.simulate(0.1f);
			GL_CHECK(glFinish());
			GL_CHECK(glBindBuffer(GL_COPY_READ_BUFFER, trails.mIndirectCommands->getBuffer()));
			GL_CHECK(glGetBufferSubData(GL_COPY_READ_BUFFER,
				GLintptr(trailCommandIndex * sizeof(ParticleDrawArraysIndirectCommand)), sizeof(trailCommand), &trailCommand));
			GL_CHECK(glBindBuffer(GL_COPY_READ_BUFFER, 0));
			if (trailCommand.count != 0u || trails.isAlive(trail))
				return fail("stopped trail points did not expire independently and retire the ribbon");
			// Leave one valid ribbon for the render graph GPU suite that follows this
			// function, so its generated MPP.TrailScene passes execute non-empty draws.
			trailSpecification.pointLifetime = 10.0f;
			auto graphTrail = trails.createTrail(trailSpecification, { -0.5f, 0.0f, 0.0f });
			trails.simulate(0.0f);
			trails.setTrailPosition(graphTrail, { 0.5f, 0.0f, 0.0f });
			trails.simulate(0.1f);

			stage = "shared particle albedo texture array";
			auto makeAlbedo = [&](char const* name, size_t width, size_t height,
				std::array<uint8_t, 4> colour, TextureColourSpace colourSpace)
			{
				auto stream = std::make_shared<ProgrammaticTextureStream>(renderSystem->getResourceManager());
				stream->setTarget(TextureTarget::Texture2D);
				stream->setInternalFormat(TextureInternalType::UnsignedInteger, true, 8u, 4u);
				stream->setColourSpace(colourSpace);
				stream->setData([=](std::string const&)
				{
					auto* data = new uint8_t[width * height * 4u];
					for (size_t texel = 0; texel < width * height; ++texel)
						std::copy(colour.begin(), colour.end(), data + texel * 4u);
					return TextureData(data, width, height, 32u, GL_RGBA, GL_UNSIGNED_BYTE);
				});
				auto resource = renderSystem->getResourceManager()->declareResource(name, stream).first;
				resource->load();
				return resource;
			};
			auto redAlbedo = makeAlbedo("__mpp_particle_test_red_albedo__", 2u, 2u,
				{ 255u, 0u, 0u, 255u }, TextureColourSpace::Linear);
			auto greenAlbedo = makeAlbedo("__mpp_particle_test_green_albedo__", 4u, 1u,
				{ 0u, 255u, 0u, 255u }, TextureColourSpace::Srgb);
			system.mTemplateTextures.push_back(redAlbedo);
			system.mTemplateTextures.push_back(greenAlbedo);
			system.mTemplateTextures.push_back(redAlbedo);
			system.mTemplateRenderData.resize(system.mTemplateRenderData.size() + 3u);
			system.updateAlbedoTextureArray();
			auto const firstAdded = system.mTemplateRenderData.size() - 3u;
			if (system.mTemplateRenderData[firstAdded].textureAndAtlas[0] == 0u ||
				system.mTemplateRenderData[firstAdded].textureAndAtlas[0] != system.mTemplateRenderData[firstAdded + 2u].textureAndAtlas[0] ||
				system.mTemplateRenderData[firstAdded].textureAndAtlas[0] == system.mTemplateRenderData[firstAdded + 1u].textureAndAtlas[0] ||
				system.mTemplateRenderData[firstAdded].textureAndAtlas[1] != 1u ||
				system.mTemplateRenderData[firstAdded + 1u].textureAndAtlas[1] != 3u)
				return fail("shared albedo layers were not unique per texture or did not retain colour-space flags");
			GL_CHECK(glBindTexture(GL_TEXTURE_2D_ARRAY, system.mAlbedoArrayTexture));
			GLint albedoWidth = 0, albedoHeight = 0, albedoLayers = 0;
			GL_CHECK(glGetTexLevelParameteriv(GL_TEXTURE_2D_ARRAY, 0, GL_TEXTURE_WIDTH, &albedoWidth));
			GL_CHECK(glGetTexLevelParameteriv(GL_TEXTURE_2D_ARRAY, 0, GL_TEXTURE_HEIGHT, &albedoHeight));
			GL_CHECK(glGetTexLevelParameteriv(GL_TEXTURE_2D_ARRAY, 0, GL_TEXTURE_DEPTH, &albedoLayers));
			std::vector<uint8_t> albedoTexels(size_t(albedoWidth) * albedoHeight * albedoLayers * 4u);
			GL_CHECK(glGetTexImage(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA, GL_UNSIGNED_BYTE, albedoTexels.data()));
			GL_CHECK(glBindTexture(GL_TEXTURE_2D_ARRAY, 0));
			auto layerTexel = [&](uint32_t layer)
			{
				size_t const offset = size_t(layer) * albedoWidth * albedoHeight * 4u;
				return std::array<uint8_t, 4>{ albedoTexels[offset], albedoTexels[offset + 1u],
					albedoTexels[offset + 2u], albedoTexels[offset + 3u] };
			};
			if (albedoWidth != 4 || albedoHeight != 2 ||
				layerTexel(system.mTemplateRenderData[firstAdded].textureAndAtlas[0]) != std::array<uint8_t, 4>{ 255u, 0u, 0u, 255u } ||
				layerTexel(system.mTemplateRenderData[firstAdded + 1u].textureAndAtlas[0]) != std::array<uint8_t, 4>{ 0u, 255u, 0u, 255u })
				return fail("authored albedo textures were not resampled into the shared array");
			system.mTemplateTextures.resize(firstAdded);
			system.mTemplateRenderData.resize(firstAdded);
			system.updateAlbedoTextureArray();

			stage = "std430 particle record stride";
			auto* spawnProgram = static_cast<RawShaderProgram*>(system.mSpawnProgram.get());
			auto const variable = glGetProgramResourceIndex(spawnProgram->getId(), GL_BUFFER_VARIABLE, "PARTICLES[0].positionAge");
			if (variable == GL_INVALID_INDEX) return fail("linked spawn kernel did not expose the particle record");
			GLenum const property = GL_TOP_LEVEL_ARRAY_STRIDE;
			GLint stride = 0;
			GL_CHECK(glGetProgramResourceiv(spawnProgram->getId(), GL_BUFFER_VARIABLE, variable, 1, &property, 1, nullptr, &stride));
			if (stride != 64 || sizeof(ParticleRecord) != 64u)
				return fail("particle record std430 array stride was " + std::to_string(stride) + " bytes instead of 64");

			GL_CHECK(glUseProgram(0));
			// The suite exercised RenderSystem's frame guard directly, while most of
			// its remaining frames used explicit test deltas. Leave the next suite at
			// a fresh renderer frame boundary so its first graph owns one simulation.
			renderSystem->startStatsCollection();
			return true;
		}
		catch (std::exception const& error)
		{
			return fail(stage + ": " + error.what());
		}
	}
}
