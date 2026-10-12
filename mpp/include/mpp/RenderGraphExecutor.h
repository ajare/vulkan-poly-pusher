#pragma once

#include <deque>
#include <functional>
#include <map>
#include <memory>

#include "mpp/Config.h"
#include "mpp/RenderGraph.h"
#include "mpp/RenderGraphTargets.h"
#include "mpp/RenderGraphFrameContext.h"

namespace mpp
{
	class RenderSystem;

	struct _MPPAPI GraphPassExecutionStats
	{
		GraphPassHandle pass;
		std::string name;
		double cpuMilliseconds{ 0.0 };
		double gpuMilliseconds{ 0.0 };
		bool gpuTimingAvailable{ false };
		bool gpuTimingSupported{ false };
		uint64_t primitivesSubmitted{ 0 };
		uint64_t trianglesSubmitted{ 0 };
		uint64_t fullscreenQuads{ 0 };
		// The executed framebuffer contract, useful to diagnostics and GPU tests
		// that verify optional MRT resources disappear when their gate is disabled.
		uint32_t colourOutputCount{ 0 };
		uint32_t depthOutputCount{ 0 };
		uint32_t storedDepthOutputCount{ 0 };
		uint32_t samplerBindingCount{ 0 };
		uint32_t maxColourOutputMipLevels{ 0 };
		// The first colour attachment is the pass's primary observable output.
		// Generated Planar passes have exactly one, so telemetry can report the
		// stable image name and its actual allocated dimensions directly.
		std::string primaryColourOutputName;
		uint32_t primaryColourOutputWidth{ 0 };
		uint32_t primaryColourOutputHeight{ 0 };
	};

	// One stored attachment read back immediately after its producing pass.
	// Pixels are top-left-origin RGB8, ready for application-side PNG encoding.
	struct _MPPAPI GraphImageCapture
	{
		std::string passName;
		std::string imageName;
		uint32_t width{ 0 }, height{ 0 };
		bool depth{ false };
		uint32_t cubeFace{ GraphNoCubeFace };
		std::vector<uint8_t> pixels;
	};

	struct _MPPAPI GraphFramebufferCacheStats
	{
		uint64_t hits{ 0 };
		uint64_t misses{ 0 };
		uint64_t invalidations{ 0 };
		uint64_t entries{ 0 };
	};
	class RenderGraphPassFactoryRegistry;
	class RenderGraphScenePass;
	class RenderGraphTemplate;

	class _MPPAPI RenderGraphExecutionContext
	{
		RenderGraphTargets const* mTargets;
		UniformCollection const* mParameters;
		RenderGraphFrameContext const* mFrame;
		GraphPassInfo const* mPass;

	public:
		explicit RenderGraphExecutionContext(RenderGraphTargets const* targets, UniformCollection const* parameters = nullptr, RenderGraphFrameContext const* frame = nullptr, GraphPassInfo const* pass = nullptr);
		RenderTargetPtr getImage(GraphImageHandle image) const;
		UniformCollection const& getParameters() const;
		RenderGraphFrameContext const& getFrame() const;
		GraphPassInfo const& getPass() const;
	};

	// Executes graphics passes supplied by application callbacks. Framebuffer
	// views are cached while their RenderGraphTargets backing generation remains
	// unchanged; graph textures remain owned by RenderGraphTargets.
	class _MPPAPI RenderGraphExecutor
	{
		RenderSystem* mRenderSystem;
		RenderGraphPassFactoryRegistry const* mFactoryRegistry{ nullptr };
		RenderGraphTemplate const* mExecutingTemplate{ nullptr };
		RenderGraphFrameContext const* mFrameContext{ nullptr };
		// Keyed by pass NAME, not by GraphPassHandle::id. The handle's id is a
		// positional index into RenderGraph::mPasses, and removePass, movePass and
		// reorderPasses all renumber it -- so an index key silently hands one pass's
		// state to whichever pass later occupies that slot. A stateful scene pass
		// inheriting another pass's TAA history is the worst version of this. Pass
		// names are enforced unique graph-wide by addPass and setPassName, so they
		// are the stable authored identifier this needs.
		std::map<std::string, std::function<void(RenderGraphExecutionContext const&)>> mCallbacks;
		std::map<std::string, std::unique_ptr<RenderGraphScenePass>> mScenePasses;
		std::map<std::string, UniformCollection> mParameterOverrides;
		std::vector<GraphPassExecutionStats> mLastExecutionStats;
		std::vector<GraphPassHandle> mLastExecutionOrder;
		std::string mLastFailedPassName;
		struct GpuTimingQuery { GraphPassHandle pass; std::string name; uint32_t begin{ 0 }, end{ 0 }; };
		struct GpuTimingResult { std::string name; double milliseconds{ 0.0 }; };
		std::deque<std::vector<GpuTimingQuery>> mPendingGpuTimings;
		std::map<std::string, GpuTimingResult> mGpuTimings;
		bool mGpuTimingSupported{ false };
		struct FramebufferViewCache;
		std::unique_ptr<FramebufferViewCache> mFramebufferViews;
		GraphFramebufferCacheStats mFramebufferCacheStats;
		bool mCaptureNextExecution{ false };
		std::vector<GraphImageCapture> mLastImageCaptures;
		void collectGpuTimings();
		void clearGpuTimings();
		void synchronizeFramebufferViews(RenderGraphTargets const& targets);
		RenderTargetPtr getFramebufferView(std::string const& name, RenderGraphTargets const& targets,
			std::vector<RenderTargetPtr> const& colours, std::vector<uint32_t> const& colourMips, std::vector<uint32_t> const& colourFaces,
			RenderTargetPtr const& depth, uint32_t depthMip, uint32_t depthFace);

	public:
		explicit RenderGraphExecutor(RenderSystem* renderSystem);
		~RenderGraphExecutor();
		RenderGraphExecutor(RenderGraphExecutor const&) = delete;
		RenderGraphExecutor& operator =(RenderGraphExecutor const&) = delete;

		// Registration is by pass name. The graph-and-handle overloads resolve it for
		// you and are the convenient form at call sites that already hold the graph;
		// they exist so that no caller has to remember an index is not an identity.
		void setPassCallback(std::string const& passName, std::function<void(RenderGraphExecutionContext const&)> callback);
		void setPassCallback(RenderGraph const& graph, GraphPassHandle pass, std::function<void(RenderGraphExecutionContext const&)> callback);
		void setPassFactoryRegistry(RenderGraphPassFactoryRegistry const* registry);
		void setFrameContext(RenderGraphFrameContext const* frameContext);
		void setPassParameterOverrides(std::string const& passName, UniformCollection const& parameters);
		void setPassParameterOverrides(RenderGraph const& graph, GraphPassHandle pass, UniformCollection const& parameters);
		void clearPassCallbacks();
		std::vector<GraphPassExecutionStats> const& getLastExecutionStats() const;
		std::vector<GraphPassHandle> const& getLastExecutionOrder() const;
		std::string const& getLastFailedPassName() const;
		GraphFramebufferCacheStats getFramebufferCacheStats() const;
		// Arms a synchronous readback of every stored pass output in the next
		// execution. Capturing at pass boundaries preserves transient images before
		// later graph work can reuse their backing textures.
		void requestImageCapture();
		std::vector<GraphImageCapture> takeImageCaptures();
		void execute(RenderGraph const& graph, RenderGraphTargets const& targets, Caps const& caps);
		void execute(RenderGraphTemplate const& graphTemplate, RenderGraphTargets const& targets, Caps const& caps);
	};
}
