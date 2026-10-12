#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstring>
#include <exception>
#include <stdexcept>
#include <string>
#include <vector>

#include <GL/glew.h>
#include <GL/gl.h>

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "mpp/ComputeProgram.h"
#include "mpp/GLErrorCheck.h"
#include "mpp/GpuDebugScope.h"
#include "mpp/MppException.h"
#include "mpp/Material.h"
#include "mpp/Mesh.h"
#include "mpp/Model.h"
#include "mpp/ParticleCurveLut.h"
#include "mpp/ParticleDrawProgram.h"
#include "mpp/ParticleShaders.h"
#include "mpp/ParticleSystem.h"
#include "mpp/Program.h"
#include "mpp/RawShaderStream.h"
#include "mpp/RenderSystem.h"
#include "mpp/RenderTexture.h"
#include "mpp/ResourceManager.h"
#include "mpp/ShaderStorageBuffer.h"
#include "mpp/Texture.h"
#include "PersistentMappedBuffer.h"

using namespace std;

namespace mpp
{
	namespace detail
	{
		class ParticleStatisticsState
		{
		public:
			static constexpr size_t RingSize = 4;
			static constexpr uint64_t MinimumLag = 2;

			struct QueryPair { GLuint begin{ 0 }; GLuint end{ 0 }; };
			struct Slot
			{
				GLuint buffer{ 0 };
				GLsync fence{ nullptr };
				QueryPair simulation;
				QueryPair sorting;
				std::vector<QueryPair> renders;
				uint64_t sequence{ 0 };
				uint64_t sourceFrame{ 0 };
				uint32_t activeListIndex{ 0 };
				uint32_t activeEmitters{ 0 };
				uint32_t submittedEffects{ 0 };
				uint32_t boundsCulledEffects{ 0 };
				uint32_t capacity{ 0 };
				std::vector<uint64_t> recordedViews;
				bool submitted{ false };
			};

			bool enabled{ false };
			uint64_t sequence{ 0 };
			uint64_t latestSequence{ 0 };
			int currentSlot{ -1 };
			std::array<Slot, RingSize> slots;
			ParticleStats stats;

			~ParticleStatisticsState() { release(); }

			static void deleteQueries(Slot& slot) noexcept
			{
				if (slot.simulation.begin)
				{
					GLuint ids[]{ slot.simulation.begin, slot.simulation.end };
					glDeleteQueries(2, ids);
					slot.simulation = {};
				}
				if (slot.sorting.begin)
				{
					GLuint ids[]{ slot.sorting.begin, slot.sorting.end };
					glDeleteQueries(2, ids);
					slot.sorting = {};
				}
				for (auto const& query : slot.renders)
				{
					GLuint ids[]{ query.begin, query.end };
					glDeleteQueries(2, ids);
				}
				slot.renders.clear();
			}

			void release() noexcept
			{
				for (auto& slot : slots)
				{
					if (slot.fence) glDeleteSync(slot.fence);
					deleteQueries(slot);
					if (slot.buffer) glDeleteBuffers(1, &slot.buffer);
					slot = {};
				}
				currentSlot = -1;
			}
		};

		class ParticleEventReadbackState
		{
		public:
			static constexpr size_t RingSize = 4;
			static constexpr uint64_t MinimumLag = 2;
			struct Slot
			{
				GLuint buffer{ 0 };
				GLsync fence{ nullptr };
				uint64_t sequence{ 0 };
				bool submitted{ false };
			};

			uint64_t sequence{ 0 };
			std::array<Slot, RingSize> slots;
			std::array<ParticleSystem::ParticleEventCallback, 5> callbacks;

			~ParticleEventReadbackState() { release(); }
			void release() noexcept
			{
				for (auto& slot : slots)
				{
					if (slot.fence) glDeleteSync(slot.fence);
					if (slot.buffer) glDeleteBuffers(1, &slot.buffer);
					slot = {};
				}
			}
			bool enabled() const
			{
				return any_of(callbacks.begin() + 1, callbacks.end(), [](auto const& callback) { return bool(callback); });
			}
		};
	}

	namespace
	{
		char const* PoolInitialiseProgramName = "__mpp_particle_pool_initialise__";
		char const* EventPrepareProgramName = "__mpp_particle_event_prepare__";
		char const* SpawnProgramName = "__mpp_particle_spawn__";
		char const* SimulationPrepareProgramName = "__mpp_particle_simulation_prepare__";
		char const* SimulationProgramName = "__mpp_particle_simulation__";
		char const* EventProcessProgramName = "__mpp_particle_event_process__";
		char const* CompactionPrepareProgramName = "__mpp_particle_compaction_prepare__";
		char const* CompactionCountProgramName = "__mpp_particle_compaction_count__";
		char const* CompactionPrefixProgramName = "__mpp_particle_compaction_prefix__";
		char const* CompactionScatterProgramName = "__mpp_particle_compaction_scatter__";
		char const* SortPrepareProgramName = "__mpp_particle_sort_prepare__";
		char const* SortKeyProgramName = "__mpp_particle_sort_keys__";
		char const* RadixHistogramProgramName = "__mpp_particle_radix_histogram__";
		char const* RadixPrefixProgramName = "__mpp_particle_radix_prefix__";
		char const* RadixScatterProgramName = "__mpp_particle_radix_scatter__";
		char const* SortFinalizeProgramName = "__mpp_particle_sort_finalize__";
		char const* DrawProgramName = "__mpp_particle_draw__";
		char const* WeightedOitDrawProgramName = "__mpp_particle_weighted_oit_draw__";
		char const* VolumetricLightingDrawProgramName = "__mpp_particle_volumetric_lighting_draw__";
		char const* MeshCommandProgramName = "__mpp_particle_mesh_commands__";

		constexpr uint32_t ParticlePoolBinding = 0;
		constexpr uint32_t FreeIndicesBinding = 1;
		constexpr uint32_t ActiveIndicesABinding = 2;
		constexpr uint32_t ActiveIndicesBBinding = 3;
		constexpr uint32_t CountersBinding = 4;
		constexpr uint32_t EmitterBinding = 5;
		// Bindings six and seven are stage-local aliases. Template data is draw-only;
		// scratch, spawn, dispatch, collision, and indirect buffers are never read together.
		constexpr uint32_t ColliderBinding = 6;
		constexpr uint32_t EventStorageBinding = 6;
		constexpr uint32_t SimulationEventStorageBinding = 7;
		constexpr uint32_t TemplateRenderBinding = 6;
		constexpr uint32_t CompactionScratchBinding = 6;
		constexpr uint32_t SpawnCommandBinding = 7;
		constexpr uint32_t DispatchCommandBinding = 7;
		constexpr uint32_t IndirectCommandBinding = 7;
		constexpr uint32_t CullingTemplateBinding = 7;
		constexpr uint32_t RequiredStorageBindings = 8;
		constexpr size_t DispatchCommandBytes = 3 * sizeof(uint32_t);
		constexpr uint32_t NoiseTextureSize = 16;

		constexpr size_t eventRulesOffset() { return sizeof(ParticleEventStorageHeader); }
		constexpr size_t eventQueueAOffset()
		{
			return eventRulesOffset() + size_t(ParticleSystem::MaxEventRuleCount) * sizeof(ParticleGpuEventRule);
		}
		constexpr size_t eventQueueBOffset()
		{
			return eventQueueAOffset() + size_t(ParticleSystem::MaxGeneratedEventCount) * sizeof(ParticleEvent);
		}
		constexpr size_t externalEventsOffset()
		{
			return eventQueueBOffset() + size_t(ParticleSystem::MaxGeneratedEventCount) * sizeof(ParticleEvent);
		}
		constexpr size_t eventStorageBytes()
		{
			return externalEventsOffset() + size_t(ParticleSystem::MaxExternalEventCount) * sizeof(ParticleEvent);
		}
		constexpr size_t eventReadbackBytes()
		{
			return sizeof(ParticleEventStorageHeader) + size_t(ParticleSystem::MaxExternalEventCount) * sizeof(ParticleEvent);
		}

		template<typename T>
		size_t bytes(vector<T> const& values)
		{
			return values.size() * sizeof(T);
		}

		void setTransform(EmitterSimData& emitter, glm::mat4 const& transform)
		{
			copy_n(glm::value_ptr(transform), emitter.transform.size(), emitter.transform.begin());
		}

		uint32_t nextGeneration(uint32_t generation)
		{
			++generation;
			return generation == 0 ? 1 : generation;
		}
	}

	ParticleSystem::ParticleSystem(RenderSystem* renderSystem, ResourceManager* resourceManager)
		: mwRenderSystem(renderSystem)
		, mwResourceManager(resourceManager)
		, mStatistics(make_unique<detail::ParticleStatisticsState>())
		, mEventReadback(make_unique<detail::ParticleEventReadbackState>())
	{
	}

	ParticleSystem::~ParticleSystem()
	{
		// Queries, fences and staging buffers require the GL context, just like the
		// other particle resources destroyed below.
		mEventReadback.reset();
		mStatistics.reset();
		mTemplateTextures.clear();
		if (mAlbedoArrayTexture != 0)
		{
			glDeleteTextures(1, &mAlbedoArrayTexture);
			mAlbedoArrayTexture = 0;
		}
		if (mNoiseTexture != 0)
		{
			glDeleteTextures(1, &mNoiseTexture);
			mNoiseTexture = 0;
		}
		if (mVertexArray != 0)
		{
			glDeleteVertexArrays(1, &mVertexArray);
			mVertexArray = 0;
		}

		mScreenSpaceCollisionDepth.reset();
		mVectorFieldTexture.reset();
		mSignedDistanceFieldTexture.reset();
		mColliderBuffer.reset();
		mSpawnCommandBuffer.reset();
		mVolumetricLightingBuffer.reset();
		mTemplateRenderBuffer.reset();
		mEmitterBuffer.reset();
		mMeshCommandTemplates.reset();
		mMeshIndirectCommands.reset();
		mEventDispatchCommand.reset();
		mEventStorage.reset();
		mSortDispatchCommand.reset();
		mRadixHistogram.reset();
		mSortRecordsB.reset();
		mSortRecordsA.reset();
		mCompactionDispatchCommand.reset();
		mSimulationDispatchCommand.reset();
		mIndirectCommands.reset();
		mCompactionScratch.reset();
		mCounters.reset();
		mRenderIndices.reset();
		mActiveIndicesB.reset();
		mActiveIndicesA.reset();
		mFreeIndices.reset();
		mParticlePool.reset();

		auto releaseProgram = [this](ResourcePtr& program)
		{
			if (!program) return;
			program->release(mwRenderSystem);
			if (!program->isReferenced()) program->destroy();
			program.reset();
		};
		releaseProgram(mPoolInitialiseProgram);
		releaseProgram(mStatisticsPrepareProgram);
		releaseProgram(mEventPrepareProgram);
		releaseProgram(mSpawnProgram);
		releaseProgram(mSimulationPrepareProgram);
		releaseProgram(mSimulationProgram);
		releaseProgram(mEventProcessProgram);
		releaseProgram(mCompactionPrepareProgram);
		releaseProgram(mCompactionCountProgram);
		releaseProgram(mCompactionPrefixProgram);
		releaseProgram(mCompactionScatterProgram);
		releaseProgram(mSortPrepareProgram);
		releaseProgram(mSortKeyProgram);
		releaseProgram(mRadixHistogramProgram);
		releaseProgram(mRadixPrefixProgram);
		releaseProgram(mRadixScatterProgram);
		releaseProgram(mSortFinalizeProgram);
		releaseProgram(mDrawProgram);
		releaseProgram(mWeightedOitDrawProgram);
		releaseProgram(mDistortionDrawProgram);
		releaseProgram(mVolumetricLightingDrawProgram);
		releaseProgram(mMeshCommandProgram);
	}

	void ParticleSystem::disableWithWarning(string const& reason)
	{
		if (!mAvailable) return;
		mwRenderSystem->warnMessage("The particle system is disabled and no particles will be drawn. " + reason);
		mAvailable = false;
	}

	void ParticleSystem::initialise()
	{
		if (mInitialised) return;
		mInitialised = true;

		auto const& caps = mwRenderSystem->getCaps();
		if (!caps.supportsCompute)
		{
			mwRenderSystem->warnMessage("This OpenGL context reports no compute shader support. The particle system is disabled and no particles will be drawn.");
			return;
		}
		if (!caps.supportsMultiDrawIndirect)
		{
			mwRenderSystem->warnMessage("This OpenGL context reports no multi-draw indirect support. The particle system is disabled and no particles will be drawn.");
			return;
		}

		try
		{
			mWorkGroupSize = max(1u, min<uint32_t>(64, min(caps.maxComputeWorkGroupSize[0], caps.maxComputeWorkGroupInvocations)));
			if (caps.maxShaderStorageBufferBindings < RequiredStorageBindings)
			{
				THROW_MPP("The particle system needs " + to_string(RequiredStorageBindings) +
					" shader storage buffer bindings; this context reports " +
					to_string(caps.maxShaderStorageBufferBindings) + ".", __LINE__, __FILE__, __func__);
			}

			auto createComputeProgram = [this](char const* name, char const* source)
			{
				auto stream = make_shared<ComputeProgramStream>(mwResourceManager);
				stream->setSource(RawShaderStage::Compute, source);
				stream->setDefine("MPP_PARTICLE_WORK_GROUP_SIZE", to_string(mWorkGroupSize));
				stream->setDefine("MPP_PARTICLE_MAX_EVENT_RULES", to_string(MaxEventRuleCount));
				stream->setDefine("MPP_PARTICLE_MAX_GENERATED_EVENTS", to_string(MaxGeneratedEventCount));
				stream->setDefine("MPP_PARTICLE_MAX_EXTERNAL_EVENTS", to_string(MaxExternalEventCount));
				auto program = mwResourceManager->declareResource(name, stream).first;
				program->acquire(mwRenderSystem);
				program->load();
				return program;
			};
			mPoolInitialiseProgram = createComputeProgram(PoolInitialiseProgramName, ParticlePoolInitialiseComputeShader);
			mStatisticsPrepareProgram = createComputeProgram("__mpp_particle_statistics_prepare__", ParticleStatisticsPrepareComputeShader);
			mEventPrepareProgram = createComputeProgram(EventPrepareProgramName, ParticleEventPrepareComputeShader);
			mSpawnProgram = createComputeProgram(SpawnProgramName, ParticleSpawnComputeShader);
			mSimulationPrepareProgram = createComputeProgram(SimulationPrepareProgramName, ParticleSimulationPrepareComputeShader);
			mSimulationProgram = createComputeProgram(SimulationProgramName, ParticleSimulationComputeShader);
			mEventProcessProgram = createComputeProgram(EventProcessProgramName, ParticleEventProcessComputeShader);
			mCompactionPrepareProgram = createComputeProgram(CompactionPrepareProgramName, ParticleCompactionPrepareComputeShader);
			mCompactionCountProgram = createComputeProgram(CompactionCountProgramName, ParticleCompactionCountComputeShader);
			mCompactionPrefixProgram = createComputeProgram(CompactionPrefixProgramName, ParticleCompactionPrefixComputeShader);
			mCompactionScatterProgram = createComputeProgram(CompactionScatterProgramName, ParticleCompactionScatterComputeShader);
			mMeshCommandProgram = createComputeProgram(MeshCommandProgramName, ParticleMeshCommandComputeShader);
			createNoiseTexture();

			auto createDrawProgram = [this](char const* name, bool weightedOit, bool distortion)
			{
				auto stream = make_shared<ParticleDrawProgramStream>(mwResourceManager);
				stream->setSource(RawShaderStage::Vertex, ParticleDrawVertexShader);
				stream->setSource(RawShaderStage::Fragment, ParticleDrawFragmentShader);
				stream->setDefine("MPP_PARTICLE_WEIGHTED_OIT", weightedOit ? "1" : "0");
				stream->setDefine("MPP_PARTICLE_DISTORTION", distortion ? "1" : "0");
				auto program = mwResourceManager->declareResource(name, stream).first;
				program->acquire(mwRenderSystem);
				program->load();
				return program;
			};
			mDrawProgram = createDrawProgram(DrawProgramName, false, false);
			mWeightedOitDrawProgram = createDrawProgram(WeightedOitDrawProgramName, true, false);
			mDistortionDrawProgram = createDrawProgram("__mpp_particle_distortion_draw__", false, true);
			auto volumetricStream = make_shared<ParticleDrawProgramStream>(mwResourceManager);
			volumetricStream->setSource(RawShaderStage::Vertex, ParticleVolumetricLightingVertexShader);
			volumetricStream->setSource(RawShaderStage::Fragment, ParticleVolumetricLightingFragmentShader);
			mVolumetricLightingDrawProgram = mwResourceManager->declareResource(
				VolumetricLightingDrawProgramName, volumetricStream).first;
			mVolumetricLightingDrawProgram->acquire(mwRenderSystem);
			mVolumetricLightingDrawProgram->load();

			GL_CHECK(glGenVertexArrays(1, &mVertexArray));
			if (mVertexArray == 0)
				THROW_MPP("Could not create the particle vertex array object.", __LINE__, __FILE__, __func__);

			mAvailable = true;
		}
		catch (exception const& error)
		{
			// A driver may report compute support and still refuse a valid kernel.
			mwRenderSystem->warnMessage(string("The particle system could not be initialised and is disabled; no particles will be drawn. ") + error.what());
			mAvailable = false;
		}
	}

	void ParticleSystem::createNoiseTexture()
	{
		if (mNoiseTexture != 0) return;

		vector<uint8_t> texels(size_t(NoiseTextureSize) * NoiseTextureSize * NoiseTextureSize * 4u);
		auto hashValue = [](uint32_t value)
		{
			value ^= value >> 16u;
			value *= 0x7feb352du;
			value ^= value >> 15u;
			value *= 0x846ca68bu;
			value ^= value >> 16u;
			return value;
		};
		for (size_t texel = 0; texel < texels.size() / 4u; ++texel)
		{
			uint32_t state = hashValue(uint32_t(texel) ^ 0x6d2b79f5u);
			for (size_t channel = 0; channel < 3; ++channel)
			{
				state = hashValue(state + uint32_t(channel));
				texels[texel * 4u + channel] = uint8_t(state >> 24u);
			}
			texels[texel * 4u + 3u] = 255u;
		}

		GL_CHECK(glGenTextures(1, &mNoiseTexture));
		if (mNoiseTexture == 0)
			THROW_MPP("Could not create the particle noise texture.", __LINE__, __FILE__, __func__);
		GL_CHECK(glBindTexture(GL_TEXTURE_3D, mNoiseTexture));
		GL_CHECK(glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR));
		GL_CHECK(glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR));
		GL_CHECK(glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_REPEAT));
		GL_CHECK(glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_REPEAT));
		GL_CHECK(glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_REPEAT));
		GL_CHECK(glTexImage3D(GL_TEXTURE_3D, 0, GL_RGBA8, NoiseTextureSize, NoiseTextureSize, NoiseTextureSize,
			0, GL_RGBA, GL_UNSIGNED_BYTE, texels.data()));
		GL_CHECK(glObjectLabel(GL_TEXTURE, mNoiseTexture, -1, "Particle 3D noise"));
		GL_CHECK(glBindTexture(GL_TEXTURE_3D, 0));
	}

	void ParticleSystem::ensurePoolAllocated()
	{
		if (mPoolAllocated) return;
		if (mEmitters.empty())
			THROW_MPP("A particle pool cannot be allocated before the first emitter is created.", __LINE__, __FILE__, __func__);

		auto const configuredCapacity = mwRenderSystem->getOptions().particlePoolCapacity;
		if (configuredCapacity < MinimumParticlePoolCapacity || configuredCapacity > MaximumParticlePoolCapacity)
		{
			THROW_MPP("Particle pool capacity " + to_string(configuredCapacity) + " is outside the supported range " +
				to_string(MinimumParticlePoolCapacity) + " to " + to_string(MaximumParticlePoolCapacity) + ".",
				__LINE__, __FILE__, __func__);
		}
		mPoolCapacity = configuredCapacity;

		size_t const poolBytes = size_t(mPoolCapacity) * sizeof(ParticleRecord);
		size_t const indexBytes = size_t(mPoolCapacity) * sizeof(uint32_t);
		size_t const counterBytes = sizeof(ParticleCounterHeader) + size_t(MaxTemplateCount) * sizeof(uint32_t);
		size_t const emitterBytes = size_t(MaxEmitterCount) * sizeof(EmitterSimData);
		size_t const templateBytes = size_t(MaxTemplateCount) * sizeof(TemplateRenderData);
		size_t const volumetricLightingBytes = size_t(MaxEmitterCount) * sizeof(ParticleVolumetricLightingGpuData);
		size_t const commandBytes = size_t(MaxSpawnCommandCount) * sizeof(ParticleSpawnCommand);
		size_t const colliderBytes = size_t(MaxColliderCount) * sizeof(ParticleCollider);
		size_t const eventBytes = eventStorageBytes();
		size_t const compactionScratchBytes = size_t(MaxTemplateCount) * 3u * sizeof(uint32_t);
		size_t const indirectCommandBytes = size_t(MaxTemplateCount) * sizeof(ParticleDrawArraysIndirectCommand);
		size_t const largestBlock = max({ poolBytes, indexBytes, counterBytes, emitterBytes, templateBytes, commandBytes,
			colliderBytes, eventBytes, compactionScratchBytes, indirectCommandBytes, volumetricLightingBytes, DispatchCommandBytes });
		if (largestBlock > mwRenderSystem->getCaps().maxShaderStorageBlockSize)
		{
			THROW_MPP("The configured particle buffers need a shader storage block of " + to_string(largestBlock) +
				" bytes, exceeding the GPU maximum of " + to_string(mwRenderSystem->getCaps().maxShaderStorageBlockSize) + " bytes.",
				__LINE__, __FILE__, __func__);
		}

		mParticlePool = make_unique<ShaderStorageBuffer>();
		mParticlePool->create(poolBytes, nullptr, "Particle pool");
		mFreeIndices = make_unique<ShaderStorageBuffer>();
		mFreeIndices->create(indexBytes, nullptr, "Particle free indices");
		mActiveIndicesA = make_unique<ShaderStorageBuffer>();
		mActiveIndicesA->create(indexBytes, nullptr, "Particle active indices A");
		mActiveIndicesB = make_unique<ShaderStorageBuffer>();
		mActiveIndicesB->create(indexBytes, nullptr, "Particle active indices B");
		mRenderIndices = make_unique<ShaderStorageBuffer>();
		mRenderIndices->create(indexBytes, nullptr, "Particle render indices by template");
		mCounters = make_unique<ShaderStorageBuffer>();
		mCounters->create(counterBytes, nullptr, "Particle counters and template live counts");
		mCompactionScratch = make_unique<ShaderStorageBuffer>();
		mCompactionScratch->create(compactionScratchBytes, nullptr, "Particle visible counts, offsets, and scatter cursors");
		mIndirectCommands = make_unique<ShaderStorageBuffer>();
		mIndirectCommands->create(indirectCommandBytes, nullptr, "Particle indirect draw commands by template");
		mSimulationDispatchCommand = make_unique<ShaderStorageBuffer>();
		mSimulationDispatchCommand->create(DispatchCommandBytes, nullptr, "Particle simulation dispatch command");
		mCompactionDispatchCommand = make_unique<ShaderStorageBuffer>();
		mCompactionDispatchCommand->create(DispatchCommandBytes, nullptr, "Particle compaction dispatch command");
		mEventStorage = make_unique<ShaderStorageBuffer>();
		mEventStorage->create(eventBytes, nullptr, "Particle event rules, GPU queues, and external output");
		mEventDispatchCommand = make_unique<ShaderStorageBuffer>();
		mEventDispatchCommand->create(DispatchCommandBytes, nullptr, "Particle event dispatch command");

		GLint storageAlignment = 1;
		GL_CHECK(glGetIntegerv(GL_SHADER_STORAGE_BUFFER_OFFSET_ALIGNMENT, &storageAlignment));
		bool const persistent = mwRenderSystem->getCaps().streamingGeometry;
		mEmitterBuffer = make_unique<detail::PersistentMappedBuffer>();
		mEmitterBuffer->create(GL_SHADER_STORAGE_BUFFER, emitterBytes, max(1, storageAlignment), persistent,
			mEmitters.data(), bytes(mEmitters), "Particle emitter simulation data");
		mTemplateRenderBuffer = make_unique<detail::PersistentMappedBuffer>();
		mTemplateRenderBuffer->create(GL_SHADER_STORAGE_BUFFER, templateBytes, max(1, storageAlignment), persistent,
			mTemplateRenderData.data(), bytes(mTemplateRenderData), "Particle emitter template render data");
		mVolumetricLightingBuffer = make_unique<detail::PersistentMappedBuffer>();
		mVolumetricLightingBuffer->create(GL_SHADER_STORAGE_BUFFER, volumetricLightingBytes, max(1, storageAlignment), persistent,
			mVolumetricLightingGpuData.data(), bytes(mVolumetricLightingGpuData), "Particle emitter volumetric lighting data");
		mSpawnCommandBuffer = make_unique<detail::PersistentMappedBuffer>();
		mSpawnCommandBuffer->create(GL_SHADER_STORAGE_BUFFER, commandBytes, max(1, storageAlignment), persistent,
			mSpawnCommands.data(), bytes(mSpawnCommands), "Particle spawn commands");
		mColliderBuffer = make_unique<detail::PersistentMappedBuffer>();
		mColliderBuffer->create(GL_SHADER_STORAGE_BUFFER, colliderBytes, max(1, storageAlignment), persistent,
			mColliders.data(), bytes(mColliders), "Particle analytical colliders");

		mFreeIndices->bindStorage(FreeIndicesBinding);
		mCounters->bindStorage(CountersBinding);
		auto* initialiseProgram = static_cast<ComputeProgram*>(mPoolInitialiseProgram.get());
		initialiseProgram->use();
		initialiseProgram->setUniform("POOL_CAPACITY", mPoolCapacity);
		initialiseProgram->setUniform("TEMPLATE_CAPACITY", MaxTemplateCount);
		uint32_t const initialisedValues = max(mPoolCapacity, MaxTemplateCount);
		initialiseProgram->dispatch((initialisedValues + mWorkGroupSize - 1) / mWorkGroupSize);
		GL_CHECK(glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_COMMAND_BARRIER_BIT));
		mPoolAllocated = true;
	}

	void ParticleEffectSource::invalidateCurveLut() const
	{
		mCurveLut.reset();
	}

	shared_ptr<ParticleEffectCurveLut> ParticleEffectSource::getCurveLut() const
	{
		if (!mCurveLut) mCurveLut = ParticleEffectCurveLut::bake(getEmitterTemplates());
		return mCurveLut;
	}

	ParticleEmitterHandle ParticleSystem::allocateEmitter(ParticleEmitterTemplate const& emitterTemplate,
		glm::mat4 const& effectTransform, shared_ptr<ParticleEffectCurveLut> const& curveLut, size_t emitterTemplateIndex,
		uint32_t effectIndex)
	{
		uint32_t index;
		if (!mFreeEmitterIndices.empty())
		{
			index = mFreeEmitterIndices.back();
			mFreeEmitterIndices.pop_back();
		}
		else
		{
			if (mEmitterSlots.size() >= MaxEmitterCount)
				THROW_MPP("The particle emitter capacity was exceeded.", __LINE__, __FILE__, __func__);
			index = uint32_t(mEmitterSlots.size());
			mEmitterSlots.emplace_back();
			mEmitters.emplace_back();
			mEmitterEventRules.emplace_back();
			mTemplateRenderData.emplace_back();
			mEmitterLighting.emplace_back();
			mTemplateTextures.emplace_back();
			mTemplateMeshModels.emplace_back();
			mTemplateMeshMaterials.emplace_back();
			mTemplateCurveLuts.emplace_back();
		}

		auto& slot = mEmitterSlots[index];
		slot.occupied = true;
		slot.pendingDestroy = false;
		slot.localTransform = emitterTemplate.localTransform;
		slot.spawnAccumulator = {};
		slot.spawnCounter = 0;
		slot.eventGeneration = slot.generation;
		slot.burstSubmitted = false;
		slot.hasSpawned = false;
		slot.eventTarget = false;
		slot.eventTargetPersistent = false;
		slot.lastSpawnSeconds = mSimulationSeconds;
		slot.maximumSpawnedLifetime = 0.0f;
		slot.effectIndex = effectIndex;
		mEmitters[index] = emitterTemplate.simulation;
		mEmitterEventRules[index] = emitterTemplate.events;
		mEventRulesDirty = true;
		mEmitters[index].emissionState[3] = index;
		setTransform(mEmitters[index], effectTransform * slot.localTransform);
		mTemplateRenderData[index] = emitterTemplate.appearance;
		mEmitterLighting[index] = emitterTemplate.lighting;
		mTemplateRenderData[index].appearance[3] = float(curveLut->getRowOffset(emitterTemplateIndex));
		mTemplateTextures[index] = emitterTemplate.albedoTexture;
		mTemplateMeshModels[index] = emitterTemplate.meshModel;
		mTemplateMeshMaterials[index] = emitterTemplate.meshMaterial;
		mTemplateRenderData[index].sorting[1] = emitterTemplate.meshModel ?
			uint32_t(ParticleRenderMode::Mesh) : uint32_t(ParticleRenderMode::Billboard);
		mTemplateCurveLuts[index] = curveLut;
		return { index, slot.generation };
	}

	ParticleSystem::EmitterSlot* ParticleSystem::findEmitter(ParticleEmitterHandle handle)
	{
		if (!handle || handle.index >= mEmitterSlots.size()) return nullptr;
		auto& slot = mEmitterSlots[handle.index];
		return slot.occupied && !slot.pendingDestroy && slot.generation == handle.generation ? &slot : nullptr;
	}

	ParticleSystem::EmitterSlot const* ParticleSystem::findEmitter(ParticleEmitterHandle handle) const
	{
		if (!handle || handle.index >= mEmitterSlots.size()) return nullptr;
		auto const& slot = mEmitterSlots[handle.index];
		return slot.occupied && !slot.pendingDestroy && slot.generation == handle.generation ? &slot : nullptr;
	}

	ParticleSystem::EffectSlot* ParticleSystem::findEffect(ParticleEffectHandle handle)
	{
		if (!handle || handle.index >= mEffectSlots.size()) return nullptr;
		auto& slot = mEffectSlots[handle.index];
		return slot.occupied && slot.generation == handle.generation ? &slot : nullptr;
	}

	void ParticleSystem::validateLighting(span<ParticleEmitterTemplate const> emitterTemplates) const
	{
		constexpr uint32_t knownFlags = uint32_t(ParticleLightingFlag::ProxyLight) |
			uint32_t(ParticleLightingFlag::PbrLightInjection) |
			uint32_t(ParticleLightingFlag::VolumetricContribution);
		for (auto const& emitterTemplate : emitterTemplates)
		{
			auto const& lighting = emitterTemplate.lighting;
			auto const flags = lighting.flagsAndPadding[0];
			if ((flags & ~knownFlags) != 0u)
				throw invalid_argument("ParticleSystem::createEffect received unknown particle lighting flags.");
			if ((flags & uint32_t(ParticleLightingFlag::PbrLightInjection)) != 0u &&
				(flags & uint32_t(ParticleLightingFlag::ProxyLight)) == 0u)
				throw invalid_argument("Particle PBR light injection requires an emitter-level proxy light.");
			if (!all_of(lighting.colourAndIntensity.begin(), lighting.colourAndIntensity.end(),
				[](float value) { return isfinite(value) && value >= 0.0f; }) ||
				!isfinite(lighting.rangeAndVolumetric[0]) || lighting.rangeAndVolumetric[0] < 0.0f ||
				!isfinite(lighting.rangeAndVolumetric[1]) || lighting.rangeAndVolumetric[1] < 0.0f)
				throw invalid_argument("Particle emitter lighting values must be finite and non-negative.");
			if (flags != 0u && lighting.rangeAndVolumetric[0] <= 0.0f)
				throw invalid_argument("Enabled particle emitter lighting requires a positive range.");
		}
	}

	void ParticleSystem::validateEventRules(span<ParticleEmitterTemplate const> emitterTemplates) const
	{
		size_t totalRules = 0;
		vector<vector<uint32_t>> secondaryEdges(emitterTemplates.size());
		vector<vector<uint32_t>> spawnEdges(emitterTemplates.size());
		for (uint32_t emitterIndex = 0; emitterIndex < emitterTemplates.size(); ++emitterIndex)
		{
			for (auto const& rule : emitterTemplates[emitterIndex].events)
			{
				++totalRules;
				if (uint32_t(rule.trigger) > uint32_t(ParticleEventTrigger::Age) ||
					uint32_t(rule.action) > uint32_t(ParticleEventAction::GameplayCallback))
					throw invalid_argument("ParticleSystem::createEffect received an unknown particle event trigger or action.");
				if (!isfinite(rule.age) || rule.age < 0.0f)
					throw invalid_argument("ParticleSystem::createEffect requires finite, non-negative particle event ages.");
				if (rule.action != ParticleEventAction::SecondaryParticleBurst) continue;
				if (rule.targetEmitterTemplate >= emitterTemplates.size() || rule.count == 0u)
					throw invalid_argument("ParticleSystem::createEffect received an invalid secondary particle burst target or count.");
				secondaryEdges[emitterIndex].push_back(rule.targetEmitterTemplate);
				if (rule.trigger == ParticleEventTrigger::Spawn)
					spawnEdges[emitterIndex].push_back(rule.targetEmitterTemplate);
			}
		}
		if (totalRules > MaxEventRuleCount)
			throw invalid_argument("ParticleSystem::createEffect exceeds MaxEventRuleCount.");

		vector<uint8_t> cycleState(emitterTemplates.size());
		function<void(uint32_t)> rejectCycles = [&](uint32_t emitter)
		{
			if (cycleState[emitter] == 1u)
				throw invalid_argument("ParticleSystem::createEffect does not allow cyclic secondary particle bursts.");
			if (cycleState[emitter] == 2u) return;
			cycleState[emitter] = 1u;
			for (auto target : secondaryEdges[emitter]) rejectCycles(target);
			cycleState[emitter] = 2u;
		};
		for (uint32_t emitter = 0; emitter < emitterTemplates.size(); ++emitter) rejectCycles(emitter);

		vector<uint8_t> depthState(emitterTemplates.size());
		vector<uint32_t> depth(emitterTemplates.size());
		function<uint32_t(uint32_t)> visitDepth = [&](uint32_t emitter)
		{
			if (depthState[emitter] == 2u) return depth[emitter];
			depthState[emitter] = 1u;
			uint32_t maximum = 0u;
			for (auto target : spawnEdges[emitter]) maximum = max(maximum, 1u + visitDepth(target));
			depthState[emitter] = 2u;
			return depth[emitter] = maximum;
		};
		for (uint32_t emitter = 0; emitter < emitterTemplates.size(); ++emitter)
			(void)visitDepth(emitter);
		for (auto const& emitterTemplate : emitterTemplates)
			for (auto const& rule : emitterTemplate.events)
				if (rule.action == ParticleEventAction::SecondaryParticleBurst &&
					1u + depth[rule.targetEmitterTemplate] > MaxSecondaryEventCascadeDepth)
					throw invalid_argument("ParticleSystem::createEffect exceeds MaxSecondaryEventCascadeDepth.");
	}

	ParticleEffectHandle ParticleSystem::createEffect(ResourcePtr const& asset, glm::mat4 const& transform)
	{
		auto const* source = asset ? dynamic_cast<ParticleEffectSource const*>(asset.get()) : nullptr;
		if (!source) throw invalid_argument("ParticleSystem::createEffect requires a particle effect asset.");
		auto handle = createEffect(source->getEmitterTemplates(), transform, source->getBounds(), source->getCurveLut());
		if (auto* effect = findEffect(handle)) effect->asset = asset;
		return handle;
	}

	ParticleEffectHandle ParticleSystem::createEffect(ParticleEffectSource const& asset, glm::mat4 const& transform)
	{
		return createEffect(asset.getEmitterTemplates(), transform, asset.getBounds(), asset.getCurveLut());
	}

	ParticleEffectHandle ParticleSystem::createEffect(span<ParticleEmitterTemplate const> emitterTemplates, glm::mat4 const& transform)
	{
		return createEffect(emitterTemplates, transform, nullopt, ParticleEffectCurveLut::bake(emitterTemplates));
	}

	ParticleEffectHandle ParticleSystem::createEffect(span<ParticleEmitterTemplate const> emitterTemplates,
		ParticleEffectBounds const& bounds, glm::mat4 const& transform)
	{
		return createEffect(emitterTemplates, transform, bounds, ParticleEffectCurveLut::bake(emitterTemplates));
	}

	ParticleEffectHandle ParticleSystem::createEffect(span<ParticleEmitterTemplate const> emitterTemplates,
		glm::mat4 const& transform, optional<ParticleEffectBounds> bounds, shared_ptr<ParticleEffectCurveLut> curveLut)
	{
		if (emitterTemplates.empty()) return {};
		if (bounds && (!isfinite(bounds->center.x) || !isfinite(bounds->center.y) || !isfinite(bounds->center.z) ||
			!isfinite(bounds->size.x) || !isfinite(bounds->size.y) || !isfinite(bounds->size.z) ||
			bounds->size.x <= 0.0f || bounds->size.y <= 0.0f || bounds->size.z <= 0.0f))
			throw invalid_argument("ParticleSystem::createEffect requires finite bounds with a strictly positive size.");
		validateEventRules(emitterTemplates);
		validateLighting(emitterTemplates);
		size_t eventRuleCount = 0u;
		for (uint32_t index = 0; index < mEmitterSlots.size(); ++index)
			if (mEmitterSlots[index].occupied) eventRuleCount += mEmitterEventRules[index].size();
		for (auto const& emitterTemplate : emitterTemplates) eventRuleCount += emitterTemplate.events.size();
		if (eventRuleCount > MaxEventRuleCount)
			THROW_MPP("The particle event-rule capacity was exceeded.", __LINE__, __FILE__, __func__);
		if (emitterTemplates.size() > MaxEmitterCount - getLiveEmitterCount())
			THROW_MPP("The particle emitter capacity was exceeded.", __LINE__, __FILE__, __func__);

		uint32_t effectIndex;
		if (!mFreeEffectIndices.empty())
		{
			effectIndex = mFreeEffectIndices.back();
			mFreeEffectIndices.pop_back();
		}
		else
		{
			effectIndex = uint32_t(mEffectSlots.size());
			mEffectSlots.emplace_back();
		}
		auto& effect = mEffectSlots[effectIndex];
		effect.occupied = true;
		effect.transform = transform;
		effect.bounds = std::move(bounds);
		effect.boundsCullingEnabled = true;
		effect.emitters.clear();
		effect.emitters.reserve(emitterTemplates.size());
		effect.asset.reset();
		effect.curveLut = std::move(curveLut);

		try
		{
			for (size_t templateIndex = 0; templateIndex < emitterTemplates.size(); ++templateIndex)
				effect.emitters.push_back(allocateEmitter(emitterTemplates[templateIndex], transform,
					effect.curveLut, templateIndex, effectIndex));

			float maximumLifetime = 0.0f;
			for (auto const& emitterTemplate : emitterTemplates)
				maximumLifetime = max(maximumLifetime, max(0.0f, emitterTemplate.simulation.lifetimeSizeRanges[1]) *
					max(0.0f, emitterTemplate.simulation.parameterMultipliers0[3]));
			float const eventRetention = maximumLifetime * float(MaxSecondaryEventCascadeDepth + 1u);
			vector<bool> isEventTarget(emitterTemplates.size());
			for (auto const& emitterTemplate : emitterTemplates)
				for (auto const& rule : emitterTemplate.events)
					if (rule.action == ParticleEventAction::SecondaryParticleBurst)
						isEventTarget[rule.targetEmitterTemplate] = true;
			bool hasPotentialContinuousRoot = false;
			for (size_t index = 0; index < emitterTemplates.size(); ++index)
			{
				auto const& simulation = emitterTemplates[index].simulation;
				if (simulation.emissionState[0] == 0u && (simulation.emissionState[1] != 0u || !isEventTarget[index]))
				{
					hasPotentialContinuousRoot = true;
					break;
				}
			}
			for (size_t source = 0; source < effect.emitters.size(); ++source)
			{
				auto sourceIndex = effect.emitters[source].index;
				for (auto& rule : mEmitterEventRules[sourceIndex])
				{
					if (rule.action != ParticleEventAction::SecondaryParticleBurst) continue;
					auto targetIndex = effect.emitters[rule.targetEmitterTemplate].index;
					auto& targetSlot = mEmitterSlots[targetIndex];
					rule.targetEmitterTemplate = targetIndex;
					rule.targetEmitterGeneration = targetSlot.eventGeneration;
					targetSlot.eventTarget = true;
					targetSlot.eventTargetPersistent = hasPotentialContinuousRoot;
					targetSlot.hasSpawned = true;
					targetSlot.maximumSpawnedLifetime = max(targetSlot.maximumSpawnedLifetime, eventRetention);
				}
			}
		}
		catch (...)
		{
			for (auto emitter : effect.emitters) reclaimEmitter(emitter.index);
			effect.occupied = false;
			effect.bounds.reset();
			effect.curveLut.reset();
			mFreeEffectIndices.push_back(effectIndex);
			throw;
		}
		invalidateViewBounds();
		return { effectIndex, effect.generation };
	}

	void ParticleSystem::destroyEffect(ParticleEffectHandle handle)
	{
		auto* effect = findEffect(handle);
		if (!effect) return;
		for (auto emitter : effect->emitters) requestEmitterDestroy(emitter.index);
		reclaimEffect(handle.index);
	}

	void ParticleSystem::setEffectTransform(ParticleEffectHandle handle, glm::mat4 const& transform)
	{
		auto* effect = findEffect(handle);
		if (!effect) return;
		effect->transform = transform;
		invalidateViewBounds();
		for (auto emitter : effect->emitters)
		{
			auto const* slot = findEmitter(emitter);
			if (slot) setTransform(mEmitters[emitter.index], transform * slot->localTransform);
		}
	}

	void ParticleSystem::setEffectBoundsCullingEnabled(ParticleEffectHandle handle, bool enabled)
	{
		auto* effect = findEffect(handle);
		if (!effect || effect->boundsCullingEnabled == enabled) return;
		effect->boundsCullingEnabled = enabled;
		invalidateViewBounds();
	}

	bool ParticleSystem::isEffectBoundsCullingEnabled(ParticleEffectHandle handle) const
	{
		if (!handle || handle.index >= mEffectSlots.size()) return false;
		auto const& effect = mEffectSlots[handle.index];
		return effect.occupied && effect.generation == handle.generation && effect.boundsCullingEnabled;
	}

	optional<ParticleEffectBounds> ParticleSystem::sampleObservedEffectBounds(ParticleEffectHandle handle) const
	{
		if (!handle || handle.index >= mEffectSlots.size() || !mPoolAllocated) return nullopt;
		auto const& effect = mEffectSlots[handle.index];
		if (!effect.occupied || effect.generation != handle.generation || effect.emitters.empty()) return nullopt;
		float const determinant = glm::determinant(effect.transform);
		if (!isfinite(determinant) || abs(determinant) < 0.000001f) return nullopt;

		GL_CHECK(glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT | GL_SHADER_STORAGE_BARRIER_BIT));
		ParticleCounterHeader counters{};
		GL_CHECK(glBindBuffer(GL_SHADER_STORAGE_BUFFER, mCounters->getBuffer()));
		GL_CHECK(glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(counters), &counters));
		uint32_t const reportedActiveCount = mActiveListIndex == 0u ? counters.activeCountA : counters.activeCountB;
		uint32_t const activeCount = min(reportedActiveCount, mPoolCapacity);
		if (activeCount == 0u) { GL_CHECK(glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0)); return nullopt; }
		vector<uint32_t> activeIndices(activeCount);
		auto const* active = mActiveListIndex == 0u ? mActiveIndicesA.get() : mActiveIndicesB.get();
		GL_CHECK(glBindBuffer(GL_SHADER_STORAGE_BUFFER, active->getBuffer()));
		GL_CHECK(glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0,
			GLsizeiptr(activeIndices.size() * sizeof(uint32_t)), activeIndices.data()));

		vector<uint8_t> included(mEmitterSlots.size(), 0u);
		for (auto emitter : effect.emitters) if (emitter.index < included.size()) included[emitter.index] = 1u;
		GL_CHECK(glBindBuffer(GL_SHADER_STORAGE_BUFFER, mParticlePool->getBuffer()));
		auto const* records = static_cast<ParticleRecord const*>(glMapBufferRange(GL_SHADER_STORAGE_BUFFER, 0,
			GLsizeiptr(mParticlePool->getSize()), GL_MAP_READ_BIT));
		CheckOpenGLError("glMapBufferRange observed particle bounds", __LINE__, __FILE__, __func__);
		if (!records) { GL_CHECK(glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0)); return nullopt; }

		glm::vec3 minimum(numeric_limits<float>::max()), maximum(numeric_limits<float>::lowest());
		bool found = false;
		auto const inverseEffect = glm::inverse(effect.transform);
		for (uint32_t particleIndex : activeIndices)
		{
			if (particleIndex >= mPoolCapacity) continue;
			auto const& particle = records[particleIndex];
			if (particle.emitterIndex >= included.size() || !included[particle.emitterIndex]) continue;
			auto const emitterIndex = particle.emitterIndex;
			float normalizedLife = particle.velocityLifetime[3] > 0.0f ?
				clamp(particle.positionAge[3] / particle.velocityLifetime[3], 0.0f, 1.0f) : 0.0f;
			float sizeCurve = 1.0f;
			auto const& lut = mTemplateCurveLuts[emitterIndex];
			if (lut)
			{
				auto const& texels = lut->getFloatTexels();
				uint32_t const row = uint32_t(max(0.0f, mTemplateRenderData[emitterIndex].appearance[3]));
				float sample = normalizedLife * float(ParticleEffectCurveLut::SampleCount - 1u);
				uint32_t left = uint32_t(sample), right = min(left + 1u, ParticleEffectCurveLut::SampleCount - 1u);
				float amount = sample - float(left);
				size_t leftOffset = (size_t(row) * ParticleEffectCurveLut::SampleCount + left) * 4u;
				size_t rightOffset = (size_t(row) * ParticleEffectCurveLut::SampleCount + right) * 4u;
				if (rightOffset < texels.size()) sizeCurve = texels[leftOffset] + (texels[rightOffset] - texels[leftOffset]) * amount;
			}
			float radius = abs(particle.baseSize * sizeCurve);
			auto const& appearance = mTemplateRenderData[emitterIndex];
			if (particleTemplateRendersMesh(appearance)) radius *= max(appearance.culling[2], 1.0f);
			else if (appearance.modes[2] == uint32_t(ParticleBillboardMode::VelocityStretched))
			{
				float const stretch = 1.0f + glm::length(glm::vec3(particle.velocityLifetime[0],
					particle.velocityLifetime[1], particle.velocityLifetime[2]));
				radius *= sqrt(1.0f + stretch * stretch);
			}
			else radius *= sqrt(2.0f);
			auto local = glm::vec3(inverseEffect * glm::vec4(particle.positionAge[0], particle.positionAge[1], particle.positionAge[2], 1.0f));
			glm::vec3 localRadius(radius * glm::length(glm::vec3(inverseEffect[0][0], inverseEffect[1][0], inverseEffect[2][0])),
				radius * glm::length(glm::vec3(inverseEffect[0][1], inverseEffect[1][1], inverseEffect[2][1])),
				radius * glm::length(glm::vec3(inverseEffect[0][2], inverseEffect[1][2], inverseEffect[2][2])));
			if (!isfinite(local.x) || !isfinite(local.y) || !isfinite(local.z) ||
				!isfinite(localRadius.x) || !isfinite(localRadius.y) || !isfinite(localRadius.z)) continue;
			minimum = glm::min(minimum, local - localRadius);
			maximum = glm::max(maximum, local + localRadius);
			found = true;
		}
		GL_CHECK(glUnmapBuffer(GL_SHADER_STORAGE_BUFFER));
		GL_CHECK(glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0));
		if (!found) return nullopt;
		return ParticleEffectBounds{ (minimum + maximum) * 0.5f, maximum - minimum };
	}

	void ParticleSystem::setEffectVisibilityFlags(ParticleEffectHandle handle, uint32_t flags)
	{
		auto* effect = findEffect(handle);
		if (!effect) return;
		for (auto emitter : effect->emitters)
			if (findEmitter(emitter)) mEmitters[emitter.index].emissionRateAndPadding[1] = float(flags);
	}

	void ParticleSystem::setEffectVisible(ParticleEffectHandle handle, bool visible)
	{
		setEffectVisibilityFlags(handle, visible ? uint32_t(ParticleEffectVisibilityFlag::Visible) : 0u);
	}

	void ParticleSystem::spawnEffect(ResourcePtr const& asset, glm::mat4 const& transform)
	{
		(void)createEffect(asset, transform);
	}

	void ParticleSystem::spawnEffect(ParticleEffectSource const& asset, glm::mat4 const& transform)
	{
		(void)createEffect(asset, transform);
	}

	void ParticleSystem::spawnEffect(span<ParticleEmitterTemplate const> emitterTemplates, glm::mat4 const& transform)
	{
		(void)createEffect(emitterTemplates, transform);
	}

	void ParticleSystem::spawnEffect(span<ParticleEmitterTemplate const> emitterTemplates,
		ParticleEffectBounds const& bounds, glm::mat4 const& transform)
	{
		(void)createEffect(emitterTemplates, bounds, transform);
	}

	ParticleEmitterHandle ParticleSystem::getEmitter(ParticleEffectHandle handle, size_t emitterIndex) const
	{
		if (!handle || handle.index >= mEffectSlots.size()) return {};
		auto const& effect = mEffectSlots[handle.index];
		if (!effect.occupied || effect.generation != handle.generation || emitterIndex >= effect.emitters.size()) return {};
		auto const emitter = effect.emitters[emitterIndex];
		return findEmitter(emitter) ? emitter : ParticleEmitterHandle{};
	}

	void ParticleSystem::requestEmitterDestroy(uint32_t index)
	{
		if (index >= mEmitterSlots.size()) return;
		auto& slot = mEmitterSlots[index];
		if (!slot.occupied || slot.pendingDestroy) return;
		slot.pendingDestroy = true;
		slot.generation = nextGeneration(slot.generation);
		mEventRulesDirty = true;
		if (slot.eventTarget) slot.lastSpawnSeconds = mSimulationSeconds;
		mEmitters[index].emissionState[1] = 0u;
		if (!slot.hasSpawned) reclaimEmitter(index);
	}

	void ParticleSystem::destroyEmitter(ParticleEmitterHandle handle)
	{
		if (!findEmitter(handle)) return;
		requestEmitterDestroy(handle.index);
	}

	void ParticleSystem::setEmitterTransform(ParticleEmitterHandle handle, glm::mat4 const& transform)
	{
		if (!findEmitter(handle)) return;
		setTransform(mEmitters[handle.index], transform);
	}

	void ParticleSystem::setEmitterParameter(ParticleEmitterHandle handle, ParticleParameter parameter, float multiplier)
	{
		if (!findEmitter(handle)) return;
		multiplier = max(0.0f, multiplier);
		auto& emitter = mEmitters[handle.index];
		switch (parameter)
		{
		case ParticleParameter::SpawnRate: emitter.parameterMultipliers0[0] = multiplier; break;
		case ParticleParameter::SizeScale: emitter.parameterMultipliers0[1] = multiplier; break;
		case ParticleParameter::SpeedScale: emitter.parameterMultipliers0[2] = multiplier; break;
		case ParticleParameter::LifetimeScale: emitter.parameterMultipliers0[3] = multiplier; break;
		case ParticleParameter::AlphaScale: emitter.parameterMultipliers1[0] = multiplier; break;
		case ParticleParameter::EmissiveScale: emitter.parameterMultipliers1[1] = multiplier; break;
		}
	}

	void ParticleSystem::requestEmitterBurst(ParticleEmitterHandle handle, uint32_t count)
	{
		auto* slot = findEmitter(handle);
		if (!slot || count == 0u) return;
		auto queued = find_if(mSpawnCommands.begin(), mSpawnCommands.end(), [handle](auto const& command)
			{ return command.emitterIndex == handle.index; });
		if (queued == mSpawnCommands.end())
			mSpawnCommands.push_back({ handle.index, count, 0x6d2b79f5u + handle.index, slot->spawnCounter });
		else queued->count += count;
		slot->spawnCounter += count;
		slot->hasSpawned = true;
		slot->lastSpawnSeconds = mSimulationSeconds;
		slot->maximumSpawnedLifetime = max(slot->maximumSpawnedLifetime,
			max(0.0f, mEmitters[handle.index].lifetimeSizeRanges[1]) *
			max(0.0f, mEmitters[handle.index].parameterMultipliers0[3]));
	}

	void ParticleSystem::updateEmitterTemplateRuntime(ParticleEmitterHandle handle,
		EmitterSimData const& simulation, TemplateRenderData const& appearance)
	{
		auto* slot = findEmitter(handle);
		if (!slot) return;
		auto& currentSimulation = mEmitters[handle.index];
		auto const transform = currentSimulation.transform;
		auto const templateIndex = currentSimulation.emissionState[3];
		auto const visibility = currentSimulation.emissionRateAndPadding[1];
		auto const multipliers0 = currentSimulation.parameterMultipliers0;
		auto const multipliers1 = currentSimulation.parameterMultipliers1;
		auto const eventRange = currentSimulation.eventRange;
		bool const resetBurst = currentSimulation.emissionState[0] != simulation.emissionState[0] ||
			currentSimulation.emissionState[2] != simulation.emissionState[2];
		currentSimulation = simulation;
		currentSimulation.transform = transform;
		currentSimulation.emissionState[3] = templateIndex;
		currentSimulation.emissionRateAndPadding[1] = visibility;
		currentSimulation.parameterMultipliers0 = multipliers0;
		currentSimulation.parameterMultipliers1 = multipliers1;
		currentSimulation.eventRange = eventRange;
		if (resetBurst) slot->burstSubmitted = false;

		auto& currentAppearance = mTemplateRenderData[handle.index];
		auto const textureLayer = currentAppearance.textureAndAtlas[0];
		auto const textureFlags = currentAppearance.textureAndAtlas[1];
		auto const curveRow = currentAppearance.appearance[3];
		auto const meshBounds = currentAppearance.culling[2];
		auto const renderMode = currentAppearance.sorting[1];
		currentAppearance = appearance;
		currentAppearance.textureAndAtlas[0] = textureLayer;
		currentAppearance.textureAndAtlas[1] = textureFlags;
		currentAppearance.appearance[3] = curveRow;
		currentAppearance.culling[2] = meshBounds;
		currentAppearance.sorting[1] = renderMode;
	}

	void ParticleSystem::setColliders(span<ParticleCollider const> colliders)
	{
		if (colliders.size() > MaxColliderCount)
			throw invalid_argument("ParticleSystem::setColliders exceeds MaxColliderCount.");
		for (auto const& collider : colliders)
			if (collider.shapeAndPadding[0] > uint32_t(ParticleColliderShape::Capsule))
				throw invalid_argument("ParticleSystem::setColliders received an unknown analytical collider shape.");
		mColliders.assign(colliders.begin(), colliders.end());
	}

	void ParticleSystem::setSignedDistanceField(ResourcePtr texture, glm::mat4 const& worldToTexture,
		float distanceScale, float isoValue)
	{
		if (!texture)
		{
			clearSignedDistanceField();
			return;
		}
		if (!isfinite(distanceScale) || distanceScale <= 0.0f || !isfinite(isoValue))
			throw invalid_argument("ParticleSystem::setSignedDistanceField requires a finite positive distance scale and finite iso value.");
		mSignedDistanceFieldTexture = std::move(texture);
		copy_n(glm::value_ptr(worldToTexture), mSignedDistanceFieldData.worldToTexture.size(),
			mSignedDistanceFieldData.worldToTexture.begin());
		mSignedDistanceFieldData.parameters = { distanceScale, isoValue, 1.0f, 0.0f };
	}

	void ParticleSystem::clearSignedDistanceField()
	{
		mSignedDistanceFieldTexture.reset();
		mSignedDistanceFieldData = {};
	}

	void ParticleSystem::setVectorField(ResourcePtr texture)
	{
		mVectorFieldTexture = std::move(texture);
	}

	void ParticleSystem::clearVectorField()
	{
		mVectorFieldTexture.reset();
	}

	void ParticleSystem::setScreenSpaceCollisionDepth(ResourcePtr sceneDepth)
	{
		if (sceneDepth && !dynamic_cast<RenderTexture*>(sceneDepth.get()))
			throw invalid_argument("ParticleSystem::setScreenSpaceCollisionDepth requires a render texture.");
		mScreenSpaceCollisionDepth = std::move(sceneDepth);
	}

	void ParticleSystem::startEmitter(ParticleEmitterHandle handle)
	{
		if (!findEmitter(handle)) return;
		mEmitters[handle.index].emissionState[1] = 1u;
	}

	void ParticleSystem::stopEmitter(ParticleEmitterHandle handle)
	{
		if (!findEmitter(handle)) return;
		mEmitters[handle.index].emissionState[1] = 0u;
	}

	bool ParticleSystem::isAlive(ParticleEffectHandle handle) const
	{
		if (!handle || handle.index >= mEffectSlots.size()) return false;
		auto const& effect = mEffectSlots[handle.index];
		return effect.occupied && effect.generation == handle.generation;
	}

	size_t ParticleSystem::getLiveEmitterCount() const
	{
		return count_if(mEmitterSlots.begin(), mEmitterSlots.end(), [](auto const& slot) { return slot.occupied; });
	}

	size_t ParticleSystem::getLiveEffectCount() const
	{
		return count_if(mEffectSlots.begin(), mEffectSlots.end(), [](auto const& slot) { return slot.occupied; });
	}

	bool ParticleSystem::hasOccupiedEmitters() const
	{
		return any_of(mEmitterSlots.begin(), mEmitterSlots.end(), [](auto const& slot) { return slot.occupied; });
	}

	void ParticleSystem::invalidateViewBounds()
	{
		mViewBoundsValid = false;
	}

	void ParticleSystem::updateViewEffectSubmissions() const
	{
		auto const frame = mwRenderSystem ? mwRenderSystem->getFrameSerial() : 0u;
		auto const viewProjection = mwRenderSystem ?
			mwRenderSystem->mCameraFrameProjection * mwRenderSystem->mCameraFrameView : glm::mat4(1.0f);
		bool const current = mViewBoundsValid && mViewBoundsFrame == frame &&
			std::memcmp(glm::value_ptr(mViewBoundsViewProjection), glm::value_ptr(viewProjection), sizeof(glm::mat4)) == 0;
		if (!current)
		{
			mViewBoundsFrame = frame;
			mViewBoundsViewProjection = viewProjection;
			mViewBoundsValid = true;
			mViewEffectSubmissions.assign(mEffectSlots.size(), 0u);
			for (uint32_t index = 0; index < mEffectSlots.size(); ++index)
			{
				auto const& effect = mEffectSlots[index];
				if (!effect.occupied) continue;
				mViewEffectSubmissions[index] = !mwRenderSystem || !effect.boundsCullingEnabled || !effect.bounds ||
					particleEffectBoundsIntersectFrustum(*effect.bounds, effect.transform, viewProjection) ? 1u : 0u;
			}
		}

		if (!mStatistics->enabled || mStatistics->currentSlot < 0) return;
		auto& slot = mStatistics->slots[size_t(mStatistics->currentSlot)];
		if (!slot.submitted || slot.sourceFrame != frame) return;
		uint64_t viewKey = 1469598103934665603ull;
		auto const* bytes = reinterpret_cast<uint8_t const*>(glm::value_ptr(viewProjection));
		for (size_t index = 0; index < sizeof(glm::mat4); ++index)
		{
			viewKey ^= bytes[index];
			viewKey *= 1099511628211ull;
		}
		if (find(slot.recordedViews.begin(), slot.recordedViews.end(), viewKey) != slot.recordedViews.end()) return;
		slot.recordedViews.push_back(viewKey);
		for (uint32_t index = 0; index < mEffectSlots.size(); ++index)
		{
			if (!mEffectSlots[index].occupied) continue;
			if (index < mViewEffectSubmissions.size() && mViewEffectSubmissions[index]) ++slot.submittedEffects;
			else ++slot.boundsCulledEffects;
		}
	}

	bool ParticleSystem::isEmitterSubmittedForCurrentView(uint32_t emitterIndex) const
	{
		if (emitterIndex >= mEmitterSlots.size()) return false;
		auto const& emitter = mEmitterSlots[emitterIndex];
		if (!emitter.occupied || emitter.effectIndex >= mEffectSlots.size()) return false;
		updateViewEffectSubmissions();
		return emitter.effectIndex < mViewEffectSubmissions.size() && mViewEffectSubmissions[emitter.effectIndex] != 0u;
	}

	void ParticleSystem::reclaimEmitter(uint32_t index)
	{
		if (index >= mEmitterSlots.size()) return;
		auto& slot = mEmitterSlots[index];
		if (!slot.occupied) return;
		slot.occupied = false;
		slot.pendingDestroy = false;
		slot.generation = nextGeneration(slot.generation);
		slot.effectIndex = ParticleEffectHandle::InvalidIndex;
		mEmitters[index] = {};
		mEmitterEventRules[index].clear();
		mEventRulesDirty = true;
		mEmitters[index].emissionState[1] = 0u;
		mEmitters[index].emissionState[3] = index;
		mTemplateTextures[index].reset();
		mTemplateMeshModels[index].reset();
		mTemplateMeshMaterials[index].reset();
		mTemplateCurveLuts[index].reset();
		mTemplateRenderData[index] = {};
		mEmitterLighting[index] = {};
		mFreeEmitterIndices.push_back(index);
	}

	void ParticleSystem::reclaimEffect(uint32_t index)
	{
		if (index >= mEffectSlots.size()) return;
		auto& effect = mEffectSlots[index];
		if (!effect.occupied) return;
		effect.occupied = false;
		effect.generation = nextGeneration(effect.generation);
		effect.emitters.clear();
		effect.bounds.reset();
		effect.asset.reset();
		effect.curveLut.reset();
		mFreeEffectIndices.push_back(index);
		invalidateViewBounds();
	}

	void ParticleSystem::retireCompletedEmitters()
	{
		for (uint32_t index = 0; index < mEmitterSlots.size(); ++index)
		{
			auto const& emitter = mEmitters[index];
			auto& slot = mEmitterSlots[index];
			if (!slot.occupied) continue;
			bool const oneShotComplete = !slot.eventTargetPersistent &&
				((emitter.emissionState[0] != 0u && (slot.burstSubmitted || emitter.emissionState[1] == 0u)) ||
					(slot.eventTarget && emitter.emissionState[1] == 0u));
			if (!(slot.pendingDestroy || oneShotComplete)) continue;
			if (!slot.hasSpawned || mSimulationSeconds - slot.lastSpawnSeconds >= slot.maximumSpawnedLifetime)
				reclaimEmitter(index);
		}

		for (uint32_t index = 0; index < mEffectSlots.size(); ++index)
		{
			auto const& effect = mEffectSlots[index];
			if (!effect.occupied) continue;
			bool const allRetired = all_of(effect.emitters.begin(), effect.emitters.end(), [this](ParticleEmitterHandle emitter)
				{ return !findEmitter(emitter); });
			if (allRetired) reclaimEffect(index);
		}
	}

	void ParticleSystem::buildSpawnCommands(float dt)
	{
		if (mEmitterSlots.size() != mEmitters.size())
			THROW_MPP("Particle emitter slots do not match the emitter table.", __LINE__, __FILE__, __func__);

		for (uint32_t emitterIndex = 0; emitterIndex < uint32_t(mEmitters.size()); ++emitterIndex)
		{
			auto const& emitter = mEmitters[emitterIndex];
			auto& slot = mEmitterSlots[emitterIndex];
			if (!slot.occupied || slot.pendingDestroy || emitter.emissionState[1] == 0u) continue;

			uint32_t spawnCount = 0;
			if (emitter.emissionState[0] == 0u)
			{
				spawnCount = slot.spawnAccumulator.accumulate(
					emitter.emissionRateAndPadding[0], emitter.parameterMultipliers0[0], dt);
			}
			else if (!slot.burstSubmitted)
			{
				spawnCount = emitter.emissionState[2];
				slot.burstSubmitted = true;
			}

			if (spawnCount == 0u) continue;
			auto queued = find_if(mSpawnCommands.begin(), mSpawnCommands.end(), [emitterIndex](auto const& command)
				{ return command.emitterIndex == emitterIndex; });
			if (queued == mSpawnCommands.end())
				mSpawnCommands.push_back({ emitterIndex, spawnCount, 0x9e3779b9u + emitterIndex, slot.spawnCounter });
			else queued->count += spawnCount;
			slot.spawnCounter += spawnCount;
			slot.hasSpawned = true;
			slot.lastSpawnSeconds = mSimulationSeconds;
			slot.maximumSpawnedLifetime = max(slot.maximumSpawnedLifetime,
				max(0.0f, emitter.lifetimeSizeRanges[1]) * max(0.0f, emitter.parameterMultipliers0[3]));
		}
	}

	void ParticleSystem::updateAlbedoTextureArray()
	{
		vector<uint32_t> sourceIds(mTemplateTextures.size(), 0u);
		vector<Texture*> uniqueTextures;
		for (uint32_t index = 0; index < mTemplateTextures.size(); ++index)
		{
			auto const& resource = mTemplateTextures[index];
			if (!resource)
			{
				mTemplateRenderData[index].textureAndAtlas[0] = 0u;
				mTemplateRenderData[index].textureAndAtlas[1] = 0u;
				continue;
			}
			resource->load();
			auto* texture = dynamic_cast<Texture*>(resource.get());
			if (!texture || texture->getTextureTarget() != GL_TEXTURE_2D)
				THROW_MPP("A particle albedo atlas must be a 2D texture.", __LINE__, __FILE__, __func__);
			sourceIds[index] = texture->getId();
			auto found = find_if(uniqueTextures.begin(), uniqueTextures.end(), [texture](Texture const* candidate)
				{ return candidate->getId() == texture->getId(); });
			uint32_t layer;
			if (found == uniqueTextures.end())
			{
				uniqueTextures.push_back(texture);
				layer = uint32_t(uniqueTextures.size());
			}
			else layer = uint32_t(distance(uniqueTextures.begin(), found)) + 1u;
			bool const srgb = texture->getInternalFormat() == GL_SRGB8 || texture->getInternalFormat() == GL_SRGB8_ALPHA8;
			mTemplateRenderData[index].textureAndAtlas[0] = layer;
			mTemplateRenderData[index].textureAndAtlas[1] = 1u | (srgb ? 2u : 0u);
		}
		if (sourceIds == mAlbedoArraySourceIds && mAlbedoArrayTexture != 0u) return;

		GLint maximumLayers = 0;
		GL_CHECK(glGetIntegerv(GL_MAX_ARRAY_TEXTURE_LAYERS, &maximumLayers));
		if (uniqueTextures.size() + 1u > size_t(maximumLayers))
			THROW_MPP("The particle albedo texture-array layer limit was exceeded.", __LINE__, __FILE__, __func__);

		size_t width = 1u, height = 1u;
		for (auto const* texture : uniqueTextures)
		{
			width = max(width, texture->getWidth());
			height = max(height, texture->getHeight());
		}
		if (width > size_t(mwRenderSystem->getCaps().maxTextureSize) ||
			height > size_t(mwRenderSystem->getCaps().maxTextureSize))
			THROW_MPP("A particle albedo texture exceeds the shared array dimensions supported by the GPU.", __LINE__, __FILE__, __func__);

		uint32_t mipLevels = 1u;
		for (size_t dimension = max(width, height); dimension > 1u; dimension >>= 1u) ++mipLevels;
		GLuint arrayTexture = 0u, readFramebuffer = 0u, drawFramebuffer = 0u;
		GLint previousReadFramebuffer = 0, previousDrawFramebuffer = 0, previousArrayBinding = 0;
		GLfloat previousClearColour[4]{};
		GL_CHECK(glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &previousReadFramebuffer));
		GL_CHECK(glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &previousDrawFramebuffer));
		GL_CHECK(glGetIntegerv(GL_TEXTURE_BINDING_2D_ARRAY, &previousArrayBinding));
		GL_CHECK(glGetFloatv(GL_COLOR_CLEAR_VALUE, previousClearColour));
		GLboolean const framebufferSrgb = glIsEnabled(GL_FRAMEBUFFER_SRGB);
		GL_CHECK(glDisable(GL_FRAMEBUFFER_SRGB));

		GL_CHECK(glGenTextures(1, &arrayTexture));
		GL_CHECK(glBindTexture(GL_TEXTURE_2D_ARRAY, arrayTexture));
		GL_CHECK(glTexStorage3D(GL_TEXTURE_2D_ARRAY, GLsizei(mipLevels), GL_RGBA8,
			GLsizei(width), GLsizei(height), GLsizei(uniqueTextures.size() + 1u)));
		GL_CHECK(glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR));
		GL_CHECK(glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR));
		GL_CHECK(glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE));
		GL_CHECK(glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE));
		GL_CHECK(glObjectLabel(GL_TEXTURE, arrayTexture, -1, "Particle shared albedo array"));
		GL_CHECK(glGenFramebuffers(1, &readFramebuffer));
		GL_CHECK(glGenFramebuffers(1, &drawFramebuffer));
		GL_CHECK(glBindFramebuffer(GL_DRAW_FRAMEBUFFER, drawFramebuffer));
		GL_CHECK(glFramebufferTextureLayer(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, arrayTexture, 0, 0));
		GL_CHECK(glDrawBuffer(GL_COLOR_ATTACHMENT0));
		GL_CHECK(glClearColor(1.0f, 1.0f, 1.0f, 1.0f));
		GL_CHECK(glClear(GL_COLOR_BUFFER_BIT));

		for (size_t textureIndex = 0; textureIndex < uniqueTextures.size(); ++textureIndex)
		{
			auto const* texture = uniqueTextures[textureIndex];
			GL_CHECK(glBindFramebuffer(GL_READ_FRAMEBUFFER, readFramebuffer));
			GL_CHECK(glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
				GL_TEXTURE_2D, texture->getId(), 0));
			GL_CHECK(glReadBuffer(GL_COLOR_ATTACHMENT0));
			if (glCheckFramebufferStatus(GL_READ_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
				THROW_MPP("Particle albedo texture '" + texture->getName() + "' cannot be copied into the shared array.",
					__LINE__, __FILE__, __func__);
			GL_CHECK(glBindFramebuffer(GL_DRAW_FRAMEBUFFER, drawFramebuffer));
			GL_CHECK(glFramebufferTextureLayer(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
				arrayTexture, 0, GLint(textureIndex + 1u)));
			if (glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
				THROW_MPP("The particle albedo texture array is not framebuffer complete.", __LINE__, __FILE__, __func__);
			GL_CHECK(glBlitFramebuffer(0, 0, GLsizei(texture->getWidth()), GLsizei(texture->getHeight()),
				0, 0, GLsizei(width), GLsizei(height), GL_COLOR_BUFFER_BIT, GL_LINEAR));
		}
		GL_CHECK(glBindTexture(GL_TEXTURE_2D_ARRAY, arrayTexture));
		GL_CHECK(glGenerateMipmap(GL_TEXTURE_2D_ARRAY));
		GL_CHECK(glBindFramebuffer(GL_READ_FRAMEBUFFER, GLuint(previousReadFramebuffer)));
		GL_CHECK(glBindFramebuffer(GL_DRAW_FRAMEBUFFER, GLuint(previousDrawFramebuffer)));
		GL_CHECK(glBindTexture(GL_TEXTURE_2D_ARRAY, GLuint(previousArrayBinding)));
		GL_CHECK(glClearColor(previousClearColour[0], previousClearColour[1], previousClearColour[2], previousClearColour[3]));
		if (framebufferSrgb) GL_CHECK(glEnable(GL_FRAMEBUFFER_SRGB));
		GL_CHECK(glDeleteFramebuffers(1, &readFramebuffer));
		GL_CHECK(glDeleteFramebuffers(1, &drawFramebuffer));
		if (mAlbedoArrayTexture != 0u) GL_CHECK(glDeleteTextures(1, &mAlbedoArrayTexture));
		mAlbedoArrayTexture = arrayTexture;
		mAlbedoArraySourceIds = std::move(sourceIds);
	}

	void ParticleSystem::uploadFrameData()
	{
		if (mEventRulesDirty)
		{
			mGpuEventRules.clear();
			for (uint32_t emitterIndex = 0; emitterIndex < mEmitters.size(); ++emitterIndex)
			{
				auto& range = mEmitters[emitterIndex].eventRange;
				range = { uint32_t(mGpuEventRules.size()), 0u,
					emitterIndex < mEmitterSlots.size() ? mEmitterSlots[emitterIndex].eventGeneration : 0u,
					emitterIndex < mEmitterSlots.size() && mEmitterSlots[emitterIndex].occupied &&
						!mEmitterSlots[emitterIndex].pendingDestroy ? 1u : 0u };
				if (emitterIndex >= mEmitterSlots.size() || !mEmitterSlots[emitterIndex].occupied) continue;
				auto const& rules = mEmitterEventRules[emitterIndex];
				range[1] = uint32_t(rules.size());
				for (auto const& rule : rules)
				{
					ParticleGpuEventRule gpuRule;
					gpuRule.configuration = { uint32_t(rule.trigger), uint32_t(rule.action),
						rule.targetEmitterTemplate, rule.count };
					gpuRule.parameters = { bit_cast<uint32_t>(rule.age), rule.payload,
						rule.targetEmitterGeneration, 0u };
					mGpuEventRules.push_back(gpuRule);
				}
			}
			if (mGpuEventRules.size() > MaxEventRuleCount)
				THROW_MPP("The particle event-rule capacity was exceeded.", __LINE__, __FILE__, __func__);
			if (!mGpuEventRules.empty())
			{
				GL_CHECK(glBindBuffer(GL_COPY_WRITE_BUFFER, mEventStorage->getBuffer()));
				GL_CHECK(glBufferSubData(GL_COPY_WRITE_BUFFER, GLintptr(eventRulesOffset()),
					GLsizeiptr(bytes(mGpuEventRules)), mGpuEventRules.data()));
				GL_CHECK(glBindBuffer(GL_COPY_WRITE_BUFFER, 0));
			}
			mEventRulesDirty = false;
		}

		if (mEmitters.size() > MaxEmitterCount || mTemplateRenderData.size() > MaxTemplateCount ||
			mSpawnCommands.size() > MaxSpawnCommandCount)
			THROW_MPP("Particle emitter-template, emitter, or spawn-command capacity was exceeded.", __LINE__, __FILE__, __func__);
		if (mTemplateRenderData.empty())
			THROW_MPP("Particle emitters require at least one emitter template.", __LINE__, __FILE__, __func__);
		if (mColliders.size() > MaxColliderCount)
			THROW_MPP("The particle analytical-collider capacity was exceeded.", __LINE__, __FILE__, __func__);
		for (auto const& emitter : mEmitters)
			if (emitter.emissionState[3] >= mTemplateRenderData.size())
				THROW_MPP("A particle emitter references an invalid emitter-template index.", __LINE__, __FILE__, __func__);
		updateAlbedoTextureArray();
		rebuildMeshDrawCommands();

		mVolumetricLightingGpuData.clear();
		mVolumetricLightingEmitters.clear();
		mVolumetricLightingGpuData.reserve(mEmitterSlots.size());
		mVolumetricLightingEmitters.reserve(mEmitterSlots.size());
		for (uint32_t index = 0; index < mEmitterSlots.size(); ++index)
		{
			if (!mEmitterSlots[index].occupied || mEmitterSlots[index].pendingDestroy) continue;
			auto const& lighting = mEmitterLighting[index];
			auto const& emitter = mEmitters[index];
			bool const active = emitter.emissionState[1] != 0u &&
				(uint32_t(emitter.emissionRateAndPadding[1]) & uint32_t(ParticleEffectVisibilityFlag::Visible)) != 0u;
			if (!active || !particleHasLighting(lighting, ParticleLightingFlag::VolumetricContribution)) continue;
			ParticleVolumetricLightingGpuData gpu;
			gpu.positionAndRange = { emitter.transform[12], emitter.transform[13], emitter.transform[14],
				lighting.rangeAndVolumetric[0] };
			gpu.colourAndIntensity = lighting.colourAndIntensity;
			gpu.colourAndIntensity[3] *= emitter.parameterMultipliers1[1];
			gpu.volumetricAndPadding = lighting.rangeAndVolumetric;
			gpu.flagsAndPadding = lighting.flagsAndPadding;
			gpu.flagsAndPadding[0] |= 8u;
			mVolumetricLightingGpuData.push_back(gpu);
			mVolumetricLightingEmitters.push_back(index);
		}

		mEmitterBuffer->upload(mEmitters.data(), bytes(mEmitters), 0, bytes(mEmitters));
		mTemplateRenderBuffer->upload(mTemplateRenderData.data(), bytes(mTemplateRenderData), 0, bytes(mTemplateRenderData));
		if (!mVolumetricLightingGpuData.empty())
			mVolumetricLightingBuffer->upload(mVolumetricLightingGpuData.data(), bytes(mVolumetricLightingGpuData),
				0, bytes(mVolumetricLightingGpuData));
		if (!mSpawnCommands.empty())
			mSpawnCommandBuffer->upload(mSpawnCommands.data(), bytes(mSpawnCommands), 0, bytes(mSpawnCommands));
		if (!mColliders.empty())
			mColliderBuffer->upload(mColliders.data(), bytes(mColliders), 0, bytes(mColliders));
	}

	void ParticleSystem::setStatisticsEnabled(bool enabled)
	{
		if (mStatistics->enabled == enabled) return;
		mStatistics->enabled = enabled;
		if (enabled)
		{
			mStatistics->stats = {};
			mStatistics->sequence = 0;
			mStatistics->latestSequence = 0;
		}
		else
		{
			// Deleting an in-flight GL object defers its storage reclamation; it does
			// not wait for the GPU. More importantly, no result is polled or retrieved.
			mStatistics->release();
		}
	}

	bool ParticleSystem::isStatisticsEnabled() const
	{
		return mStatistics->enabled;
	}

	ParticleStats const& ParticleSystem::getStats() const
	{
		return mStatistics->stats;
	}

	void ParticleSystem::setEventCallback(ParticleEventAction action, ParticleEventCallback callback)
	{
		auto const index = uint32_t(action);
		if (index >= mEventReadback->callbacks.size())
			throw invalid_argument("ParticleSystem::setEventCallback received an unknown particle event action.");
		if (action == ParticleEventAction::SecondaryParticleBurst)
			throw invalid_argument("Secondary particle bursts are GPU-owned and cannot have a CPU particle event callback.");
		mEventReadback->callbacks[index] = std::move(callback);
		if (!mEventReadback->enabled()) mEventReadback->release();
	}

	void ParticleSystem::clearEventCallback(ParticleEventAction action)
	{
		auto const index = uint32_t(action);
		if (index >= mEventReadback->callbacks.size()) return;
		mEventReadback->callbacks[index] = {};
		if (!mEventReadback->enabled()) mEventReadback->release();
	}

	bool ParticleSystem::hasEventCallback(ParticleEventAction action) const
	{
		auto const index = uint32_t(action);
		return index < mEventReadback->callbacks.size() && bool(mEventReadback->callbacks[index]);
	}

	vector<ParticleProxyLight> ParticleSystem::getProxyLights(size_t maximumCount, bool injectionOnly) const
	{
		updateViewEffectSubmissions();
		vector<ParticleProxyLight> result;
		result.reserve(min(maximumCount, mEmitterSlots.size()));
		for (uint32_t index = 0; index < mEmitterSlots.size() && result.size() < maximumCount; ++index)
		{
			auto const& slot = mEmitterSlots[index];
			if (!slot.occupied || slot.pendingDestroy || index >= mEmitterLighting.size() ||
				!isEmitterSubmittedForCurrentView(index)) continue;
			auto const& emitter = mEmitters[index];
			if (emitter.emissionState[1] == 0u ||
				(uint32_t(emitter.emissionRateAndPadding[1]) & uint32_t(ParticleEffectVisibilityFlag::Visible)) == 0u) continue;
			auto const& lighting = mEmitterLighting[index];
			if (!particleHasLighting(lighting, ParticleLightingFlag::ProxyLight)) continue;
			bool const injected = particleHasLighting(lighting, ParticleLightingFlag::PbrLightInjection);
			if (injectionOnly && !injected) continue;

			ParticleProxyLight proxy;
			proxy.emitter = { index, slot.generation };
			proxy.injectedIntoPbr = injected;
			proxy.light.type = PbrLightType::Point;
			proxy.light.colour = { lighting.colourAndIntensity[0], lighting.colourAndIntensity[1],
				lighting.colourAndIntensity[2] };
			proxy.light.intensity = lighting.colourAndIntensity[3] * emitter.parameterMultipliers1[1];
			proxy.light.position = { emitter.transform[12], emitter.transform[13], emitter.transform[14] };
			proxy.light.range = lighting.rangeAndVolumetric[0];
			result.push_back(proxy);
		}
		return result;
	}

	void ParticleSystem::pollEventReadback()
	{
		if (!mEventReadback->enabled()) return;
		auto& state = *mEventReadback;
		++state.sequence;
		vector<ParticleEvent> readyEvents;
		for (auto& slot : state.slots)
		{
			if (!slot.submitted || !slot.fence || state.sequence < slot.sequence + detail::ParticleEventReadbackState::MinimumLag)
				continue;
			auto const status = glClientWaitSync(slot.fence, 0, 0);
			if (status != GL_ALREADY_SIGNALED && status != GL_CONDITION_SATISFIED) continue;

			ParticleEventStorageHeader header;
			GL_CHECK(glBindBuffer(GL_COPY_READ_BUFFER, slot.buffer));
			GL_CHECK(glGetBufferSubData(GL_COPY_READ_BUFFER, 0, sizeof(header), &header));
			auto const count = min(header.externalCount, MaxExternalEventCount);
			size_t const oldSize = readyEvents.size();
			readyEvents.resize(oldSize + count);
			if (count != 0u)
				GL_CHECK(glGetBufferSubData(GL_COPY_READ_BUFFER, sizeof(header), GLsizeiptr(size_t(count) * sizeof(ParticleEvent)),
					readyEvents.data() + oldSize));
			GL_CHECK(glBindBuffer(GL_COPY_READ_BUFFER, 0));
			glDeleteSync(slot.fence);
			slot.fence = nullptr;
			slot.submitted = false;
		}

		for (auto const& event : readyEvents)
		{
			auto const action = uint32_t(event.getAction());
			if (action >= state.callbacks.size()) continue;
			auto callback = state.callbacks[action];
			if (callback) callback(event);
		}
	}

	void ParticleSystem::queueEventReadback()
	{
		if (!mEventReadback->enabled() || !mEventStorage) return;
		auto& state = *mEventReadback;
		auto& slot = state.slots[size_t(state.sequence % state.slots.size())];
		if (slot.submitted) return;
		if (slot.buffer == 0u)
		{
			GL_CHECK(glGenBuffers(1, &slot.buffer));
			GL_CHECK(glBindBuffer(GL_COPY_WRITE_BUFFER, slot.buffer));
			GL_CHECK(glBufferData(GL_COPY_WRITE_BUFFER, GLsizeiptr(eventReadbackBytes()), nullptr, GL_STREAM_READ));
			GL_CHECK(glObjectLabel(GL_BUFFER, slot.buffer, -1, "Particle event asynchronous readback"));
		}
		GL_CHECK(glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT));
		GL_CHECK(glBindBuffer(GL_COPY_READ_BUFFER, mEventStorage->getBuffer()));
		GL_CHECK(glBindBuffer(GL_COPY_WRITE_BUFFER, slot.buffer));
		GL_CHECK(glCopyBufferSubData(GL_COPY_READ_BUFFER, GL_COPY_WRITE_BUFFER, 0, 0, sizeof(ParticleEventStorageHeader)));
		GL_CHECK(glCopyBufferSubData(GL_COPY_READ_BUFFER, GL_COPY_WRITE_BUFFER, GLintptr(externalEventsOffset()),
			sizeof(ParticleEventStorageHeader), GLsizeiptr(size_t(MaxExternalEventCount) * sizeof(ParticleEvent))));
		GL_CHECK(glBindBuffer(GL_COPY_READ_BUFFER, 0));
		GL_CHECK(glBindBuffer(GL_COPY_WRITE_BUFFER, 0));
		slot.fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
		slot.sequence = state.sequence;
		slot.submitted = slot.fence != nullptr;
	}

	void ParticleSystem::advanceStatisticsFrame()
	{
		if (!mStatistics->enabled) return;
		auto& state = *mStatistics;
		++state.sequence;

		// The next particle frame is the only reliable point after every graph pass
		// from the prior frame. Its fence therefore covers both simulation and all
		// particle draws without requiring a render-graph callback.
		if (state.currentSlot >= 0)
		{
			auto& previous = state.slots[size_t(state.currentSlot)];
			if (previous.submitted && !previous.fence)
				previous.fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
			state.currentSlot = -1;
		}

		for (auto& slot : state.slots)
		{
			if (!slot.submitted || !slot.fence || state.sequence < slot.sequence + detail::ParticleStatisticsState::MinimumLag)
				continue;
			auto const status = glClientWaitSync(slot.fence, 0, 0);
			if (status != GL_ALREADY_SIGNALED && status != GL_CONDITION_SATISFIED) continue;

			ParticleCounterHeader counters;
			GL_CHECK(glBindBuffer(GL_COPY_READ_BUFFER, slot.buffer));
			GL_CHECK(glGetBufferSubData(GL_COPY_READ_BUFFER, 0, sizeof(counters), &counters));
			GL_CHECK(glBindBuffer(GL_COPY_READ_BUFFER, 0));
			auto elapsedMilliseconds = [](detail::ParticleStatisticsState::QueryPair const& query)
			{
				if (!query.begin || !query.end) return 0.0;
				GLuint64 begin = 0, end = 0;
				GL_CHECK(glGetQueryObjectui64v(query.begin, GL_QUERY_RESULT, &begin));
				GL_CHECK(glGetQueryObjectui64v(query.end, GL_QUERY_RESULT, &end));
				return end >= begin ? double(end - begin) / 1000000.0 : 0.0;
			};

			if (slot.sequence >= state.latestSequence)
			{
				auto& stats = state.stats;
				stats.valid = true;
				stats.sourceFrame = slot.sourceFrame;
				stats.framesLagged = uint32_t(min<uint64_t>(state.sequence - slot.sequence, UINT32_MAX));
				stats.activeParticles = slot.activeListIndex == 0u ? counters.activeCountA : counters.activeCountB;
				stats.freeParticles = counters.freeCount;
				stats.spawnedParticles = counters.spawnedCount;
				stats.killedParticles = counters.killedCount;
				stats.droppedParticles = counters.droppedSpawnCount;
				stats.renderedParticles = counters.renderedCount;
				stats.culledParticles = counters.culledCount;
				stats.submittedEffects = slot.submittedEffects;
				stats.boundsCulledEffects = slot.boundsCulledEffects;
				stats.activeEmitters = slot.activeEmitters;
				stats.capacity = slot.capacity;
				stats.capacityUsage = slot.capacity == 0u ? 0.0f : float(stats.activeParticles) / float(slot.capacity);
				stats.simulationGpuMilliseconds = elapsedMilliseconds(slot.simulation);
				stats.sortingGpuMilliseconds = elapsedMilliseconds(slot.sorting);
				stats.renderGpuMilliseconds = 0.0;
				for (auto const& query : slot.renders) stats.renderGpuMilliseconds += elapsedMilliseconds(query);
				state.latestSequence = slot.sequence;
			}

			glDeleteSync(slot.fence);
			slot.fence = nullptr;
			detail::ParticleStatisticsState::deleteQueries(slot);
			slot.submitted = false;
		}

		auto& candidate = state.slots[size_t(state.sequence % state.slots.size())];
		if (!candidate.submitted)
		{
			candidate.sequence = state.sequence;
			candidate.sourceFrame = mwRenderSystem->getFrameSerial();
			state.currentSlot = int(state.sequence % state.slots.size());
		}
		// If all slots are still busy, this frame simply has no sample. Statistics
		// are diagnostic and must never turn ring pressure into a wait.
	}

	void ParticleSystem::dispatchStatisticsPrepare()
	{
		if (!mStatistics->enabled) return;
		mCounters->bindStorage(CountersBinding);
		auto* program = static_cast<ComputeProgram*>(mStatisticsPrepareProgram.get());
		program->use();
		uint64_t requestedSpawnCount = 0;
		for (auto const& command : mSpawnCommands) requestedSpawnCount += command.count;
		program->setUniform("ACTIVE_LIST_INDEX", mActiveListIndex);
		program->setUniform("REQUESTED_SPAWN_COUNT", uint32_t(min<uint64_t>(requestedSpawnCount, UINT32_MAX)));
		program->dispatch(1u);
		GL_CHECK(glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT));
	}

	void ParticleSystem::beginStatisticsSample()
	{
		if (!mStatistics->enabled || mStatistics->currentSlot < 0) return;
		auto& slot = mStatistics->slots[size_t(mStatistics->currentSlot)];
		if (slot.buffer == 0)
		{
			GL_CHECK(glGenBuffers(1, &slot.buffer));
			GL_CHECK(glBindBuffer(GL_COPY_WRITE_BUFFER, slot.buffer));
			GL_CHECK(glBufferData(GL_COPY_WRITE_BUFFER, sizeof(ParticleCounterHeader), nullptr, GL_STREAM_READ));
			GL_CHECK(glObjectLabel(GL_BUFFER, slot.buffer, -1, "Particle statistics readback"));
			GL_CHECK(glBindBuffer(GL_COPY_WRITE_BUFFER, 0));
		}
		GLuint id{};
		GL_CHECK(glGenQueries(1, &id));
		slot.simulation = { id, 0 };
		slot.activeListIndex = mActiveListIndex;
		slot.capacity = mPoolCapacity;
		GL_CHECK(glQueryCounter(slot.simulation.begin, GL_TIMESTAMP));
	}

	void ParticleSystem::finishSimulationTiming()
	{
		if (!mStatistics->enabled || mStatistics->currentSlot < 0) return;
		auto& slot = mStatistics->slots[size_t(mStatistics->currentSlot)];
		if (slot.simulation.begin && !slot.simulation.end)
		{
			GL_CHECK(glGenQueries(1, &slot.simulation.end));
			GL_CHECK(glQueryCounter(slot.simulation.end, GL_TIMESTAMP));
		}
	}

	void ParticleSystem::finishStatisticsSample()
	{
		if (!mStatistics->enabled || mStatistics->currentSlot < 0) return;
		auto& slot = mStatistics->slots[size_t(mStatistics->currentSlot)];
		if (!slot.simulation.begin) return;
		finishSimulationTiming();
		slot.activeListIndex = mActiveListIndex;
		slot.activeEmitters = uint32_t(getLiveEmitterCount());
		slot.submittedEffects = 0u;
		slot.boundsCulledEffects = 0u;
		slot.recordedViews.clear();
		GL_CHECK(glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT));
		GL_CHECK(glBindBuffer(GL_COPY_READ_BUFFER, mCounters->getBuffer()));
		GL_CHECK(glBindBuffer(GL_COPY_WRITE_BUFFER, slot.buffer));
		GL_CHECK(glCopyBufferSubData(GL_COPY_READ_BUFFER, GL_COPY_WRITE_BUFFER, 0, 0, sizeof(ParticleCounterHeader)));
		GL_CHECK(glBindBuffer(GL_COPY_READ_BUFFER, 0));
		GL_CHECK(glBindBuffer(GL_COPY_WRITE_BUFFER, 0));
		slot.submitted = true;
	}

	void ParticleSystem::beginSortTiming()
	{
		if (!mStatistics->enabled || mStatistics->currentSlot < 0) return;
		auto& slot = mStatistics->slots[size_t(mStatistics->currentSlot)];
		if (!slot.simulation.begin || slot.sorting.begin) return;
		GLuint ids[2]{};
		GL_CHECK(glGenQueries(2, ids));
		slot.sorting = { ids[0], ids[1] };
		GL_CHECK(glQueryCounter(slot.sorting.begin, GL_TIMESTAMP));
	}

	void ParticleSystem::finishSortTiming()
	{
		if (!mStatistics->enabled || mStatistics->currentSlot < 0) return;
		auto& slot = mStatistics->slots[size_t(mStatistics->currentSlot)];
		if (slot.sorting.begin && slot.sorting.end)
			GL_CHECK(glQueryCounter(slot.sorting.end, GL_TIMESTAMP));
	}

	void ParticleSystem::beginRenderTiming()
	{
		if (!mStatistics->enabled || mStatistics->currentSlot < 0) return;
		auto& slot = mStatistics->slots[size_t(mStatistics->currentSlot)];
		if (!slot.submitted) return;
		GLuint ids[2]{};
		GL_CHECK(glGenQueries(2, ids));
		slot.renders.push_back({ ids[0], ids[1] });
		GL_CHECK(glQueryCounter(ids[0], GL_TIMESTAMP));
	}

	void ParticleSystem::finishRenderTiming()
	{
		if (!mStatistics->enabled || mStatistics->currentSlot < 0) return;
		auto& slot = mStatistics->slots[size_t(mStatistics->currentSlot)];
		if (!slot.submitted || slot.renders.empty()) return;
		GL_CHECK(glQueryCounter(slot.renders.back().end, GL_TIMESTAMP));
	}

	void ParticleSystem::dispatchEventPrepare(uint32_t mode, uint32_t sourceQueue)
	{
		mEventStorage->bindStorage(EventStorageBinding);
		mEventDispatchCommand->bindStorage(DispatchCommandBinding);
		auto* program = static_cast<ComputeProgram*>(mEventPrepareProgram.get());
		program->use();
		program->setUniform("MODE", mode);
		program->setUniform("SOURCE_QUEUE", sourceQueue);
		program->dispatch(1u);
		GL_CHECK(glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_COMMAND_BARRIER_BIT));
	}

	void ParticleSystem::dispatchSpawnCommands()
	{
		if (mSpawnCommands.empty()) return;

		mParticlePool->bindStorage(ParticlePoolBinding);
		mFreeIndices->bindStorage(FreeIndicesBinding);
		mActiveIndicesA->bindStorage(ActiveIndicesABinding);
		mActiveIndicesB->bindStorage(ActiveIndicesBBinding);
		mCounters->bindStorage(CountersBinding);
		mEventStorage->bindStorage(EventStorageBinding);
		GL_CHECK(glBindBufferRange(GL_SHADER_STORAGE_BUFFER, EmitterBinding, mEmitterBuffer->getBuffer(),
			static_cast<GLintptr>(mEmitterBuffer->getActiveOffset()), static_cast<GLsizeiptr>(bytes(mEmitters))));
		GL_CHECK(glBindBufferRange(GL_SHADER_STORAGE_BUFFER, SpawnCommandBinding, mSpawnCommandBuffer->getBuffer(),
			static_cast<GLintptr>(mSpawnCommandBuffer->getActiveOffset()), static_cast<GLsizeiptr>(bytes(mSpawnCommands))));

		auto* spawnProgram = static_cast<ComputeProgram*>(mSpawnProgram.get());
		spawnProgram->use();
		spawnProgram->setUniform("EMITTER_COUNT", uint32_t(mEmitters.size()));
		spawnProgram->setUniform("TEMPLATE_COUNT", uint32_t(mTemplateRenderData.size()));
		spawnProgram->setUniform("SPAWN_COMMAND_OFFSET", 0u);
		spawnProgram->setUniform("ACTIVE_LIST_INDEX", mActiveListIndex);
		spawnProgram->dispatch(uint32_t(mSpawnCommands.size()));
		GL_CHECK(glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_COMMAND_BARRIER_BIT));
		mSpawnCommandBuffer->markUsed();
		mSpawnCommands.clear();
	}

	void ParticleSystem::dispatchSimulation(float dt)
	{
		mCounters->bindStorage(CountersBinding);
		mSimulationDispatchCommand->bindStorage(DispatchCommandBinding);
		auto* prepareProgram = static_cast<ComputeProgram*>(mSimulationPrepareProgram.get());
		prepareProgram->use();
		prepareProgram->setUniform("ACTIVE_LIST_INDEX", mActiveListIndex);
		prepareProgram->dispatch(1u);
		GL_CHECK(glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_COMMAND_BARRIER_BIT));

		mParticlePool->bindStorage(ParticlePoolBinding);
		mFreeIndices->bindStorage(FreeIndicesBinding);
		mActiveIndicesA->bindStorage(ActiveIndicesABinding);
		mActiveIndicesB->bindStorage(ActiveIndicesBBinding);
		mCounters->bindStorage(CountersBinding);
		GL_CHECK(glBindBufferRange(GL_SHADER_STORAGE_BUFFER, EmitterBinding, mEmitterBuffer->getBuffer(),
			static_cast<GLintptr>(mEmitterBuffer->getActiveOffset()), static_cast<GLsizeiptr>(bytes(mEmitters))));
		if (!mColliders.empty())
			GL_CHECK(glBindBufferRange(GL_SHADER_STORAGE_BUFFER, ColliderBinding, mColliderBuffer->getBuffer(),
				static_cast<GLintptr>(mColliderBuffer->getActiveOffset()), static_cast<GLsizeiptr>(bytes(mColliders))));
		mEventStorage->bindStorage(SimulationEventStorageBinding);

		auto* collisionDepth = dynamic_cast<RenderTexture*>(mScreenSpaceCollisionDepth.get());
		bool const hasCollisionDepth = collisionDepth && collisionDepth->getDepthTextureId() != 0u && !collisionDepth->isMultisampled();
		Texture* vectorField = nullptr;
		if (mVectorFieldTexture)
		{
			mVectorFieldTexture->load();
			vectorField = dynamic_cast<Texture*>(mVectorFieldTexture.get());
			if (!vectorField || vectorField->getTextureTarget() != GL_TEXTURE_3D)
				THROW_MPP("A particle vector field must be a 3D texture.", __LINE__, __FILE__, __func__);
		}
		Texture* signedDistanceField = nullptr;
		if (mSignedDistanceFieldTexture)
		{
			mSignedDistanceFieldTexture->load();
			signedDistanceField = dynamic_cast<Texture*>(mSignedDistanceFieldTexture.get());
			if (!signedDistanceField || signedDistanceField->getTextureTarget() != GL_TEXTURE_3D)
				THROW_MPP("A particle signed distance field must be a 3D texture.", __LINE__, __FILE__, __func__);
		}

		auto* simulationProgram = static_cast<ComputeProgram*>(mSimulationProgram.get());
		simulationProgram->use();
		simulationProgram->setUniform("ACTIVE_LIST_INDEX", mActiveListIndex);
		simulationProgram->setUniform("EMITTER_COUNT", uint32_t(mEmitters.size()));
		simulationProgram->setUniform("TEMPLATE_COUNT", uint32_t(mTemplateRenderData.size()));
		simulationProgram->setUniform("COLLIDER_COUNT", uint32_t(mColliders.size()));
		simulationProgram->setUniform("HAS_COLLISION_DEPTH", int32_t(hasCollisionDepth ? 1 : 0));
		simulationProgram->setUniform("HAS_SIGNED_DISTANCE_FIELD", int32_t(signedDistanceField ? 1 : 0));
		simulationProgram->setUniform("HAS_VECTOR_FIELD", int32_t(vectorField ? 1 : 0));
		simulationProgram->setUniform("DELTA_SECONDS", dt);
		simulationProgram->setUniform("SIMULATION_SECONDS", mSimulationSeconds);
		simulationProgram->setUniform("NOISE_TEXTURE", int32_t(0));
		simulationProgram->setUniform("COLLISION_DEPTH_TEXTURE", int32_t(1));
		simulationProgram->setUniform("SIGNED_DISTANCE_FIELD_TEXTURE", int32_t(2));
		simulationProgram->setUniform("VECTOR_FIELD_TEXTURE", int32_t(3));
		GL_CHECK(glUniformMatrix4fv(simulationProgram->getUniformLocation("SDF_WORLD_TO_TEXTURE"), 1, GL_FALSE,
			mSignedDistanceFieldData.worldToTexture.data()));
		GL_CHECK(glUniform4fv(simulationProgram->getUniformLocation("SDF_PARAMETERS"), 1,
			mSignedDistanceFieldData.parameters.data()));
		GL_CHECK(glActiveTexture(GL_TEXTURE0));
		GL_CHECK(glBindSampler(0, 0));
		GL_CHECK(glBindTexture(GL_TEXTURE_3D, mNoiseTexture));
		if (hasCollisionDepth) collisionDepth->bindDepth(1u);
		if (signedDistanceField) signedDistanceField->bind(2u);
		if (vectorField) vectorField->bind(3u);
		mSimulationDispatchCommand->bindDispatchIndirect();
		simulationProgram->dispatchIndirect();
		GL_CHECK(glBindBuffer(GL_DISPATCH_INDIRECT_BUFFER, 0));
		GL_CHECK(glActiveTexture(GL_TEXTURE3));
		GL_CHECK(glBindTexture(GL_TEXTURE_3D, 0));
		GL_CHECK(glActiveTexture(GL_TEXTURE2));
		GL_CHECK(glBindTexture(GL_TEXTURE_3D, 0));
		GL_CHECK(glActiveTexture(GL_TEXTURE1));
		GL_CHECK(glBindTexture(GL_TEXTURE_2D, 0));
		GL_CHECK(glActiveTexture(GL_TEXTURE0));
		GL_CHECK(glBindTexture(GL_TEXTURE_3D, 0));
		GL_CHECK(glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_COMMAND_BARRIER_BIT));
		if (!mColliders.empty()) mColliderBuffer->markUsed();

		mActiveListIndex = 1u - mActiveListIndex;
	}

	void ParticleSystem::dispatchParticleEvents()
	{
		if (mGpuEventRules.empty()) return;
		for (uint32_t cascade = 0u; cascade < MaxSecondaryEventCascadeDepth; ++cascade)
		{
			uint32_t const sourceQueue = cascade & 1u;
			dispatchEventPrepare(1u, sourceQueue);
			mParticlePool->bindStorage(ParticlePoolBinding);
			mFreeIndices->bindStorage(FreeIndicesBinding);
			mActiveIndicesA->bindStorage(ActiveIndicesABinding);
			mActiveIndicesB->bindStorage(ActiveIndicesBBinding);
			mCounters->bindStorage(CountersBinding);
			GL_CHECK(glBindBufferRange(GL_SHADER_STORAGE_BUFFER, EmitterBinding, mEmitterBuffer->getBuffer(),
				static_cast<GLintptr>(mEmitterBuffer->getActiveOffset()), static_cast<GLsizeiptr>(bytes(mEmitters))));
			mEventStorage->bindStorage(EventStorageBinding);
			auto* program = static_cast<ComputeProgram*>(mEventProcessProgram.get());
			program->use();
			program->setUniform("ACTIVE_LIST_INDEX", mActiveListIndex);
			program->setUniform("EMITTER_COUNT", uint32_t(mEmitters.size()));
			program->setUniform("TEMPLATE_COUNT", uint32_t(mTemplateRenderData.size()));
			program->setUniform("SOURCE_QUEUE", sourceQueue);
			mEventDispatchCommand->bindDispatchIndirect();
			program->dispatchIndirect();
			GL_CHECK(glBindBuffer(GL_DISPATCH_INDIRECT_BUFFER, 0));
			GL_CHECK(glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_COMMAND_BARRIER_BIT));
		}
		dispatchEventPrepare(2u, MaxSecondaryEventCascadeDepth & 1u);
		queueEventReadback();
	}

	void ParticleSystem::dispatchCompaction()
	{
		uint32_t const templateCount = uint32_t(mTemplateRenderData.size());

		mCounters->bindStorage(CountersBinding);
		mCompactionScratch->bindStorage(CompactionScratchBinding);
		mCompactionDispatchCommand->bindStorage(DispatchCommandBinding);
		auto* prepareProgram = static_cast<ComputeProgram*>(mCompactionPrepareProgram.get());
		prepareProgram->use();
		prepareProgram->setUniform("ACTIVE_LIST_INDEX", mActiveListIndex);
		prepareProgram->setUniform("TEMPLATE_COUNT", templateCount);
		prepareProgram->setUniform("TEMPLATE_CAPACITY", MaxTemplateCount);
		prepareProgram->dispatch((templateCount + mWorkGroupSize - 1u) / mWorkGroupSize);
		GL_CHECK(glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_COMMAND_BARRIER_BIT));

		mParticlePool->bindStorage(ParticlePoolBinding);
		mActiveIndicesA->bindStorage(ActiveIndicesABinding);
		mActiveIndicesB->bindStorage(ActiveIndicesBBinding);
		mCounters->bindStorage(CountersBinding);
		GL_CHECK(glBindBufferRange(GL_SHADER_STORAGE_BUFFER, EmitterBinding, mEmitterBuffer->getBuffer(),
			static_cast<GLintptr>(mEmitterBuffer->getActiveOffset()), static_cast<GLsizeiptr>(bytes(mEmitters))));
		mCompactionScratch->bindStorage(CompactionScratchBinding);
		GL_CHECK(glBindBufferRange(GL_SHADER_STORAGE_BUFFER, CullingTemplateBinding, mTemplateRenderBuffer->getBuffer(),
			static_cast<GLintptr>(mTemplateRenderBuffer->getActiveOffset()), static_cast<GLsizeiptr>(bytes(mTemplateRenderData))));
		auto* countProgram = static_cast<ComputeProgram*>(mCompactionCountProgram.get());
		countProgram->use();
		countProgram->setUniform("ACTIVE_LIST_INDEX", mActiveListIndex);
		countProgram->setUniform("EMITTER_COUNT", uint32_t(mEmitters.size()));
		countProgram->setUniform("TEMPLATE_COUNT", templateCount);
		countProgram->setUniform("TEMPLATE_CAPACITY", MaxTemplateCount);
		mCompactionDispatchCommand->bindDispatchIndirect();
		countProgram->dispatchIndirect();
		GL_CHECK(glBindBuffer(GL_DISPATCH_INDIRECT_BUFFER, 0));
		GL_CHECK(glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT));

		mCounters->bindStorage(CountersBinding);
		mCompactionScratch->bindStorage(CompactionScratchBinding);
		GL_CHECK(glBindBufferRange(GL_SHADER_STORAGE_BUFFER, EmitterBinding, mTemplateRenderBuffer->getBuffer(),
			static_cast<GLintptr>(mTemplateRenderBuffer->getActiveOffset()), static_cast<GLsizeiptr>(bytes(mTemplateRenderData))));
		mIndirectCommands->bindStorage(IndirectCommandBinding);
		auto* prefixProgram = static_cast<ComputeProgram*>(mCompactionPrefixProgram.get());
		prefixProgram->use();
		prefixProgram->setUniform("TEMPLATE_COUNT", templateCount);
		prefixProgram->setUniform("TEMPLATE_CAPACITY", MaxTemplateCount);
		prefixProgram->dispatch(1u);
		GL_CHECK(glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_COMMAND_BARRIER_BIT));
		dispatchMeshDrawCommands();

		mParticlePool->bindStorage(ParticlePoolBinding);
		mRenderIndices->bindStorage(FreeIndicesBinding);
		mActiveIndicesA->bindStorage(ActiveIndicesABinding);
		mActiveIndicesB->bindStorage(ActiveIndicesBBinding);
		mCounters->bindStorage(CountersBinding);
		GL_CHECK(glBindBufferRange(GL_SHADER_STORAGE_BUFFER, EmitterBinding, mEmitterBuffer->getBuffer(),
			static_cast<GLintptr>(mEmitterBuffer->getActiveOffset()), static_cast<GLsizeiptr>(bytes(mEmitters))));
		mCompactionScratch->bindStorage(CompactionScratchBinding);
		GL_CHECK(glBindBufferRange(GL_SHADER_STORAGE_BUFFER, CullingTemplateBinding, mTemplateRenderBuffer->getBuffer(),
			static_cast<GLintptr>(mTemplateRenderBuffer->getActiveOffset()), static_cast<GLsizeiptr>(bytes(mTemplateRenderData))));
		auto* scatterProgram = static_cast<ComputeProgram*>(mCompactionScatterProgram.get());
		scatterProgram->use();
		scatterProgram->setUniform("ACTIVE_LIST_INDEX", mActiveListIndex);
		scatterProgram->setUniform("EMITTER_COUNT", uint32_t(mEmitters.size()));
		scatterProgram->setUniform("TEMPLATE_COUNT", templateCount);
		scatterProgram->setUniform("TEMPLATE_CAPACITY", MaxTemplateCount);
		mCompactionDispatchCommand->bindDispatchIndirect();
		scatterProgram->dispatchIndirect();
		GL_CHECK(glBindBuffer(GL_DISPATCH_INDIRECT_BUFFER, 0));
		GL_CHECK(glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_COMMAND_BARRIER_BIT));

		mEmitterBuffer->markUsed();
		mTemplateRenderBuffer->markUsed();
	}

	void ParticleSystem::rebuildMeshDrawCommands()
	{
		mMeshDrawRecords.clear();
		vector<ParticleMeshDrawIndirectCommand> commands;
		vector<uint32_t> commandTemplates;
		for (uint32_t templateIndex = 0u; templateIndex < mTemplateMeshModels.size(); ++templateIndex)
		{
			auto const& modelResource = mTemplateMeshModels[templateIndex];
			if (!modelResource) continue;
			modelResource->load();
			auto const* model = dynamic_cast<Model const*>(modelResource.get());
			if (!model)
				THROW_MPP("A mesh particle model must be a Model resource.", __LINE__, __FILE__, __func__);

			float boundsRadius = 1.0f;
			if (model->hasBounds())
			{
				glm::vec3 minimum, maximum;
				model->getBounds(minimum, maximum);
				// The farthest AABB corner may combine components from opposite
				// endpoints, so neither endpoint length is a conservative radius.
				auto const farthestCorner = glm::max(glm::abs(minimum), glm::abs(maximum));
				boundsRadius = max(0.000001f, glm::length(farthestCorner));
			}
			mTemplateRenderData[templateIndex].culling[2] = boundsRadius;

			for (int meshIndex = 0; meshIndex < model->getNumMeshes(); ++meshIndex)
			{
				if (mMeshDrawRecords.size() >= MaxMeshDrawCount)
					THROW_MPP("The mesh-particle draw capacity was exceeded.", __LINE__, __FILE__, __func__);
				auto const* mesh = model->getMesh(meshIndex);
				auto material = mTemplateMeshMaterials[templateIndex] ?
					mTemplateMeshMaterials[templateIndex] : mesh->getMaterial();
				if (!dynamic_cast<Material*>(material.get()))
					THROW_MPP("A mesh particle requires a Material resource.", __LINE__, __FILE__, __func__);
				material->load();
				auto* program = dynamic_cast<Program*>(static_cast<Material*>(material.get())->getProgram().get());
				if (!program || program->getUniformId("MPP_PARTICLE_MESH_ENABLED") < 0 ||
					program->getUniformId("MPP_PARTICLE_MESH_TEMPLATE") < 0)
					THROW_MPP("A mesh-particle material must use a 3D program with model and model-camera-projection matrices.", __LINE__, __FILE__, __func__);

				size_t const vertexOrIndexCount = mesh->mPrimitiveCount * size_t(mesh->mPrimitiveSize);
				if (vertexOrIndexCount > numeric_limits<uint32_t>::max())
					THROW_MPP("A mesh-particle mesh has too many vertices or indices for indirect drawing.", __LINE__, __FILE__, __func__);
				ParticleMeshDrawIndirectCommand command;
				command.count = uint32_t(vertexOrIndexCount);
				if (mesh->mIsIndexed)
				{
					size_t const indexBytes = mesh->mIndexWidth / 8u;
					size_t const firstIndex = indexBytes == 0u ? 0u : mesh->getActiveIndexOffset() / indexBytes;
					if (firstIndex > numeric_limits<uint32_t>::max())
						THROW_MPP("A mesh-particle index offset exceeds the indirect command range.", __LINE__, __FILE__, __func__);
					command.first = uint32_t(firstIndex);
				}
				commands.push_back(command);
				commandTemplates.push_back(templateIndex);
				mMeshDrawRecords.push_back({ templateIndex, mesh, std::move(material) });
			}
		}

		if (commands.empty()) return;
		if (!mMeshIndirectCommands)
		{
			mMeshIndirectCommands = make_unique<ShaderStorageBuffer>();
			mMeshIndirectCommands->create(size_t(MaxMeshDrawCount) * sizeof(ParticleMeshDrawIndirectCommand), nullptr,
				"Particle real-mesh indirect draw commands");
			mMeshCommandTemplates = make_unique<ShaderStorageBuffer>();
			mMeshCommandTemplates->create(size_t(MaxMeshDrawCount) * sizeof(uint32_t), nullptr,
				"Particle mesh draw to emitter-template mapping");
		}
		GL_CHECK(glBindBuffer(GL_SHADER_STORAGE_BUFFER, mMeshIndirectCommands->getBuffer()));
		GL_CHECK(glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, GLsizeiptr(commands.size() * sizeof(commands.front())), commands.data()));
		GL_CHECK(glBindBuffer(GL_SHADER_STORAGE_BUFFER, mMeshCommandTemplates->getBuffer()));
		GL_CHECK(glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, GLsizeiptr(commandTemplates.size() * sizeof(commandTemplates.front())), commandTemplates.data()));
		GL_CHECK(glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0));
	}

	void ParticleSystem::dispatchMeshDrawCommands()
	{
		if (mMeshDrawRecords.empty()) return;
		mCompactionScratch->bindStorage(0u);
		mMeshCommandTemplates->bindStorage(1u);
		mMeshIndirectCommands->bindStorage(2u);
		auto* program = static_cast<ComputeProgram*>(mMeshCommandProgram.get());
		program->use();
		program->setUniform("MESH_DRAW_COUNT", uint32_t(mMeshDrawRecords.size()));
		program->setUniform("TEMPLATE_CAPACITY", MaxTemplateCount);
		program->dispatch((uint32_t(mMeshDrawRecords.size()) + mWorkGroupSize - 1u) / mWorkGroupSize);
		GL_CHECK(glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_COMMAND_BARRIER_BIT));
	}

	void ParticleSystem::ensureSortBuffersAllocated()
	{
		if (mSortRecordsA) return;
		auto createComputeProgram = [this](char const* name, char const* source)
		{
			auto stream = make_shared<ComputeProgramStream>(mwResourceManager);
			stream->setSource(RawShaderStage::Compute, source);
			stream->setDefine("MPP_PARTICLE_WORK_GROUP_SIZE", to_string(mWorkGroupSize));
			auto program = mwResourceManager->declareResource(name, stream).first;
			program->acquire(mwRenderSystem);
			program->load();
			return program;
		};
		mSortPrepareProgram = createComputeProgram(SortPrepareProgramName, ParticleSortPrepareComputeShader);
		mSortKeyProgram = createComputeProgram(SortKeyProgramName, ParticleSortKeyComputeShader);
		mRadixHistogramProgram = createComputeProgram(RadixHistogramProgramName, ParticleRadixHistogramComputeShader);
		mRadixPrefixProgram = createComputeProgram(RadixPrefixProgramName, ParticleRadixPrefixComputeShader);
		mRadixScatterProgram = createComputeProgram(RadixScatterProgramName, ParticleRadixScatterComputeShader);
		mSortFinalizeProgram = createComputeProgram(SortFinalizeProgramName, ParticleSortFinalizeComputeShader);

		size_t const recordBytes = size_t(mPoolCapacity) * sizeof(ParticleSortRecord);
		size_t const groupCount = (size_t(mPoolCapacity) + mWorkGroupSize - 1u) / mWorkGroupSize;
		size_t const histogramBytes = groupCount * 16u * sizeof(uint32_t);
		size_t const largestBlock = max(recordBytes, histogramBytes);
		if (largestBlock > mwRenderSystem->getCaps().maxShaderStorageBlockSize)
		{
			THROW_MPP("Particle depth sorting needs a shader storage block of " + to_string(largestBlock) +
				" bytes, exceeding the GPU maximum of " + to_string(mwRenderSystem->getCaps().maxShaderStorageBlockSize) + " bytes.",
				__LINE__, __FILE__, __func__);
		}
		mSortRecordsA = make_unique<ShaderStorageBuffer>();
		mSortRecordsA->create(recordBytes, nullptr, "Particle depth sort records A");
		mSortRecordsB = make_unique<ShaderStorageBuffer>();
		mSortRecordsB->create(recordBytes, nullptr, "Particle depth sort records B");
		mRadixHistogram = make_unique<ShaderStorageBuffer>();
		mRadixHistogram->create(histogramBytes, nullptr, "Particle radix group histograms");
		mSortDispatchCommand = make_unique<ShaderStorageBuffer>();
		mSortDispatchCommand->create(DispatchCommandBytes, nullptr, "Particle depth sort dispatch command");
	}

	void ParticleSystem::dispatchDepthSorts()
	{
		bool const hasDepthSort = any_of(mTemplateRenderData.begin(), mTemplateRenderData.end(),
			particleAppearanceRequiresDepthSort);
		if (!hasDepthSort) return;
		ensureSortBuffersAllocated();
		finishSimulationTiming();
		beginSortTiming();

		for (uint32_t templateIndex = 0; templateIndex < mTemplateRenderData.size(); ++templateIndex)
		{
			if (!particleAppearanceRequiresDepthSort(mTemplateRenderData[templateIndex])) continue;

			mIndirectCommands->bindStorage(0u);
			mSortDispatchCommand->bindStorage(1u);
			auto* prepare = static_cast<ComputeProgram*>(mSortPrepareProgram.get());
			prepare->use();
			prepare->setUniform("TEMPLATE_INDEX", templateIndex);
			prepare->dispatch(1u);
			GL_CHECK(glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_COMMAND_BARRIER_BIT));

			mParticlePool->bindStorage(0u);
			mRenderIndices->bindStorage(1u);
			mIndirectCommands->bindStorage(2u);
			mSortRecordsA->bindStorage(3u);
			auto* keys = static_cast<ComputeProgram*>(mSortKeyProgram.get());
			keys->use();
			keys->setUniform("TEMPLATE_INDEX", templateIndex);
			mSortDispatchCommand->bindDispatchIndirect();
			keys->dispatchIndirect();
			GL_CHECK(glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT));

			ShaderStorageBuffer* input = mSortRecordsA.get();
			ShaderStorageBuffer* output = mSortRecordsB.get();
			for (uint32_t shift = 0u; shift < 32u; shift += 4u)
			{
				input->bindStorage(0u);
				mIndirectCommands->bindStorage(1u);
				mRadixHistogram->bindStorage(2u);
				auto* histogram = static_cast<ComputeProgram*>(mRadixHistogramProgram.get());
				histogram->use();
				histogram->setUniform("TEMPLATE_INDEX", templateIndex);
				histogram->setUniform("RADIX_SHIFT", shift);
				mSortDispatchCommand->bindDispatchIndirect();
				histogram->dispatchIndirect();
				GL_CHECK(glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT));

				mIndirectCommands->bindStorage(0u);
				mRadixHistogram->bindStorage(1u);
				auto* prefix = static_cast<ComputeProgram*>(mRadixPrefixProgram.get());
				prefix->use();
				prefix->setUniform("TEMPLATE_INDEX", templateIndex);
				prefix->dispatch(1u);
				GL_CHECK(glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT));

				input->bindStorage(0u);
				output->bindStorage(1u);
				mIndirectCommands->bindStorage(2u);
				mRadixHistogram->bindStorage(3u);
				auto* scatter = static_cast<ComputeProgram*>(mRadixScatterProgram.get());
				scatter->use();
				scatter->setUniform("TEMPLATE_INDEX", templateIndex);
				scatter->setUniform("RADIX_SHIFT", shift);
				mSortDispatchCommand->bindDispatchIndirect();
				scatter->dispatchIndirect();
				GL_CHECK(glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT));
				swap(input, output);
			}

			input->bindStorage(0u);
			mRenderIndices->bindStorage(1u);
			mIndirectCommands->bindStorage(2u);
			auto* finalize = static_cast<ComputeProgram*>(mSortFinalizeProgram.get());
			finalize->use();
			finalize->setUniform("TEMPLATE_INDEX", templateIndex);
			mSortDispatchCommand->bindDispatchIndirect();
			finalize->dispatchIndirect();
			GL_CHECK(glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_COMMAND_BARRIER_BIT));
		}
		GL_CHECK(glBindBuffer(GL_DISPATCH_INDIRECT_BUFFER, 0));
		finishSortTiming();
	}

	void ParticleSystem::resetSimulationClock()
	{
		mLastSimulationTime = chrono::steady_clock::now();
		mHasLastSimulationTime = true;
	}

	optional<float> ParticleSystem::resolveSimulationDelta(float realDeltaSeconds)
	{
		if (mPendingSimulationStep)
		{
			float const step = *mPendingSimulationStep;
			mPendingSimulationStep.reset();
			return step;
		}
		if (mSimulationPaused || mSimulationTimeScale == 0.0f) return nullopt;
		return clampParticleDeltaSeconds(realDeltaSeconds * mSimulationTimeScale);
	}

	void ParticleSystem::pauseSimulation()
	{
		mSimulationPaused = true;
		resetSimulationClock();
	}

	void ParticleSystem::resumeSimulation()
	{
		mSimulationPaused = false;
		mPendingSimulationStep.reset();
		resetSimulationClock();
	}

	void ParticleSystem::setSimulationTimeScale(float scale)
	{
		if (!isfinite(scale) || scale < 0.0f)
			throw invalid_argument("Particle simulation time scale must be finite and non-negative.");
		mSimulationTimeScale = scale;
		resetSimulationClock();
	}

	void ParticleSystem::requestSimulationStep(float deltaSeconds)
	{
		if (!isfinite(deltaSeconds) || deltaSeconds <= 0.0f || deltaSeconds > MaximumParticleDeltaSeconds)
			throw invalid_argument("Particle simulation step must be finite, positive, and no greater than MaximumParticleDeltaSeconds.");
		mSimulationPaused = true;
		mPendingSimulationStep = deltaSeconds;
		resetSimulationClock();
	}

	void ParticleSystem::simulate()
	{
		initialise();

		try
		{
			pollEventReadback();
			advanceStatisticsFrame();
			auto const now = chrono::steady_clock::now();
			float realDeltaSeconds = 0.0f;
			if (mHasLastSimulationTime)
				realDeltaSeconds = chrono::duration<float>(now - mLastSimulationTime).count();
			mLastSimulationTime = now;
			mHasLastSimulationTime = true;
			auto const resolvedDelta = resolveSimulationDelta(realDeltaSeconds);
			if (!resolvedDelta) return;
			float const dt = *resolvedDelta;
			mSimulationSeconds += dt;

			if (!hasOccupiedEmitters()) return;
			buildSpawnCommands(dt);
			if (!mAvailable)
			{
				// CPU ownership and one-shot retirement remain deterministic even on a
				// renderer where the GPU particle path is unavailable.
				mSpawnCommands.clear();
				retireCompletedEmitters();
				return;
			}
			ensurePoolAllocated();
			uploadFrameData();
			dispatchStatisticsPrepare();
			if (!mGpuEventRules.empty()) dispatchEventPrepare(0u);
			beginStatisticsSample();
			{
				GpuDebugScope spawnScope("Particles: Spawn");
				dispatchSpawnCommands();
			}
			{
				GpuDebugScope simulationScope("Particles: Simulate");
				dispatchSimulation(dt);
			}
			{
				GpuDebugScope eventScope("Particles: Process secondary and external events");
				dispatchParticleEvents();
			}
			{
				GpuDebugScope compactionScope("Particles: Compact by emitter template");
				dispatchCompaction();
			}
			finishSimulationTiming();
			if (any_of(mTemplateRenderData.begin(), mTemplateRenderData.end(), particleAppearanceRequiresDepthSort))
			{
				GpuDebugScope sortScope("Particles: Depth radix sort");
				dispatchDepthSorts();
			}
			// Retirement happens only after this frame's simulation has consumed the
			// old emitter record. Reusing a slot before then could retarget a particle.
			retireCompletedEmitters();
			finishStatisticsSample();
		}
		catch (exception const& error)
		{
			disableWithWarning(error.what());
		}
	}

	void ParticleSystem::render(ParticleBlendClass blendClass, ResourcePtr const& sceneDepthResource)
	{
		setScreenSpaceCollisionDepth(sceneDepthResource);
		render(blendClass, dynamic_cast<RenderTexture*>(sceneDepthResource.get()));
	}

	void ParticleSystem::render(ParticleBlendClass blendClass, RenderTexture* sceneDepth)
	{
		initialise();
		if (!mAvailable || !mPoolAllocated) return;
		updateViewEffectSubmissions();

		uint32_t const requestedClass = uint32_t(blendClass);
		if (none_of(mTemplateRenderData.begin(), mTemplateRenderData.end(), [requestedClass](auto const& appearance)
			{ return appearance.modes[3] == requestedClass; })) return;

		GpuDebugScope scope("Particles: Draw blend class " + to_string(requestedClass));
		// The pass owns blend/cull/depth-test state, but depth writes are a particle
		// invariant. Force the GL state through RenderSystem so its cache remains
		// coherent when the graph scope restores authored state.
		mwRenderSystem->setDepthWriteState(false, true);
		auto const& drawResource = blendClass == ParticleBlendClass::WeightedOit ? mWeightedOitDrawProgram : mDrawProgram;
		auto* program = static_cast<ParticleDrawProgram*>(drawResource.get());
		program->use();
		program->setUniform("SCENE_DEPTH", int32_t(0));
		program->setUniform("PARTICLE_CURVE_LUT", int32_t(1));
		program->setUniform("PARTICLE_ALBEDO_ARRAY", int32_t(2));
		program->setUniform("HAS_SCENE_DEPTH", int32_t(sceneDepth ? 1 : 0));
		if (sceneDepth) sceneDepth->bindDepth(0);
		GL_CHECK(glActiveTexture(GL_TEXTURE2));
		GL_CHECK(glBindTexture(GL_TEXTURE_2D_ARRAY, mAlbedoArrayTexture));

		mParticlePool->bindStorage(ParticlePoolBinding);
		mRenderIndices->bindStorage(FreeIndicesBinding);
		GL_CHECK(glBindBufferRange(GL_SHADER_STORAGE_BUFFER, EmitterBinding, mEmitterBuffer->getBuffer(),
			static_cast<GLintptr>(mEmitterBuffer->getActiveOffset()), static_cast<GLsizeiptr>(bytes(mEmitters))));
		GL_CHECK(glBindBufferRange(GL_SHADER_STORAGE_BUFFER, TemplateRenderBinding, mTemplateRenderBuffer->getBuffer(),
			static_cast<GLintptr>(mTemplateRenderBuffer->getActiveOffset()), static_cast<GLsizeiptr>(bytes(mTemplateRenderData))));
		mIndirectCommands->bindDrawIndirect();

		GL_CHECK(glBindVertexArray(mVertexArray));
		// Authored hierarchy order is independent of blend class. Draw adjacent
		// commands together only while class, LUT, and view submission agree.
		size_t command = 0u;
		beginRenderTiming();
		while (command < mTemplateRenderData.size())
		{
			if (mTemplateRenderData[command].modes[3] != requestedClass ||
				!isEmitterSubmittedForCurrentView(uint32_t(command)))
			{
				++command;
				continue;
			}
			auto const& lut = mTemplateCurveLuts[command];
			if (!lut) THROW_MPP("A particle emitter template has no baked curve LUT.", __LINE__, __FILE__, __func__);
			size_t groupEnd = command + 1u;
			while (groupEnd < mTemplateRenderData.size() &&
				mTemplateRenderData[groupEnd].modes[3] == requestedClass &&
				mTemplateCurveLuts[groupEnd] == lut && isEmitterSubmittedForCurrentView(uint32_t(groupEnd))) ++groupEnd;
			lut->bind(1u);
			auto const commandOffset = reinterpret_cast<void const*>(command * sizeof(ParticleDrawArraysIndirectCommand));
			GL_CHECK(glMultiDrawArraysIndirect(GL_TRIANGLE_STRIP, commandOffset,
				static_cast<GLsizei>(groupEnd - command), sizeof(ParticleDrawArraysIndirectCommand)));
			command = groupEnd;
		}
		finishRenderTiming();
		GL_CHECK(glBindVertexArray(0));
		GL_CHECK(glBindBuffer(GL_DRAW_INDIRECT_BUFFER, 0));
		GL_CHECK(glActiveTexture(GL_TEXTURE2));
		GL_CHECK(glBindTexture(GL_TEXTURE_2D_ARRAY, 0));
		GL_CHECK(glActiveTexture(GL_TEXTURE1));
		GL_CHECK(glBindTexture(GL_TEXTURE_2D, 0));
		if (sceneDepth)
		{
			GL_CHECK(glActiveTexture(GL_TEXTURE0));
			GL_CHECK(glBindTexture(GL_TEXTURE_2D, 0));
		}
		mEmitterBuffer->markUsed();
		mTemplateRenderBuffer->markUsed();
	}

	void ParticleSystem::renderDistortion(ResourcePtr const& sceneDepthResource)
	{
		setScreenSpaceCollisionDepth(sceneDepthResource);
		renderDistortion(dynamic_cast<RenderTexture*>(sceneDepthResource.get()));
	}

	void ParticleSystem::renderDistortion(RenderTexture* sceneDepth)
	{
		initialise();
		if (!mAvailable || !mPoolAllocated ||
			!any_of(mTemplateRenderData.begin(), mTemplateRenderData.end(), particleAppearanceWritesDistortion)) return;
		updateViewEffectSubmissions();

		GpuDebugScope scope("Particles: Draw distortion output");
		mwRenderSystem->setDepthWriteState(false, true);
		auto* program = static_cast<ParticleDrawProgram*>(mDistortionDrawProgram.get());
		program->use();
		program->setUniform("SCENE_DEPTH", int32_t(0));
		program->setUniform("PARTICLE_CURVE_LUT", int32_t(1));
		program->setUniform("PARTICLE_ALBEDO_ARRAY", int32_t(2));
		program->setUniform("HAS_SCENE_DEPTH", int32_t(sceneDepth ? 1 : 0));
		if (sceneDepth) sceneDepth->bindDepth(0);
		GL_CHECK(glActiveTexture(GL_TEXTURE2));
		GL_CHECK(glBindTexture(GL_TEXTURE_2D_ARRAY, mAlbedoArrayTexture));

		mParticlePool->bindStorage(ParticlePoolBinding);
		mRenderIndices->bindStorage(FreeIndicesBinding);
		GL_CHECK(glBindBufferRange(GL_SHADER_STORAGE_BUFFER, EmitterBinding, mEmitterBuffer->getBuffer(),
			static_cast<GLintptr>(mEmitterBuffer->getActiveOffset()), static_cast<GLsizeiptr>(bytes(mEmitters))));
		GL_CHECK(glBindBufferRange(GL_SHADER_STORAGE_BUFFER, TemplateRenderBinding, mTemplateRenderBuffer->getBuffer(),
			static_cast<GLintptr>(mTemplateRenderBuffer->getActiveOffset()), static_cast<GLsizeiptr>(bytes(mTemplateRenderData))));
		mIndirectCommands->bindDrawIndirect();
		GL_CHECK(glBindVertexArray(mVertexArray));
		beginRenderTiming();
		for (size_t command = 0; command < mTemplateRenderData.size(); ++command)
		{
			if (!particleAppearanceWritesDistortion(mTemplateRenderData[command]) ||
				!isEmitterSubmittedForCurrentView(uint32_t(command))) continue;
			auto const& lut = mTemplateCurveLuts[command];
			if (!lut) THROW_MPP("A distortion particle emitter template has no baked curve LUT.", __LINE__, __FILE__, __func__);
			lut->bind(1u);
			auto const commandOffset = reinterpret_cast<void const*>(command * sizeof(ParticleDrawArraysIndirectCommand));
			GL_CHECK(glDrawArraysIndirect(GL_TRIANGLE_STRIP, commandOffset));
		}
		finishRenderTiming();
		GL_CHECK(glBindVertexArray(0));
		GL_CHECK(glBindBuffer(GL_DRAW_INDIRECT_BUFFER, 0));
		GL_CHECK(glActiveTexture(GL_TEXTURE2));
		GL_CHECK(glBindTexture(GL_TEXTURE_2D_ARRAY, 0));
		GL_CHECK(glActiveTexture(GL_TEXTURE1));
		GL_CHECK(glBindTexture(GL_TEXTURE_2D, 0));
		if (sceneDepth)
		{
			GL_CHECK(glActiveTexture(GL_TEXTURE0));
			GL_CHECK(glBindTexture(GL_TEXTURE_2D, 0));
		}
		mEmitterBuffer->markUsed();
		mTemplateRenderBuffer->markUsed();
	}

	void ParticleSystem::renderVolumetricLighting(ResourcePtr const& sceneDepthResource)
	{
		renderVolumetricLighting(dynamic_cast<RenderTexture*>(sceneDepthResource.get()));
	}

	void ParticleSystem::renderVolumetricLighting(RenderTexture* sceneDepth)
	{
		initialise();
		if (!mAvailable || !mPoolAllocated || mVolumetricLightingGpuData.empty()) return;
		updateViewEffectSubmissions();
		mVisibleVolumetricLightingGpuData.clear();
		for (size_t index = 0; index < mVolumetricLightingGpuData.size(); ++index)
			if (index < mVolumetricLightingEmitters.size() &&
				isEmitterSubmittedForCurrentView(mVolumetricLightingEmitters[index]))
				mVisibleVolumetricLightingGpuData.push_back(mVolumetricLightingGpuData[index]);
		if (mVisibleVolumetricLightingGpuData.empty()) return;
		mVolumetricLightingBuffer->upload(mVisibleVolumetricLightingGpuData.data(),
			bytes(mVisibleVolumetricLightingGpuData), 0, bytes(mVisibleVolumetricLightingGpuData));

		GpuDebugScope scope("Particles: Draw emitter-level volumetric lighting");
		mwRenderSystem->setDepthWriteState(false, true);
		auto* program = static_cast<ParticleDrawProgram*>(mVolumetricLightingDrawProgram.get());
		program->use();
		program->setUniform("SCENE_DEPTH", int32_t(0));
		program->setUniform("HAS_SCENE_DEPTH", int32_t(sceneDepth ? 1 : 0));
		if (sceneDepth) sceneDepth->bindDepth(0);
		GL_CHECK(glBindBufferRange(GL_SHADER_STORAGE_BUFFER, 0u, mVolumetricLightingBuffer->getBuffer(),
			static_cast<GLintptr>(mVolumetricLightingBuffer->getActiveOffset()),
			static_cast<GLsizeiptr>(bytes(mVisibleVolumetricLightingGpuData))));
		GL_CHECK(glBindVertexArray(mVertexArray));
		beginRenderTiming();
		GL_CHECK(glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4,
			static_cast<GLsizei>(mVisibleVolumetricLightingGpuData.size())));
		finishRenderTiming();
		GL_CHECK(glBindVertexArray(0));
		if (sceneDepth)
		{
			GL_CHECK(glActiveTexture(GL_TEXTURE0));
			GL_CHECK(glBindTexture(GL_TEXTURE_2D, 0));
		}
		mVolumetricLightingBuffer->markUsed();
	}

	void ParticleSystem::bindMeshParticleMaterial(ResourcePtr const& materialResource, uint32_t templateIndex)
	{
		auto* material = dynamic_cast<Material*>(materialResource.get());
		if (!material) THROW_MPP("A mesh particle requires a Material resource.", __LINE__, __FILE__, __func__);
		materialResource->load();
		auto programResource = material->getProgram();
		auto* program = dynamic_cast<Program*>(programResource.get());
		if (!program) THROW_MPP("A mesh-particle material requires a generated Program.", __LINE__, __FILE__, __func__);
		program->bind();
		material->setUniforms();
		mwRenderSystem->mActivePipelineUniformOverrides.bindUniforms(programResource);

		if (auto location = program->getViewPosId(); location >= 0)
		{
			auto const inverseView = glm::inverse(mwRenderSystem->m3dCameraMatrix);
			glm::vec3 const position(inverseView[3]);
			GL_CHECK(glUniform3fv(location, 1, glm::value_ptr(position)));
		}
		if (auto location = program->getUniformId("GAMMA"); location >= 0)
			GL_CHECK(glUniform1f(location, mwRenderSystem->mGamma));
		GL_CHECK(glUniform1i(program->getUniformId("MPP_PARTICLE_MESH_ENABLED"), 1));
		GL_CHECK(glUniform1ui(program->getUniformId("MPP_PARTICLE_MESH_TEMPLATE"), templateIndex));

		auto const textureCount = uint32_t(material->getNumTextures());
		if (textureCount > mwRenderSystem->getCaps().maxFragmentTextureUnits)
			THROW_MPP("A mesh-particle material requires more fragment texture units than supported by this renderer.", __LINE__, __FILE__, __func__);
		Texture const* prefilteredSpecular = nullptr;
		Texture const* resolvedSceneColour = nullptr;
		for (uint32_t textureIndex = 0u; textureIndex < textureCount; ++textureIndex)
		{
			auto textureResource = material->getTexture(int(textureIndex));
			auto const& sampler = program->getSamplerName(int(textureIndex));
			auto* activeShadow = dynamic_cast<RenderTexture*>(mwRenderSystem->mActiveShadowDepthTarget.get());
			bool const activePointShadow = activeShadow && activeShadow->getAttachmentTextureTarget() == GL_TEXTURE_CUBE_MAP;
			if (activeShadow && ((sampler == "SHADOW_MAP" && !activePointShadow) ||
				(sampler == "POINT_SHADOW_MAP" && activePointShadow)))
			{
				activeShadow->bindDepth(textureIndex);
				continue;
			}
			auto override = mwRenderSystem->mActivePipelineSamplerOverrides.find(sampler);
			if (override != mwRenderSystem->mActivePipelineSamplerOverrides.end() && override->second)
				textureResource = override->second;
			auto* depthTarget = dynamic_cast<RenderTexture*>(textureResource.get());
			if (depthTarget && depthTarget->getNumColourAttachments() == 0u && depthTarget->getDepthTextureId() != 0u)
				depthTarget->bindDepth(textureIndex);
			else
			{
				auto* texture = dynamic_cast<Texture*>(textureResource.get());
				if (!texture) THROW_MPP("A mesh-particle material sampler is not a Texture resource.", __LINE__, __FILE__, __func__);
				texture->bind(textureIndex);
				if (sampler == "PBR_PREFILTERED_SPECULAR_MAP") prefilteredSpecular = texture;
				if (sampler == "PBR_SCENE_COLOUR_RESOLVED") resolvedSceneColour = texture;
			}
		}
		if (prefilteredSpecular)
			if (auto location = program->getUniformId("PBR_PREFILTERED_MAX_LOD"); location >= 0)
				GL_CHECK(glUniform1f(location, float(max(1u, prefilteredSpecular->getMipLevels()) - 1u)));
		if (resolvedSceneColour)
			if (auto location = program->getUniformId("PBR_SCENE_COLOUR_MAX_LOD"); location >= 0)
				GL_CHECK(glUniform1f(location, float(max(1u, resolvedSceneColour->getMipLevels()) - 1u)));
	}

	void ParticleSystem::renderMeshes()
	{
		initialise();
		if (!mAvailable || !mPoolAllocated || mMeshDrawRecords.empty()) return;
		updateViewEffectSubmissions();
		GpuDebugScope scope("Particles: Draw instanced real meshes");
		mParticlePool->bindStorage(ParticlePoolBinding);
		mRenderIndices->bindStorage(FreeIndicesBinding);
		mCompactionScratch->bindStorage(7u);
		mMeshIndirectCommands->bindDrawIndirect();
		beginRenderTiming();
		for (size_t drawIndex = 0u; drawIndex < mMeshDrawRecords.size(); ++drawIndex)
		{
			auto const& draw = mMeshDrawRecords[drawIndex];
			if (!isEmitterSubmittedForCurrentView(draw.templateIndex)) continue;
			auto* material = static_cast<Material*>(draw.material.get());
			bindMeshParticleMaterial(draw.material, draw.templateIndex);
			bool const transparent = material->isTransparent();
			mwRenderSystem->setDepthTestState(true, true);
			mwRenderSystem->setDepthWriteState(!transparent, true);
			mwRenderSystem->setCullState(material->isDoubleSided() ? GraphCullMode::None : GraphCullMode::Back, true);
			mwRenderSystem->setBlendState(transparent, true);
			if (transparent)
				mwRenderSystem->setBlendFunctionState(GraphBlendFactor::SourceAlpha, GraphBlendFactor::OneMinusSourceAlpha,
					GraphBlendFactor::One, GraphBlendFactor::OneMinusSourceAlpha, true);

			draw.mesh->bind(true);
			auto const offset = reinterpret_cast<void const*>(drawIndex * sizeof(ParticleMeshDrawIndirectCommand));
			if (draw.mesh->mIsIndexed)
			{
				GLenum const indexType = draw.mesh->mIndexWidth == 16u ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT;
				GL_CHECK(glDrawElementsIndirect(draw.mesh->mPrimitiveRenderType, indexType, offset));
			}
			else GL_CHECK(glDrawArraysIndirect(draw.mesh->mPrimitiveRenderType, offset));
			draw.mesh->bind(false);
			auto* program = static_cast<Program*>(material->getProgram().get());
			GL_CHECK(glUniform1i(program->getUniformId("MPP_PARTICLE_MESH_ENABLED"), 0));
		}
		finishRenderTiming();
		GL_CHECK(glBindBuffer(GL_DRAW_INDIRECT_BUFFER, 0));
	}
}
