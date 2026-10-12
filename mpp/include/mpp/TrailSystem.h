#pragma once

#include <chrono>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#include <glm/vec3.hpp>

#include "mpp/Config.h"
#include "mpp/Resource.h"
#include "mpp/TrailData.h"

namespace mpp
{
	class ParticleDrawProgram;
	class RenderSystem;
	class RenderTexture;
	class ResourceManager;
	class ShaderStorageBuffer;
	namespace detail { class PersistentMappedBuffer; }

	struct _MPPAPI TrailHandle
	{
		static constexpr uint32_t InvalidIndex = std::numeric_limits<uint32_t>::max();
		uint32_t index{ InvalidIndex };
		uint32_t generation{ 0u };
		explicit operator bool() const noexcept { return index != InvalidIndex && generation != 0u; }
		auto operator<=>(TrailHandle const&) const = default;
	};

	// Owns the trail primitive's position-history buffers and ribbon draw path.
	// ParticleSystem is deliberately not involved: no ParticleRecord, particle
	// pool slot, spawn command, or particle indirect command represents a trail.
	class _MPPAPI TrailSystem
	{
	public:
		static constexpr uint32_t MaxTrailCount = 1024u;
		static constexpr uint32_t MaxPointCount = 256u;
		static constexpr uint32_t CurveSampleCount = 256u;

	private:
		struct Slot
		{
			uint32_t generation{ 1u };
			uint32_t historyGeneration{ 1u };
			bool occupied{ false };
			bool stopping{ false };
			float stopSeconds{ 0.0f };
			TrailSpecification specification;
		};

		RenderSystem* mwRenderSystem;
		ResourceManager* mwResourceManager;
		ResourcePtr mUpdateProgram;
		ResourcePtr mDrawProgram;
		std::unique_ptr<ShaderStorageBuffer> mPoints;
		std::unique_ptr<ShaderStorageBuffer> mStates;
		std::unique_ptr<ShaderStorageBuffer> mIndirectCommands;
		std::unique_ptr<detail::PersistentMappedBuffer> mControlBuffer;
		std::vector<Slot> mSlots;
		std::vector<TrailControlData> mControls;
		std::vector<uint32_t> mFreeIndices;
		std::vector<uint32_t> mDirtyCurveSlots;
		uint32_t mVertexArray{ 0u };
		uint32_t mCurveLut{ 0u };
		uint32_t mWorkGroupSize{ 64u };
		bool mInitialised{ false };
		bool mAvailable{ false };
		bool mBuffersAllocated{ false };
		bool mHasLastSimulationTime{ false };
		std::chrono::steady_clock::time_point mLastSimulationTime{};
		float mSimulationSeconds{ 0.0f };

		void ensureBuffersAllocated();
		void uploadDirtyCurves();
		void simulate(float deltaSeconds);
		void reclaimStoppedTrails();
		void reclaim(uint32_t index);
		Slot* find(TrailHandle handle);
		Slot const* find(TrailHandle handle) const;
		static void validate(TrailSpecification const& specification);
		static std::vector<float> bakeCurveRows(TrailSpecification const& specification);
		friend _MPPAPI bool runParticleSystemCpuTests(std::string* failure);
		friend _MPPAPI bool runParticleGpuTests(RenderSystem* renderSystem, std::string* failure);

	public:
		TrailSystem(RenderSystem* renderSystem, ResourceManager* resourceManager);
		~TrailSystem();
		TrailSystem(TrailSystem const&) = delete;
		TrailSystem& operator=(TrailSystem const&) = delete;

		void initialise();
		bool isAvailable() const noexcept { return mAvailable; }
		bool isBuffersAllocated() const noexcept { return mBuffersAllocated; }

		TrailHandle createTrail(TrailSpecification const& specification, glm::vec3 const& position = {});
		// Immediate destruction clears the GPU history at the next simulation.
		void destroyTrail(TrailHandle trail);
		void setTrailPosition(TrailHandle trail, glm::vec3 const& position);
		// Stops adding points but retains and ages the existing ribbon. The handle
		// retires after its longest possible point lifetime without GPU readback.
		void stopTrail(TrailHandle trail);
		// Restarting begins a new, disconnected position history.
		void startTrail(TrailHandle trail);
		void clearTrail(TrailHandle trail);
		bool isAlive(TrailHandle trail) const { return find(trail) != nullptr; }
		size_t getLiveTrailCount() const;

		// Called once per renderer frame before graph execution. Position history
		// therefore advances once even when several views draw the same ribbon.
		void simulate();
		// Dedicated indirect strip draw, called only by MPP.TrailScene.
		void render(ParticleBlendClass blendClass, RenderTexture* sceneDepth = nullptr);
		void render(ParticleBlendClass blendClass, ResourcePtr const& sceneDepth);
	};
}
