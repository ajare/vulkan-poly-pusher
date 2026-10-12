#pragma once

#include <vector>
#include <memory>
#include <string>

#include "mpp/Config.h"
#include "mpp/Resource.h"
#include "mpp/SceneModel3d.h"
#include "mpp/Camera.h"
#include "mpp/RenderTarget.h"
#include "mpp/Colour.h"

namespace mpp
{
	class RenderSystem;

	// How a mesh instance should be rasterized in the PBR forward pass. Kept as
	// plain values so the precedence rules can be tested without a GL context --
	// notably that a double-sided material overrides the model's culling flag.
	struct PbrForwardMeshClassification
	{
		bool blend;
		bool sortTransparent;
		bool cullBackFaces;

		bool operator ==(PbrForwardMeshClassification const&) const = default;
	};

	_MPPAPI PbrForwardMeshClassification classifyPbrForwardMesh(bool pbrShadingModel, bool transparent, bool doubleSided, bool modelCullBackFaces);

	class _MPPAPI RenderPass
	{
		RenderSystem* mRenderSystem;

		RenderTargetPtr mTarget;

		bool mPbrForward;

	public:

		explicit RenderPass(RenderSystem* renderSystem, bool pbrForward = false, std::string const& debugName = "SceneTarget");

		virtual ~RenderPass();

		RenderTargetPtr getRenderTarget();

		void bindRenderTarget();

		bool resize(size_t width, size_t height);

		virtual void render(std::vector<SceneModel3dPtr> const& models, CameraPtr camera);
	};

	typedef std::shared_ptr<RenderPass> RenderPassPtr;
}