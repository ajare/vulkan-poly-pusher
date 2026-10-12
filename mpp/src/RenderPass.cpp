#include <GL/glew.h>

#include "mpp/RenderPass.h"
#include "mpp/RenderSystem.h"
#include "mpp/Material.h"
#include "mpp/MppException.h"
#include "mpp/GLErrorCheck.h"

using namespace std;

namespace mpp
{

	RenderPass::RenderPass(RenderSystem* renderSystem, bool pbrForward, string const& debugName)
		: mRenderSystem(renderSystem)
		, mPbrForward(pbrForward)
	{
		RenderTextureOptions options;
		options.numAttachments = 1;
		options.depthAttachment = RenderTextureDepthAttachment::DepthRenderbuffer;
		if (mPbrForward)
		{
			options.colourType = TextureInternalType::Float;
			options.colourNormalised = false;
			options.colourBitSize = 16;
			options.params.minFilter = GL_LINEAR;
			options.params.magFilter = GL_LINEAR;
		}

		mTarget = renderSystem->createRenderTexture(
			debugName,
			renderSystem->getWindowWidth(),
			renderSystem->getWindowHeight(),
			options);
	}

	RenderPass::~RenderPass()
	{
	}

	RenderTargetPtr RenderPass::getRenderTarget()
	{
		return mTarget;
	}

	void RenderPass::bindRenderTarget()
	{
		mRenderSystem->setRenderTarget(mTarget);
	}

	bool RenderPass::resize(size_t width, size_t height)
	{
		return mTarget->resize(width, height);
	}

	PbrForwardMeshClassification classifyPbrForwardMesh(bool pbrShadingModel, bool transparent, bool doubleSided, bool modelCullBackFaces)
	{
		// Blend and depth sorting are PBR alpha semantics, so they stay gated on the
		// shading model; a BasicMaterial in this pass keeps its legacy behaviour.
		bool const blended = pbrShadingModel && transparent;

		// A double-sided material already reaches the shader, which flips the normal
		// for back faces, but nothing ever disabled culling for it -- so those faces
		// were discarded before the branch could run, and foliage, cloth and other
		// thin shells rendered with holes. This deliberately overrides the model's
		// flag rather than combining with it: the material describes the surface,
		// and a surface with no meaningful back face cannot be rasterized
		// single-sided whatever the model asked for.
		return { blended, blended, modelCullBackFaces && !(pbrShadingModel && doubleSided) };
	}

	void RenderPass::render(vector<SceneModel3dPtr> const& models, CameraPtr camera)
	{
		// Render models in view
		for (auto model: models)
		{
			auto instance = mRenderSystem->renderModelBatched(
				static_cast<Model const&>(*model->getModel().get()),
				model->getTransform(),
				camera);

			instance->setSourceSceneObject(model.get());
			instance->setParams(model->getParams());

			// Preserve the legacy Default blend behaviour for BasicMaterial. PBR
			// alpha semantics are supplied by the future PbrMaterial implementation
			// through Material's common render classification.
			for (auto meshInstance : instance->getMeshInstances())
			{
				auto material = static_cast<Material*>(meshInstance->getMaterial().get());
				if (!mPbrForward && material->getShadingModel() == Material::ShadingModel::Pbr)
				{
					THROW_MPP("PbrMaterial requires a PBR forward pipeline.", __LINE__, __FILE__, __func__);
				}
				if (!mPbrForward)
				{
					continue; // Preserve legacy BasicMaterial blend behaviour in Default.
				}
				// setParams above has already applied the model's own culling flag,
				// which is why it is fed in here and can then be overridden.
				auto const classification = classifyPbrForwardMesh(
					material->getShadingModel() == Material::ShadingModel::Pbr,
					material->isTransparent(),
					material->isDoubleSided(),
					meshInstance->cullBackFaces());
				meshInstance->blend(classification.blend);
				meshInstance->sortTransparent(classification.sortTransparent);
				meshInstance->cullBackFaces(classification.cullBackFaces);
			}
		}
	}
}