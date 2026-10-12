#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#pragma warning(push)
#pragma warning(disable : 4201)
#include <glm/vec2.hpp>
#include <glm/vec4.hpp>
#pragma warning(pop)

#include "mpp/Config.h"
#include "mpp/TextureParams.h"
#include "mpp/UniformCollection.h"

namespace mpp
{
	struct Caps;

	enum class GraphImageFormat
	{
		R8,
		Rg8,
		Rgba8,
		Srgb8Alpha8,
		R16f,
		Rg16f,
		Rgba16f,
		R32f,
		Rg32f,
		Rgba32f,
		R11g11b10f,
		Rgb10a2,
		Depth16,
		Depth24,
		Depth32f,
		Depth24Stencil8,
		Depth32fStencil8
	};

	_MPPAPI char const* graphImageFormatName(GraphImageFormat format);

	enum class GraphImageShape { Texture2D, CubeMap };

	enum class GraphImageUsage : uint32_t
	{
		None = 0,
		Sampled = 1 << 0,
		ColourAttachment = 1 << 1,
		DepthAttachment = 1 << 2,
		Presentation = 1 << 3,
		// Keeps the final produced value live for host-side consumption even when
		// it is not a swapchain/presentation image.
		Exported = 1 << 4
	};

	inline GraphImageUsage operator |(GraphImageUsage left, GraphImageUsage right)
	{
		return (GraphImageUsage)((uint32_t)left | (uint32_t)right);
	}

	inline bool hasGraphImageUsage(GraphImageUsage value, GraphImageUsage test)
	{
		return ((uint32_t)value & (uint32_t)test) != 0;
	}

	struct _MPPAPI GraphImageDesc
	{
		GraphImageFormat format{ GraphImageFormat::Rgba8 };
		// Texture2D preserves the historical render-graph default.
		GraphImageShape shape{ GraphImageShape::Texture2D };
		glm::uvec2 absoluteSize{ 0 };
		glm::vec2 relativeSize{ 1.0f };
		uint32_t mipLevels{ 1 };
		GraphImageUsage usage{ GraphImageUsage::None };
		TextureParams params;
		TextureColourSpace colourSpace{ TextureColourSpace::Linear };
		// Enables sampler2DShadow/samplerCubeShadow comparison on depth images.
		bool depthCompare{ false };
		bool transient{ true };
		bool external{ false };
	};

	struct _MPPAPI GraphImageInfo
	{
		std::string name;
		GraphImageDesc desc;
		std::string importName;
	};

	struct _MPPAPI GraphImageHandle	{
		uint32_t id{ UINT32_MAX };
		uint32_t version{ 0 };

		bool isValid() const { return id != UINT32_MAX; }
	};

	struct _MPPAPI GraphPassHandle
	{
		uint32_t id{ UINT32_MAX };
		bool isValid() const { return id != UINT32_MAX; }
	};

	enum class GraphPassType { Scene, Fullscreen, Present };
	enum class GraphLoadOp { Load, Clear, DontCare };
	enum class GraphStoreOp { Store, DontCare };
	enum class GraphFillMode { Fill, Line };
	enum class GraphFrontFace { CounterClockwise, Clockwise };
	enum class GraphCullMode { None, Front, Back };
	enum class GraphCompareOp { Never, Less, Equal, LessEqual, Greater, NotEqual, GreaterEqual, Always };
	enum class GraphBlendOp { Add, Subtract, ReverseSubtract, Minimum, Maximum };
	enum class GraphBlendFactor { Zero, One, SourceColour, OneMinusSourceColour, DestinationColour, OneMinusDestinationColour, SourceAlpha, OneMinusSourceAlpha, DestinationAlpha, OneMinusDestinationAlpha };

	struct _MPPAPI GraphColourWriteMask
	{
		bool red{ true }, green{ true }, blue{ true }, alpha{ true };
		bool operator ==(GraphColourWriteMask const&) const = default;
	};

	struct _MPPAPI GraphRasterState
	{
		// false preserves the callback/built-in historical state contract.
		bool explicitState{ false };
		GraphFillMode fillMode{ GraphFillMode::Fill };
		GraphFrontFace frontFace{ GraphFrontFace::CounterClockwise };
		GraphCullMode cullMode{ GraphCullMode::Back };
		bool depthTest{ true };
		bool depthWrite{ true };
		GraphCompareOp depthCompare{ GraphCompareOp::Less };
		bool blend{ false };
		GraphBlendOp colourBlendOp{ GraphBlendOp::Add };
		GraphBlendOp alphaBlendOp{ GraphBlendOp::Add };
		GraphBlendFactor sourceColourBlend{ GraphBlendFactor::One };
		GraphBlendFactor destinationColourBlend{ GraphBlendFactor::Zero };
		GraphBlendFactor sourceAlphaBlend{ GraphBlendFactor::One };
		GraphBlendFactor destinationAlphaBlend{ GraphBlendFactor::Zero };
		std::vector<GraphColourWriteMask> colourWriteMasks;
		bool multisample{ true };
		bool alphaToCoverage{ false };
		bool scissor{ false };
		glm::uvec4 scissorRectangle{ 0 };
		// Serialization emits this block only when it differs from the default, so
		// documents that never touched raster state stay byte-identical.
		bool operator ==(GraphRasterState const&) const = default;
	};

	// UINT32_MAX means the ordinary 2D subresource; cubemap attachments select
	// exactly one of the six faces [0, 5].
	inline constexpr uint32_t GraphNoCubeFace = UINT32_MAX;

	struct _MPPAPI GraphColourOutput
	{
		GraphImageHandle image;
		uint32_t mipLevel{ 0 };
		GraphLoadOp load{ GraphLoadOp::DontCare };
		GraphStoreOp store{ GraphStoreOp::Store };
		glm::vec4 clearColour{ 0.0f };
		uint32_t cubeFace{ GraphNoCubeFace };
	};

	struct _MPPAPI GraphDepthOutput
	{
		GraphImageHandle image;
		uint32_t mipLevel{ 0 };
		GraphLoadOp load{ GraphLoadOp::DontCare };
		GraphStoreOp store{ GraphStoreOp::Store };
		float clearDepth{ 1.0f };
		uint32_t cubeFace{ GraphNoCubeFace };
	};

	struct _MPPAPI GraphSamplerBinding
	{
		std::string sampler;
		GraphImageHandle image;
		// UINT32_MAX exposes the declared chain; otherwise this mip is presented
		// as the sampler's base/max level for the duration of the pass.
		uint32_t mipLevel{ UINT32_MAX };
	};

	struct _MPPAPI GraphPassInfo
	{
		std::string name;
		bool enabled{ true };
		GraphPassType type{ GraphPassType::Scene };
		// XML stores this stable factory identifier, not executable code.
		std::string callbackFactory;
		// Resource name; resolution/validation occurs when a declarative
		// fullscreen pass implementation is attached.
		std::string programResource;
		std::vector<GraphImageHandle> sampledInputs;
		std::vector<GraphSamplerBinding> samplerBindings;
		UniformCollection parameters;
		std::vector<GraphColourOutput> colourOutputs;
		std::vector<GraphDepthOutput> depthOutputs;
		GraphRasterState rasterState;
	};

	enum class GraphCompileMessageSeverity { Information, Warning, Error };

	struct _MPPAPI GraphCompileMessage
	{
		GraphCompileMessageSeverity severity{ GraphCompileMessageSeverity::Information };
		std::string code;
		std::string message;
		GraphPassHandle pass;
		GraphImageHandle image;
	};

	struct _MPPAPI RenderGraphCompileResult
	{
		bool valid{ false };
		std::vector<GraphPassHandle> passOrder;
		std::vector<GraphPassHandle> culledPasses;
		std::vector<GraphImageHandle> unusedOutputs;
		// Legacy error-only text retained for execution exceptions and callers that
		// do not need structured compiler information.
		std::vector<std::string> diagnostics;
		std::vector<GraphCompileMessage> messages;
	};

	struct _MPPAPI RenderGraphPlanCacheStats
	{
		uint64_t compileHits{ 0 };
		uint64_t compileMisses{ 0 };
		uint64_t allocationHits{ 0 };
		uint64_t allocationMisses{ 0 };
		uint64_t invalidations{ 0 };
	};

	// One physical attachment requirement for a produced image version. The
	// first RG2 allocator may allocate one target per entry; later allocators
	// can alias entries with compatible, non-overlapping intervals.
	struct _MPPAPI GraphImageLifetime
	{
		GraphImageHandle image;
		std::string debugName;
		GraphImageDesc desc;
		glm::uvec2 size{ 0 };
		uint32_t firstPass{ UINT32_MAX };
		uint32_t lastPass{ UINT32_MAX };
		uint64_t estimatedBytes{ 0 };
		uint32_t physicalAllocation{ UINT32_MAX };
	};

	// Whether two graph images may share one physical allocation, ignoring their
	// lifetimes -- callers add the interval and transience rules on top.
	//
	// This must be the ONE definition. RenderGraph::buildAllocationPlan reports a
	// grouping that PipelineEditor displays, and RenderGraphTargets produces the
	// real one; when the two predicates disagree the editor describes an
	// allocation the renderer never makes. They previously drifted over six
	// sampler fields. RenderGraphTargets additionally reuses a pooled texture
	// across frames, which is the same question about the same texture object, so
	// the strict field set is the correct one for both.
	//
	// Sample count is deliberately absent: it is derived from usage and the
	// requested MSAA level rather than carried on the descriptor, so the allocator
	// compares it separately.
	_MPPAPI bool graphImagesCanAlias(GraphImageLifetime const& left, GraphImageLifetime const& right);

	// What a descriptor's size actually becomes at a given viewport, and how many
	// mip levels that size can carry. Shared so capability validation and the
	// allocation plan cannot disagree about whether a descriptor is legal.
	_MPPAPI glm::uvec2 resolveGraphImageSize(GraphImageDesc const& desc, glm::uvec2 const& viewport);
	_MPPAPI uint32_t maxGraphImageMipLevels(glm::uvec2 const& size);

	struct _MPPAPI RenderGraphAllocationPlan
	{
		bool valid{ false };
		std::vector<GraphImageLifetime> allocatedImages;
		std::vector<GraphImageHandle> importedImages;
		uint64_t estimatedPhysicalBytes{ 0 };
		std::vector<std::string> diagnostics;
	};

	// Immutable value artifact pairing one capability/viewport compilation with
	// the physical allocation plan produced from the same graph generation.
	struct _MPPAPI RenderGraphCompiledPlan
	{
		bool valid{ false };
		RenderGraphCompileResult compilation;
		RenderGraphAllocationPlan allocation;
	};

	// Declarative topology only. Allocation/execution is deliberately separate
	// so validation is usable without an OpenGL context.
	class _MPPAPI RenderGraph
	{
		struct Image;
		struct Pass;
		struct PlanCache;

		std::vector<Image> mImages;
		std::vector<Pass> mPasses;
		mutable std::unique_ptr<PlanCache> mPlanCache;
		mutable RenderGraphPlanCacheStats mPlanCacheStats;

		bool validImage(GraphImageHandle image) const;
		bool validPass(GraphPassHandle pass) const;
		void removeProducedValue(GraphImageHandle image);
		PlanCache& planCache() const;
		void invalidatePlanCache();

	public:
		RenderGraph();
		~RenderGraph();
		RenderGraph(RenderGraph&&) noexcept;
		RenderGraph& operator =(RenderGraph&&) noexcept;
		RenderGraph(RenderGraph const&);
		RenderGraph& operator =(RenderGraph const&);

		GraphImageHandle createImage(std::string const& name, GraphImageDesc const& desc);
		void setImageImportName(GraphImageHandle image, std::string const& importName);
		void clearImageImportName(GraphImageHandle image);
		void setImageName(GraphImageHandle image, std::string const& name);
		void setImageDesc(GraphImageHandle image, GraphImageDesc const& desc);
		// Structural editor operations remove dependent references rather than
		// manufacturing replacements. Metadata validation then reports required slots.
		void removeImage(GraphImageHandle image);
		GraphImageInfo getImageInfo(GraphImageHandle image) const;
		std::vector<GraphImageHandle> getImportedImages() const;
		GraphPassHandle addPass(std::string const& name, GraphPassType type = GraphPassType::Scene);
		void setPassEnabled(GraphPassHandle pass, bool enabled);
		void setPassName(GraphPassHandle pass, std::string const& name);
		void setPassProgramResource(GraphPassHandle pass, std::string const& resourceName);
		void clearPassProgramResource(GraphPassHandle pass);
		void setPassCallbackFactory(GraphPassHandle pass, std::string const& factoryName);
		void setPassRasterState(GraphPassHandle pass, GraphRasterState const& state);
		void removePass(GraphPassHandle pass);
		GraphPassHandle duplicatePass(GraphPassHandle pass, std::string const& name);
		void movePass(GraphPassHandle pass, uint32_t destination);

		void readSampled(GraphPassHandle pass, GraphImageHandle image);
		void bindSampler(GraphPassHandle pass, std::string const& sampler, GraphImageHandle image, uint32_t mipLevel = UINT32_MAX);
		void setPassParameters(GraphPassHandle pass, UniformCollection const& parameters);
		GraphImageHandle writeColour(GraphPassHandle pass, GraphImageHandle image, GraphLoadOp load = GraphLoadOp::DontCare, GraphStoreOp store = GraphStoreOp::Store, glm::vec4 const& clear = glm::vec4(0.0f), uint32_t mipLevel = 0, uint32_t cubeFace = GraphNoCubeFace);
		GraphImageHandle writeDepth(GraphPassHandle pass, GraphImageHandle image, GraphLoadOp load = GraphLoadOp::DontCare, GraphStoreOp store = GraphStoreOp::Store, float clear = 1.0f, uint32_t mipLevel = 0, uint32_t cubeFace = GraphNoCubeFace);
		void setColourOutput(GraphPassHandle pass, size_t output, GraphLoadOp load, GraphStoreOp store, glm::vec4 const& clear, uint32_t mipLevel, uint32_t cubeFace = GraphNoCubeFace);
		GraphImageHandle retargetColourOutput(GraphPassHandle pass, size_t output, GraphImageHandle image);
		void removeColourOutput(GraphPassHandle pass, size_t output);
		void setDepthOutput(GraphPassHandle pass, size_t output, GraphLoadOp load, GraphStoreOp store, float clear, uint32_t mipLevel, uint32_t cubeFace = GraphNoCubeFace);
		GraphImageHandle retargetDepthOutput(GraphPassHandle pass, size_t output, GraphImageHandle image);
		void removeDepthOutput(GraphPassHandle pass, size_t output);
		void setSamplerBinding(GraphPassHandle pass, size_t binding, std::string const& sampler, GraphImageHandle image, uint32_t mipLevel = UINT32_MAX);
		void removeSamplerBinding(GraphPassHandle pass, size_t binding);

		// Stable authored IDs identify imported and produced image values independently
		// of pass position. Unnamed values retain deterministic generated IDs.
		void setValueId(GraphImageHandle image, std::string const& valueId);
		std::string const& getValueId(GraphImageHandle image) const;
		GraphImageHandle findValue(std::string const& valueId) const;

		size_t getImageCount() const;
		size_t getImageVersionCount(uint32_t imageId) const;
		size_t getPassCount() const;
		GraphPassInfo getPassInfo(GraphPassHandle pass) const;

		RenderGraphCompileResult compile() const;
		// Device-limit validation. Images sized relative to the viewport cannot be
		// checked without one, so this overload skips them; prefer the overload
		// below anywhere a viewport is known -- the editor always knows its preview
		// size, and an unchecked relative image throws at allocation instead.
		RenderGraphCompileResult compile(Caps const& caps) const;
		RenderGraphCompileResult compile(Caps const& caps, glm::uvec2 const& viewport) const;
		// Dependency-derived stable order offered as an explicit editor action.
		RenderGraphCompileResult buildDependencyOrder() const;
		void reorderPasses(std::vector<GraphPassHandle> const& order);
		RenderGraphAllocationPlan buildAllocationPlan(glm::uvec2 const& viewport) const;
		RenderGraphCompiledPlan buildCompiledPlan(Caps const& caps, glm::uvec2 const& viewport) const;
		RenderGraphPlanCacheStats getPlanCacheStats() const;

		// Context-free diagnostic dump for logs and tests. It reports declared
		// images, their produced versions, and each pass's graph dependencies.
		std::string describe() const;
	};
}
