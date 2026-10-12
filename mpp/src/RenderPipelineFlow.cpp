#include "mpp/RenderPipelineFlow.h"

#include <stdexcept>

namespace mpp
{
	namespace
	{
		char const* passTypeName(GraphPassType type)
		{
			switch (type)
			{
			case GraphPassType::Scene: return "Scene";
			case GraphPassType::Fullscreen: return "Fullscreen";
			case GraphPassType::Present: return "Present";
			}
			return "Unknown";
		}
	}

	char const* renderFlowEventKindName(RenderFlowEventKind kind)
	{
		switch (kind)
		{
		case RenderFlowEventKind::PassBegin: return "pass begin";
		case RenderFlowEventKind::PassEnd: return "pass end";
		case RenderFlowEventKind::BatchSubmission: return "batch submission";
		case RenderFlowEventKind::GlState: return "GL state";
		case RenderFlowEventKind::MsaaResolve: return "MSAA resolve";
		case RenderFlowEventKind::Taa: return "TAA";
		case RenderFlowEventKind::SsaaHorizontal: return "SSAA horizontal";
		case RenderFlowEventKind::SsaaVertical: return "SSAA vertical";
		case RenderFlowEventKind::Fxaa: return "FXAA";
		case RenderFlowEventKind::Presentation: return "presentation";
		}
		throw std::invalid_argument("Unknown render-flow event kind.");
	}

	std::string renderFlowPassRenderDocLabel(GraphPassHandle pass, std::string const& name, GraphPassType type)
	{
		return "RenderGraph Pass " + std::to_string(pass.id) + ": " + name + " [" + passTypeName(type) + "]";
	}

	char const* renderFlowGeometryRenderDocLabel(bool transparent)
	{
		return transparent ? "Draw: Transparent Geometry" : "Draw: Opaque + Masked Geometry";
	}

	std::string renderFlowOutputRenderDocLabel(std::string const& outputName, RenderFlowEventKind kind)
	{
		return "Output " + outputName + ": " + renderFlowEventKindName(kind);
	}
}
