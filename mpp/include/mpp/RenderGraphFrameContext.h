#pragma once

#include <vector>

#include "mpp/Camera.h"
#include "mpp/Scene.h"
#include "mpp/SceneModel3d.h"
#include "mpp/RenderPass.h"

namespace mpp
{
	class RenderSystem;
	struct RenderPipelineOptions;

	// Live application state supplied for one graph execution. XML references
	// pass factories only; it never serializes these runtime objects.
	struct _MPPAPI RenderGraphFrameContext
	{
		RenderSystem* renderSystem{ nullptr };
		ScenePtr scene;
		CameraPtr camera;
		std::vector<SceneModel3dPtr> visibleModels;
		RenderPipelineOptions const* pipelineOptions{ nullptr };
		RenderPassPtr sceneRenderPass;
		// Whether this graph contains an enabled MPP.WaterScene pass. The opaque
		// scene pass defers water materials only when something is going to draw
		// them; in a graph without a water pass they shade in place, falling back
		// to the cubemap because no resolved scene colour is bound. That keeps a
		// water material from silently disappearing in an older pipeline.
		bool hasWaterPass{ false };
		// False is a failure fallback, not a technique change: WaterScene still
		// draws absorption/compositing while reflected radiance is suppressed.
		bool waterReflectionEnabled{ true };
	};
}
