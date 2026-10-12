#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include <glm/mat4x4.hpp>

#include "mpp/Config.h"
#include "mpp/ParticleData.h"
#include "mpp/ParticleEffectBounds.h"
#include "mpp/PbrLight.h"
#include "mpp/Resource.h"

namespace mpp
{
	class RenderSystem;
	class ResourceManager;
	class ShaderStorageBuffer;
	class RenderTexture;
	class ParticleEffectCurveLut;
	class Mesh;
	namespace detail
	{
		class PersistentMappedBuffer;
		class ParticleStatisticsState;
		class ParticleEventReadbackState;
	}

	// A completed, asynchronous snapshot. sourceFrame identifies the renderer
	// frame that produced it; framesLagged makes its intentional staleness explicit.
	struct _MPPAPI ParticleStats
	{
		bool valid{ false };
		uint64_t sourceFrame{ 0 };
		uint32_t framesLagged{ 0 };
		uint32_t activeParticles{ 0 };
		uint32_t freeParticles{ 0 };
		uint32_t spawnedParticles{ 0 };
		uint32_t killedParticles{ 0 };
		uint32_t droppedParticles{ 0 };
		uint32_t renderedParticles{ 0 };
		// GPU per-particle rejection; effect-level CPU bounds do not alter it.
		uint32_t culledParticles{ 0 };
		// View-level totals accumulated across distinct view-projection states
		// sampled this frame.
		uint32_t submittedEffects{ 0 };
		uint32_t boundsCulledEffects{ 0 };
		uint32_t activeEmitters{ 0 };
		uint32_t capacity{ 0 };
		float capacityUsage{ 0.0f };
		double simulationGpuMilliseconds{ 0.0 };
		double sortingGpuMilliseconds{ 0.0 };
		double renderGpuMilliseconds{ 0.0 };
	};

	enum class ParticleParameter : uint32_t
	{
		SpawnRate,
		SizeScale,
		SpeedScale,
		LifetimeScale,
		AlphaScale,
		EmissiveScale
	};

	struct _MPPAPI ParticleEmitterHandle
	{
		static constexpr uint32_t InvalidIndex = std::numeric_limits<uint32_t>::max();
		uint32_t index{ InvalidIndex };
		uint32_t generation{ 0 };
		explicit operator bool() const noexcept { return index != InvalidIndex && generation != 0; }
		auto operator<=>(ParticleEmitterHandle const&) const = default;
	};

	struct _MPPAPI ParticleEffectHandle
	{
		static constexpr uint32_t InvalidIndex = std::numeric_limits<uint32_t>::max();
		uint32_t index{ InvalidIndex };
		uint32_t generation{ 0 };
		explicit operator bool() const noexcept { return index != InvalidIndex && generation != 0; }
		auto operator<=>(ParticleEffectHandle const&) const = default;
	};

	struct _MPPAPI ParticleProxyLight
	{
		ParticleEmitterHandle emitter;
		PbrLight light;
		bool injectedIntoPbr{ false };
	};

	// Runtime form of one authored emitter template. Asset implementations copy
	// their authored values into this structure; ParticleSystem then owns each
	// live emitter's mutable copy.
	struct _MPPAPI ParticleEmitterTemplate
	{
		EmitterSimData simulation{};
		TemplateRenderData appearance{};
		ParticleEmitterLighting lighting{};
		// The authored atlas resource belongs to the emitter template. ParticleSystem
		// copies each distinct atlas into one layer of its shared albedo array.
		ResourcePtr albedoTexture;
		// A mesh model selects the dedicated mesh-particle draw path. Every mesh in
		// the model is instanced; meshMaterial optionally overrides each embedded
		// mesh material while retaining that material's exact shader and textures.
		ResourcePtr meshModel;
		ResourcePtr meshMaterial;
		std::array<ParticleCurve, size_t(ParticleScalarCurve::Count)> curves{};
		ParticleGradient colourGradient{};
		std::vector<ParticleEventRule> events;
		glm::mat4 localTransform{ 1.0f };
	};

	// Narrow bridge between the CPU API and the particle effect Resource supplied
	// by the asset ticket. Keeping it abstract lets this API remain usable by
	// programmatic effects without taking ownership of parsing or authoring.
	class _MPPAPI ParticleEffectSource
	{
		mutable std::shared_ptr<ParticleEffectCurveLut> mCurveLut;

	protected:
		// Asset streams call this if authored templates are replaced before reload.
		void invalidateCurveLut() const;

	public:
		virtual ~ParticleEffectSource() = default;
		virtual std::span<ParticleEmitterTemplate const> getEmitterTemplates() const = 0;
		virtual std::optional<ParticleEffectBounds> getBounds() const { return std::nullopt; }
		// The first request performs the load-time CPU bake. The strong cache makes
		// one LUT belong to this reusable particle effect asset.
		std::shared_ptr<ParticleEffectCurveLut> getCurveLut() const;
	};

	// The GPU-driven particle system. The CPU owns effects, generational emitter
	// slots and spawn commands; particles and their allocation state stay on GPU.
	class _MPPAPI ParticleSystem
	{
	public:
		static constexpr uint32_t MaxEmitterCount = 4096;
		static constexpr uint32_t MaxTemplateCount = 4096;
		static constexpr uint32_t MaxSpawnCommandCount = 4096;
		static constexpr uint32_t MaxColliderCount = 1024;
		static constexpr uint32_t MaxMeshDrawCount = 16384;
		static constexpr uint32_t MaxEventRuleCount = 16384;
		static constexpr uint32_t MaxGeneratedEventCount = 32768;
		static constexpr uint32_t MaxExternalEventCount = 4096;
		static constexpr uint32_t MaxSecondaryEventCascadeDepth = 8;

	private:
		RenderSystem* mwRenderSystem;
		ResourceManager* mwResourceManager;

		ResourcePtr mPoolInitialiseProgram, mStatisticsPrepareProgram, mEventPrepareProgram, mSpawnProgram, mSimulationPrepareProgram, mSimulationProgram;
		ResourcePtr mEventProcessProgram, mCompactionPrepareProgram, mCompactionCountProgram, mCompactionPrefixProgram, mCompactionScatterProgram;
		ResourcePtr mSortPrepareProgram, mSortKeyProgram, mRadixHistogramProgram, mRadixPrefixProgram, mRadixScatterProgram, mSortFinalizeProgram;
		ResourcePtr mDrawProgram, mWeightedOitDrawProgram, mDistortionDrawProgram, mVolumetricLightingDrawProgram, mMeshCommandProgram;

		std::unique_ptr<ShaderStorageBuffer> mParticlePool;
		std::unique_ptr<ShaderStorageBuffer> mFreeIndices;
		std::unique_ptr<ShaderStorageBuffer> mActiveIndicesA;
		std::unique_ptr<ShaderStorageBuffer> mActiveIndicesB;
		std::unique_ptr<ShaderStorageBuffer> mRenderIndices;
		std::unique_ptr<ShaderStorageBuffer> mCounters;
		std::unique_ptr<ShaderStorageBuffer> mCompactionScratch;
		std::unique_ptr<ShaderStorageBuffer> mIndirectCommands;
		std::unique_ptr<ShaderStorageBuffer> mSimulationDispatchCommand;
		std::unique_ptr<ShaderStorageBuffer> mCompactionDispatchCommand;
		std::unique_ptr<ShaderStorageBuffer> mSortRecordsA;
		std::unique_ptr<ShaderStorageBuffer> mSortRecordsB;
		std::unique_ptr<ShaderStorageBuffer> mRadixHistogram;
		std::unique_ptr<ShaderStorageBuffer> mSortDispatchCommand;
		std::unique_ptr<ShaderStorageBuffer> mEventStorage;
		std::unique_ptr<ShaderStorageBuffer> mEventDispatchCommand;
		std::unique_ptr<ShaderStorageBuffer> mMeshIndirectCommands;
		std::unique_ptr<ShaderStorageBuffer> mMeshCommandTemplates;
		std::unique_ptr<detail::PersistentMappedBuffer> mEmitterBuffer;
		std::unique_ptr<detail::PersistentMappedBuffer> mTemplateRenderBuffer;
		std::unique_ptr<detail::PersistentMappedBuffer> mVolumetricLightingBuffer;
		std::unique_ptr<detail::PersistentMappedBuffer> mSpawnCommandBuffer;
		std::unique_ptr<detail::PersistentMappedBuffer> mColliderBuffer;
		std::unique_ptr<detail::ParticleStatisticsState> mStatistics;
		std::unique_ptr<detail::ParticleEventReadbackState> mEventReadback;

		std::vector<EmitterSimData> mEmitters;
		std::vector<std::vector<ParticleEventRule>> mEmitterEventRules;
		std::vector<ParticleGpuEventRule> mGpuEventRules;
		std::vector<TemplateRenderData> mTemplateRenderData;
		std::vector<ParticleEmitterLighting> mEmitterLighting;
		std::vector<ParticleVolumetricLightingGpuData> mVolumetricLightingGpuData;
		std::vector<ResourcePtr> mTemplateTextures;
		std::vector<ResourcePtr> mTemplateMeshModels;
		std::vector<ResourcePtr> mTemplateMeshMaterials;
		std::vector<std::shared_ptr<ParticleEffectCurveLut>> mTemplateCurveLuts;
		struct MeshDrawRecord
		{
			uint32_t templateIndex{ 0 };
			Mesh const* mesh{ nullptr };
			ResourcePtr material;
		};
		std::vector<MeshDrawRecord> mMeshDrawRecords;
		std::vector<uint32_t> mAlbedoArraySourceIds;
		uint32_t mAlbedoArrayTexture{ 0 };
		std::vector<ParticleSpawnCommand> mSpawnCommands;
		std::vector<ParticleCollider> mColliders;
		ParticleSignedDistanceFieldData mSignedDistanceFieldData;
		ResourcePtr mSignedDistanceFieldTexture;
		ResourcePtr mVectorFieldTexture;
		ResourcePtr mScreenSpaceCollisionDepth;
		struct EmitterSlot
		{
			uint32_t generation{ 1 };
			bool occupied{ false };
			bool pendingDestroy{ false };
			glm::mat4 localTransform{ 1.0f };
			ParticleSpawnAccumulator spawnAccumulator;
			uint32_t spawnCounter{ 0 };
			uint32_t eventGeneration{ 0 };
			bool burstSubmitted{ false };
			bool hasSpawned{ false };
			bool eventTarget{ false };
			bool eventTargetPersistent{ false };
			float lastSpawnSeconds{ 0.0f };
			float maximumSpawnedLifetime{ 0.0f };
			uint32_t effectIndex{ ParticleEffectHandle::InvalidIndex };
		};
		struct EffectSlot
		{
			uint32_t generation{ 1 };
			bool occupied{ false };
			glm::mat4 transform{ 1.0f };
			std::vector<ParticleEmitterHandle> emitters;
			std::optional<ParticleEffectBounds> bounds;
			bool boundsCullingEnabled{ true };
			ResourcePtr asset;
			std::shared_ptr<ParticleEffectCurveLut> curveLut;
		};
		std::vector<EmitterSlot> mEmitterSlots;
		std::vector<uint32_t> mFreeEmitterIndices;
		std::vector<EffectSlot> mEffectSlots;
		std::vector<uint32_t> mFreeEffectIndices;

		uint32_t mVertexArray{ 0 };
		uint32_t mNoiseTexture{ 0 };
		uint32_t mWorkGroupSize{ 64 };
		uint32_t mPoolCapacity{ 0 };
		uint32_t mActiveListIndex{ 0 };
		bool mInitialised{ false };
		bool mAvailable{ false };
		bool mPoolAllocated{ false };
		bool mEventRulesDirty{ true };
		bool mHasLastSimulationTime{ false };
		std::chrono::steady_clock::time_point mLastSimulationTime{};
		float mSimulationSeconds{ 0.0f };
		float mSimulationTimeScale{ 1.0f };
		std::optional<float> mPendingSimulationStep;
		bool mSimulationPaused{ false };
		mutable bool mViewBoundsValid{ false };
		mutable uint64_t mViewBoundsFrame{ 0 };
		mutable glm::mat4 mViewBoundsViewProjection{ 1.0f };
		mutable std::vector<uint8_t> mViewEffectSubmissions;
		std::vector<uint32_t> mVolumetricLightingEmitters;
		std::vector<ParticleVolumetricLightingGpuData> mVisibleVolumetricLightingGpuData;

		void ensurePoolAllocated();
		void createNoiseTexture();
		std::optional<float> resolveSimulationDelta(float realDeltaSeconds);
		void resetSimulationClock();
		void buildSpawnCommands(float dt);
		void retireCompletedEmitters();
		void uploadFrameData();
		void updateAlbedoTextureArray();
		void dispatchStatisticsPrepare();
		void dispatchEventPrepare(uint32_t mode, uint32_t sourceQueue = 0u);
		void dispatchSpawnCommands();
		void dispatchSimulation(float dt);
		void dispatchParticleEvents();
		void queueEventReadback();
		void pollEventReadback();
		void dispatchCompaction();
		void rebuildMeshDrawCommands();
		void dispatchMeshDrawCommands();
		void bindMeshParticleMaterial(ResourcePtr const& material, uint32_t templateIndex);
		void ensureSortBuffersAllocated();
		void dispatchDepthSorts();
		void advanceStatisticsFrame();
		void beginStatisticsSample();
		void finishSimulationTiming();
		void finishStatisticsSample();
		void beginSortTiming();
		void finishSortTiming();
		void beginRenderTiming();
		void finishRenderTiming();
		void disableWithWarning(std::string const& reason);
		ParticleEmitterHandle allocateEmitter(ParticleEmitterTemplate const& emitterTemplate, glm::mat4 const& effectTransform,
			std::shared_ptr<ParticleEffectCurveLut> const& curveLut, size_t emitterTemplateIndex, uint32_t effectIndex);
		ParticleEffectHandle createEffect(std::span<ParticleEmitterTemplate const> emitterTemplates, glm::mat4 const& transform,
			std::optional<ParticleEffectBounds> bounds, std::shared_ptr<ParticleEffectCurveLut> curveLut);
		void invalidateViewBounds();
		void updateViewEffectSubmissions() const;
		bool isEmitterSubmittedForCurrentView(uint32_t emitterIndex) const;
		EmitterSlot* findEmitter(ParticleEmitterHandle handle);
		EmitterSlot const* findEmitter(ParticleEmitterHandle handle) const;
		EffectSlot* findEffect(ParticleEffectHandle handle);
		void requestEmitterDestroy(uint32_t index);
		void reclaimEmitter(uint32_t index);
		void reclaimEffect(uint32_t index);
		bool hasOccupiedEmitters() const;
		void validateEventRules(std::span<ParticleEmitterTemplate const> emitterTemplates) const;
		void validateLighting(std::span<ParticleEmitterTemplate const> emitterTemplates) const;
		friend _MPPAPI bool runParticleSystemCpuTests(std::string* failure);
		friend _MPPAPI bool runParticleGpuTests(RenderSystem* renderSystem, std::string* failure);

	public:
		ParticleSystem(RenderSystem* renderSystem, ResourceManager* resourceManager);
		~ParticleSystem();
		ParticleSystem(ParticleSystem const&) = delete;
		ParticleSystem& operator =(ParticleSystem const&) = delete;

		void initialise();
		bool isAvailable() const { return mAvailable; }
		bool isPoolAllocated() const { return mPoolAllocated; }
		uint32_t getPoolCapacity() const { return mPoolCapacity; }

		// Runtime-only controls for the shared once-per-rendered-frame simulation.
		// A requested step supplies simulation seconds directly (independent of
		// wall time and time scale), consumes one future frame, and stays paused.
		void pauseSimulation();
		void resumeSimulation();
		bool isSimulationPaused() const { return mSimulationPaused; }
		void setSimulationTimeScale(float scale);
		float getSimulationTimeScale() const { return mSimulationTimeScale; }
		void requestSimulationStep(float deltaSeconds = 1.0f / 60.0f);

		// Disabled by default. Enabling lazily allocates the readback/query ring;
		// disabling immediately removes all particle-path polling and retrieval.
		void setStatisticsEnabled(bool enabled);
		bool isStatisticsEnabled() const;
		ParticleStats const& getStats() const;

		using ParticleEventCallback = std::function<void(ParticleEvent const&)>;
		// SecondaryParticleBurst is consumed on the GPU and cannot have a CPU
		// callback. Other actions use a frame-lagged fence ring that is polled with
		// zero timeout; callback delivery never waits for current GPU work.
		void setEventCallback(ParticleEventAction action, ParticleEventCallback callback);
		void clearEventCallback(ParticleEventAction action);
		bool hasEventCallback(ParticleEventAction action) const;

		// Returns at most one dynamic-light proxy per opted-in live emitter. The
		// renderer uses injectionOnly=true and its remaining fixed light capacity;
		// callers may inspect all proxies without exposing individual particles.
		std::vector<ParticleProxyLight> getProxyLights(size_t maximumCount = MaxEmitterCount,
			bool injectionOnly = false) const;

		ParticleEffectHandle createEffect(ResourcePtr const& asset, glm::mat4 const& transform = glm::mat4(1.0f));
		ParticleEffectHandle createEffect(ParticleEffectSource const& asset, glm::mat4 const& transform = glm::mat4(1.0f));
		ParticleEffectHandle createEffect(std::span<ParticleEmitterTemplate const> emitterTemplates, glm::mat4 const& transform = glm::mat4(1.0f));
		ParticleEffectHandle createEffect(std::span<ParticleEmitterTemplate const> emitterTemplates,
			ParticleEffectBounds const& bounds, glm::mat4 const& transform = glm::mat4(1.0f));
		void destroyEffect(ParticleEffectHandle effect);
		void setEffectTransform(ParticleEffectHandle effect, glm::mat4 const& transform);
		// Runtime-only per-effect preview/debug override. Authored bounds remain
		// installed and simulation is unaffected.
		void setEffectBoundsCullingEnabled(ParticleEffectHandle effect, bool enabled);
		bool isEffectBoundsCullingEnabled(ParticleEffectHandle effect) const;
		// Explicit synchronous inspection seam for editor tooling. Returns the
		// local-space visual extents of currently live particles in this effect.
		std::optional<ParticleEffectBounds> sampleObservedEffectBounds(ParticleEffectHandle effect) const;
		// Visibility changes affect every existing particle in the live particle
		// effect on the next GPU compaction, without stopping its emitters.
		void setEffectVisibilityFlags(ParticleEffectHandle effect, uint32_t flags);
		void setEffectVisible(ParticleEffectHandle effect, bool visible);
		void spawnEffect(ResourcePtr const& asset, glm::mat4 const& transform = glm::mat4(1.0f));
		void spawnEffect(ParticleEffectSource const& asset, glm::mat4 const& transform = glm::mat4(1.0f));
		void spawnEffect(std::span<ParticleEmitterTemplate const> emitterTemplates, glm::mat4 const& transform = glm::mat4(1.0f));
		void spawnEffect(std::span<ParticleEmitterTemplate const> emitterTemplates,
			ParticleEffectBounds const& bounds, glm::mat4 const& transform = glm::mat4(1.0f));

		ParticleEmitterHandle getEmitter(ParticleEffectHandle effect, size_t index) const;
		void destroyEmitter(ParticleEmitterHandle emitter);
		void setEmitterTransform(ParticleEmitterHandle emitter, glm::mat4 const& transform);
		void setEmitterParameter(ParticleEmitterHandle emitter, ParticleParameter parameter, float multiplier);
		// Queues a runtime-only burst for one live emitter. This does not alter its
		// authored emission mode, enabled state, or burst count. The command is
		// consumed by the next simulation frame (including an explicit paused step).
		void requestEmitterBurst(ParticleEmitterHandle emitter, uint32_t count);
		// Applies authored spawn and billboard values without replacing the live
		// emitter. Runtime transform, parameter multipliers, visibility, curve rows,
		// texture layers, mesh selection, and event routing remain independently owned.
		void updateEmitterTemplateRuntime(ParticleEmitterHandle emitter, EmitterSimData const& simulation,
			TemplateRenderData const& appearance);
		// Analytical colliders are shared world data scanned by every emitter that
		// enables the analytical collision source. Replacing the span is atomic from
		// the next simulated frame's point of view.
		void setColliders(std::span<ParticleCollider const> colliders);
		std::span<ParticleCollider const> getColliders() const { return mColliders; }
		// The SDF texture must be Texture3D. worldToTexture maps world positions into
		// its [0,1]^3 domain and red stores signed distance around isoValue.
		void setSignedDistanceField(ResourcePtr texture, glm::mat4 const& worldToTexture = glm::mat4(1.0f),
			float distanceScale = 1.0f, float isoValue = 0.0f);
		void clearSignedDistanceField();
		// One arbitrary RGB 3D vector field may be shared by emitters. Texture values
		// are decoded from [0,1] to [-1,1]; each emitter authors its own mapping and strength.
		void setVectorField(ResourcePtr texture);
		void clearVectorField();
		// Graph particle passes retain the last main scene-depth resource. Simulation
		// samples that previous completed frame, preserving ADR 0005's once-per-frame
		// pre-graph dispatch.
		void setScreenSpaceCollisionDepth(ResourcePtr sceneDepth);
		void startEmitter(ParticleEmitterHandle emitter);
		void stopEmitter(ParticleEmitterHandle emitter);
		bool isAlive(ParticleEmitterHandle emitter) const { return findEmitter(emitter) != nullptr; }
		bool isAlive(ParticleEffectHandle effect) const;
		size_t getLiveEmitterCount() const;
		size_t getLiveEffectCount() const;

		// Spawn preparation and simulation are called once per rendered frame outside
		// graph passes. RenderSystem guards the frame serial before entering here.
		void simulate();
		// Draw only the contiguous command span selected by this authored pass.
		// A null scene depth selects hard-edged particles.
		void render(ParticleBlendClass blendClass, RenderTexture* sceneDepth);
		void render(ParticleBlendClass blendClass, ResourcePtr const& sceneDepth);
		// Draws only billboard appearances that opt into distortion output. The
		// authored graph owns the additive RG distortion target and composite step.
		void renderDistortion(RenderTexture* sceneDepth);
		void renderDistortion(ResourcePtr const& sceneDepth);
		// Draws one depth-aware additive proxy volume per opted-in emitter. This is
		// a render-graph contribution, not a collection of per-particle lights.
		void renderVolumetricLighting(RenderTexture* sceneDepth);
		void renderVolumetricLighting(ResourcePtr const& sceneDepth);
		// Dedicated real-mesh pass. Unlike billboard appearances this binds model
		// vertex arrays and each template's Material, then issues GPU-authored
		// indirect instanced draws without particle readback.
		void renderMeshes();
	};
}
