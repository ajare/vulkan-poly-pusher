#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "mpp/Config.h"
#include "mpp/RenderGraph.h"
#include "mpp/RenderOutputProcessor.h"
#include "mpp/mesh/Primitive.h"

namespace mpp
{
	class SceneModel3d;

	enum class RenderFlowEventKind : uint8_t
	{
		PassBegin,
		PassEnd,
		BatchSubmission,
		GlState,
		MsaaResolve,
		Taa,
		SsaaHorizontal,
		SsaaVertical,
		Fxaa,
		Presentation
	};

	_MPPAPI char const* renderFlowEventKindName(RenderFlowEventKind kind);
	_MPPAPI std::string renderFlowPassRenderDocLabel(GraphPassHandle pass, std::string const& name,
	                                                GraphPassType type);
	_MPPAPI char const* renderFlowGeometryRenderDocLabel(bool transparent);
	_MPPAPI std::string renderFlowOutputRenderDocLabel(std::string const& outputName,
	                                                  RenderFlowEventKind kind);

	// Exact renderer submission descriptor populated immediately before the
	// corresponding mesh draw. Source identity is non-owning and generation-local.
	struct _MPPAPI RenderBatchSubmission
	{
		uint64_t sequence{ 0 };
		GraphPassHandle parentPass;
		SceneModel3d const* sceneObject{ nullptr };
		std::string meshName;
		std::string materialName;
		std::string programName;
		std::vector<std::string> textureNames;
		mesh::Primitive::Type primitiveType{ mesh::Primitive::Type::Triangles };
		uint32_t offset{ 0 };
		uint32_t count{ 0 };
		size_t instanceCount{ 0 };
		bool transparent{ false };
		bool blend{ false };
		bool cullBackFaces{ false };
		bool wireframe{ false };
	};

	struct _MPPAPI RenderFlowResourceDesc
	{
		std::string name;
		glm::uvec2 size{ 0 };
		GraphImageFormat format{ GraphImageFormat::Rgba8 };
		uint32_t samples{ 1 };
	};

	struct _MPPAPI RenderFlowEvent
	{
		RenderFlowEventKind kind{ RenderFlowEventKind::PassBegin };
		uint64_t sequence{ 0 };
		GraphPassHandle pass;
		GraphImageHandle image;
		std::string name;
		std::string outputName;
		std::string bypassReason;
		bool enabled{ true };
		bool depth{ false };
		std::vector<std::string> stateChanges;
		std::vector<RenderFlowResourceDesc> inputs;
		std::vector<RenderFlowResourceDesc> outputs;
	};

	// RenderPipeline publishes snapshots through shared_ptr<const ...>. The
	// renderer constructs a complete mutable candidate and publishes it only
	// after the frame succeeds, so consumers never observe partial execution.
	struct _MPPAPI RenderPipelineFlowSnapshot
	{
		uint64_t frameSerial{ 0 };
		uint64_t pipelineGeneration{ 0 };
		std::vector<GraphPassHandle> actualPassOrder;
		std::vector<RenderBatchSubmission> batches;
		// Contains the complete ordered event stream: pass boundaries, batch
		// submissions, resolves, output processing, and presentation.
		std::vector<RenderFlowEvent> physicalEvents;
		std::vector<RenderPipelineOutputPlan> outputPlans;
	};

	using RenderPipelineFlowSnapshotPtr = std::shared_ptr<RenderPipelineFlowSnapshot const>;
}
