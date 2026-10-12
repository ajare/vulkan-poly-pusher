#pragma once

#include <map>
#include <vector>

#include "mpp/Config.h"
#include "mpp/RenderGraph.h"
#include "mpp/RenderGraphGpuTests.h"
#include "mpp/RenderTarget.h"
#include "mpp/RenderTextureStream.h"

namespace mpp
{
	class RenderSystem;
	_MPPAPI RenderTextureOptions makeGraphRenderTextureOptions(GraphImageDesc const& desc);
	class RenderGraphImportRegistry;

	// Owns the first, deliberately non-aliased set of physical render targets
	// for a graph allocation plan. Imported targets remain application-owned.
	class _MPPAPI RenderGraphTargets
	{
		struct PoolEntry
		{
			GraphImageLifetime lifetime;
			RenderTargetPtr target;
			RenderTargetPtr writeTarget;
			uint32_t samples{ 1 };
		};

		RenderSystem* mRenderSystem;
		std::map<uint64_t, RenderTargetPtr> mTargets;
		std::map<uint64_t, RenderTargetPtr> mWriteTargets;
		std::map<uint32_t, RenderTargetPtr> mImportedTargets;
		struct TargetSignature
		{
			uint64_t width{ 0 }, height{ 0 };
			uint32_t textureTarget{ 0 }, colourTexture{ 0 }, depthTexture{ 0 };
			bool depthStencil{ false };
			bool operator ==(TargetSignature const&) const = default;
		};
		std::map<uint32_t, TargetSignature> mImportedSignatures;
		std::vector<PoolEntry> mPool;
		uint64_t mGeneration{ 1 };

		static uint64_t makeKey(GraphImageHandle image);
		static TargetSignature targetSignature(RenderTargetPtr const& target);
		void allocatePhysical(RenderGraphAllocationPlan const& plan, uint32_t samples);
		friend class RenderPipeline;
		friend _MPPAPI bool runRenderGraphGpuTests(RenderSystem* renderSystem, std::string* failure);

	public:
		explicit RenderGraphTargets(RenderSystem* renderSystem);

		// Reuses compatible targets retained from previous allocate() calls.
		void allocate(RenderGraphAllocationPlan const& plan);
		// An import identifies backing storage, so every version of an external
		// logical image resolves to this target.
		void bindImported(GraphImageHandle image, RenderTargetPtr target);
		// Validates the imported target against the graph's public descriptor.
		void bindImported(RenderGraph const& graph, GraphImageHandle image, RenderTargetPtr target);
		void bindImports(RenderGraph const& graph, RenderGraphImportRegistry const& imports);
		void clear();
		// Changes only when an attachment mapping acquires different backing
		// storage. Repeating an unchanged allocation keeps framebuffer views valid.
		uint64_t getGeneration() const;
		RenderTargetPtr get(GraphImageHandle image) const;
		RenderTargetPtr getWriteTarget(GraphImageHandle image) const;
		bool resolve(GraphImageHandle image, bool depth) const;
	};
}
