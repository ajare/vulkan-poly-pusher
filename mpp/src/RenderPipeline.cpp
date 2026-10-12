#include <algorithm>
#include <atomic>
#include <functional>
#include <cmath>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <GL/glew.h>

#include "mpp/RenderPipeline.h"
#include "mpp/ResourceManager.h"
#include "mpp/RenderSystem.h"
#include "mpp/RenderGraphExecutor.h"
#include "mpp/RenderGraphBuiltInPasses.h"
#include "mpp/RenderGraphTemplate.h"
#include "mpp/RenderGraphImportRegistry.h"
#include "mpp/GLErrorCheck.h"
#include "mpp/GpuDebugScope.h"
#include "mpp/MppException.h"
#include "mpp/Material.h"
#include "mpp/ParticleData.h"
#include "mpp/Program.h"
#include "mpp/UniformBuffer.h"

using namespace std;

namespace mpp
{
	VirtualCameraTransforms buildObliquelyClippedVirtualCamera(
		glm::mat4 const& view, glm::mat4 const& projection,
		glm::vec4 const& worldClipPlane, float seamBias)
	{
		auto const normalLength = glm::length(glm::vec3(worldClipPlane));
		if (!(normalLength > 0.000001f) || !std::isfinite(normalLength) ||
			!std::isfinite(seamBias) || seamBias < 0.0f)
			THROW_MPP("An auxiliary world clip plane requires a finite non-zero normal and non-negative seam bias.", __LINE__, __FILE__, __func__);
		if (std::abs(glm::determinant(view)) < 0.000001f ||
			std::abs(glm::determinant(projection)) < 0.000001f)
			THROW_MPP("Auxiliary view and projection transforms must be invertible.", __LINE__, __FILE__, __func__);

		VirtualCameraTransforms result{ view, projection };
		// Normalize first: seamBias is always measured in world units. Adding it to
		// w expands the retained positive half-space into the rejected side.
		auto worldClip = worldClipPlane / normalLength;
		worldClip.w += seamBias;
		auto cameraClip = glm::transpose(glm::inverse(view)) * worldClip;
		auto nonZeroSign = [](float value) { return value < 0.0f ? -1.0f : 1.0f; };
		auto corner = glm::inverse(projection) * glm::vec4(
			nonZeroSign(cameraClip.x), nonZeroSign(cameraClip.y), 1.0f, 1.0f);
		float const denominator = glm::dot(cameraClip, corner);
		if (std::abs(denominator) < 0.000001f || !std::isfinite(denominator))
			THROW_MPP("An auxiliary world clip plane is degenerate for the virtual camera.", __LINE__, __FILE__, __func__);
		auto const scaledClip = cameraClip * (2.0f / denominator);
		// Replace projection row 2 with C - row 3 (OpenGL's -w near plane).
		for (int column = 0; column < 4; ++column)
			result.projection[column][2] = scaledClip[column] - result.projection[column][3];
		return result;
	}

	PlanarReflectionView buildPlanarReflectionView(
		Camera& camera, PlanarReflectionPlaneDescriptor const& plane,
		float aspectRatio)
	{
		if (aspectRatio <= 0.0f)
			THROW_MPP("A Planar reflection camera requires a positive aspect ratio.", __LINE__, __FILE__, __func__);

		auto mirror = [elevation = plane.elevation](glm::vec3 value, bool position)
		{
			value.y = position ? 2.0f * elevation - value.y : -value.y;
			return value;
		};

		PlanarReflectionView result;
		result.position = mirror(camera.getPosition(), true);
		result.direction = glm::normalize(mirror(camera.getDirection(), false));
		result.up = glm::normalize(mirror(camera.getUp(), false));
		result.view = glm::lookAt(result.position, result.position + result.direction, result.up);
		auto const baseProjection = glm::perspective(glm::radians(camera.getFov()), aspectRatio,
			camera.getNearClipDistance(), camera.getFarClipDistance());
		// Positive distance is the real viewer's side. The shared helper applies
		// the renderer-wide 0.05-world-unit seam expansion.
		auto const worldClip = plane.viewerSide == ReflectionPlaneSide::Above
			? glm::vec4(0.0f, 1.0f, 0.0f, -plane.elevation)
			: glm::vec4(0.0f, -1.0f, 0.0f, plane.elevation);
		result.projection = buildObliquelyClippedVirtualCamera(
			result.view, baseProjection, worldClip).projection;
		return result;
	}

	namespace
	{
		atomic<uint64_t> nextFlowGeneration{ 1 };

		char const* pipelineModeName(RenderPipelineMode mode)
		{
			switch (mode)
			{
			case RenderPipelineMode::LegacyForward: return "LegacyForward";
			case RenderPipelineMode::PbrForward: return "PbrForward";
			case RenderPipelineMode::GraphPbrForward: return "GraphPbrForward";
			case RenderPipelineMode::XmlGraphPbrForward: return "XmlGraphPbrForward";
			case RenderPipelineMode::GraphLegacyForward: return "GraphLegacyForward";
			default: return "Unknown";
			}
		}

		// The opaque scene pass has to know whether something later in the graph is
		// going to draw the water materials it would otherwise skip.
		bool graphHasWaterPass(RenderGraph const& graph)
		{
			for (uint32_t id = 0; id < graph.getPassCount(); ++id)
			{
				auto const info = graph.getPassInfo({ id });
				if (info.enabled && info.callbackFactory == "MPP.WaterScene") return true;
			}
			return false;
		}

		// GPU primitives update once per rendered frame outside the graph, so each
		// independent owner is scheduled only when its own pure draw pass is present.
		bool graphHasParticlePass(RenderGraph const& graph)
		{
			for (uint32_t id = 0; id < graph.getPassCount(); ++id)
			{
				auto const info = graph.getPassInfo({ id });
				if (info.enabled && (info.callbackFactory == "MPP.ParticleScene" ||
					info.callbackFactory == "MPP.ParticleWeightedOit" ||
					info.callbackFactory == "MPP.ParticleDistortion" ||
					info.callbackFactory == "MPP.ParticleVolumetricLighting" ||
					info.callbackFactory == "MPP.ParticleMeshScene")) return true;
			}
			return false;
		}

		bool graphHasTrailPass(RenderGraph const& graph)
		{
			for (uint32_t id = 0; id < graph.getPassCount(); ++id)
			{
				auto const info = graph.getPassInfo({ id });
				if (info.enabled && info.callbackFactory == "MPP.TrailScene") return true;
			}
			return false;
		}
	}


	RenderPipeline::RenderPipeline(string const& name, RenderSystem* renderSystem, RenderPipelineOptions const& options)
		: mName(name)
		, mRenderSystem(renderSystem)
		, mOptions(options)
	{
		mFlowGeneration = nextFlowGeneration.fetch_add(1, memory_order_relaxed);
		if (mOptions.waterReflections.planarPlanes.size() > WaterReflectionOptions::MaxPlanarPlanes)
			THROW_MPP("A generated Water pipeline accepts at most four planar reflection planes.", __LINE__, __FILE__, __func__);
		if(!mOptions.outputs.empty())mOutputProcessor=make_unique<RenderOutputProcessor>(renderSystem,mName);
		// The PBR preview path owns an HDR scene target. Legacy pipelines keep
		// their RGBA8 target and existing presentation behaviour.
		bool const pbr = mOptions.mode == RenderPipelineMode::PbrForward || mOptions.mode == RenderPipelineMode::GraphPbrForward || mOptions.mode == RenderPipelineMode::XmlGraphPbrForward;
		mPasses.push_back(make_shared<RenderPass>(renderSystem, pbr, mName + (pbr ? ".SceneHDR" : ".SceneLDR")));
	}

	RenderPipeline::~RenderPipeline()
	{
	}

	bool RenderPipeline::sceneProgramsSupportOutputs(vector<SceneModel3dPtr> const& models, size_t requiredCount)
	{
		vector<shared_ptr<Program>> programs;
		map<SceneModel3d const*, SceneModelProgramCache> currentModelCache;
		for (auto const& sceneModel : models)
		{
			if (!sceneModel) { programs.push_back({}); continue; }
			auto model = dynamic_cast<Model*>(sceneModel->getModel().get());
			auto params = sceneModel->getParams();
			uint64_t const modelRevision = model ? model->getMaterialRevision() : 0;
			uint64_t const parameterRevision = params->getProgramSetRevision();
			auto const cached = mSceneModelProgramCache.find(sceneModel.get());
			bool const cachedMaterialsCurrent = cached != mSceneModelProgramCache.end() && std::all_of(cached->second.materials.begin(), cached->second.materials.end(), [](auto const& material)
			{
				return material.first && material.first->getLifecycleRevision() == material.second;
			});
			if (cached != mSceneModelProgramCache.end() && cached->second.model == model &&
				cached->second.modelMaterialRevision == modelRevision && cached->second.parameterRevision == parameterRevision && cachedMaterialsCurrent)
			{
				++mOutputValidationModelHits;
				programs.insert(programs.end(), cached->second.programs.begin(), cached->second.programs.end());
				currentModelCache.emplace(sceneModel.get(), cached->second);
				continue;
			}

			++mOutputValidationModelMisses;
			SceneModelProgramCache modelCache{ model, modelRevision, parameterRevision, {}, {} };
			if (!model)
			{
				modelCache.programs.push_back({});
			}
			else
			{
				auto const& meshParams = params->getMeshParams();
				auto const defaultParams = meshParams.find("");
				for (int meshIndex = 0; meshIndex < model->getNumMeshes(); ++meshIndex)
				{
					auto mesh = model->getMesh(meshIndex);
					auto const specificParams = meshParams.find(mesh->getName());
					auto const* renderParams = specificParams != meshParams.end() ? &specificParams->second :
						(defaultParams != meshParams.end() ? &defaultParams->second : nullptr);
					if (renderParams && (renderParams->flags & ModelRenderParams::Flag_Visible) == 0) continue;
					auto materialResource = renderParams && renderParams->material ? renderParams->material : mesh->getMaterial();
					if (materialResource) modelCache.materials.push_back({ materialResource, materialResource->getLifecycleRevision() });
					auto material = dynamic_cast<Material*>(materialResource.get());
					modelCache.programs.push_back(material ? dynamic_pointer_cast<Program>(material->getProgram()) : shared_ptr<Program>{});
				}
			}
			std::sort(modelCache.materials.begin(), modelCache.materials.end(), [](auto const& left, auto const& right)
			{
				return std::less<Resource const*>{}(left.first.get(), right.first.get());
			});
			modelCache.materials.erase(std::unique(modelCache.materials.begin(), modelCache.materials.end(), [](auto const& left, auto const& right)
			{
				return left.first.get() == right.first.get();
			}), modelCache.materials.end());
			std::sort(modelCache.programs.begin(), modelCache.programs.end(), [](auto const& left, auto const& right)
			{
				return std::less<Program const*>{}(left.get(), right.get());
			});
			modelCache.programs.erase(std::unique(modelCache.programs.begin(), modelCache.programs.end(), [](auto const& left, auto const& right)
			{
				return left.get() == right.get();
			}), modelCache.programs.end());
			programs.insert(programs.end(), modelCache.programs.begin(), modelCache.programs.end());
			currentModelCache.emplace(sceneModel.get(), std::move(modelCache));
		}
		mSceneModelProgramCache.swap(currentModelCache);
		std::sort(programs.begin(), programs.end(), [](auto const& left, auto const& right)
		{
			return std::less<Program const*>{}(left.get(), right.get());
		});
		programs.erase(std::unique(programs.begin(), programs.end(), [](auto const& left, auto const& right)
		{
			return left.get() == right.get();
		}), programs.end());

		vector<ProgramOutputKey> key;
		key.reserve(programs.size());
		for (auto const& program : programs) key.push_back({ program, program ? program->getFragmentOutputRevision() : 0 });
		if (mOutputValidationKnown && requiredCount == mOutputValidationRequiredCount && key == mOutputValidationPrograms)
		{
			++mOutputValidationHits;
			return mOutputValidationResult;
		}

		++mOutputValidationMisses;
		bool supported = true;
		for (auto const& program : programs)
		{
			string diagnostic;
			if (!program || !program->validateFragmentOutputLocations(requiredCount, diagnostic)) { supported = false; break; }
		}
		mOutputValidationPrograms = std::move(key);
		mOutputValidationRequiredCount = requiredCount;
		mOutputValidationResult = supported;
		mOutputValidationKnown = true;
		return supported;
	}

	string const& RenderPipeline::getName() const
	{
		return mName;
	}

	RenderPipelineOptions const& RenderPipeline::getOptions() const
	{
		return mOptions;
	}

	void RenderPipeline::setExposure(float exposure)
	{
		mOptions.exposure = std::max(exposure, 0.0f);
	}

	void RenderPipeline::setToneMapOperator(PbrToneMapOperator toneMapOperator)
	{
		mOptions.toneMapOperator = toneMapOperator;
	}

	void RenderPipeline::setBloomOptions(BloomOptions const& bloomOptions)
	{
		mOptions.bloom = bloomOptions;
		if (!mOptions.bloom.enabled || !mOptions.bloom.useMrtEmissiveMask)
		{
			mSceneModelProgramCache.clear();
			mOutputValidationPrograms.clear();
			mOutputValidationKnown = false;
		}
	}

	void RenderPipeline::setAmbientOcclusionOptions(AmbientOcclusionOptions const& ambientOcclusionOptions)
	{
		mOptions.ambientOcclusion = ambientOcclusionOptions;
	}

	void RenderPipeline::setPostEffectEnabled(string const& passName, bool enabled)
	{
		auto& parameters = mPostEffectParameterOverrides[passName];
		parameters.setUniform("ENABLED", enabled ? 1 : 0);
		if (mGraphExecutor) mGraphExecutor->setPassParameterOverrides(passName, parameters);
	}

	void RenderPipeline::setPostEffectParameter(string const& passName, string const& parameterName, float value)
	{
		auto& parameters = mPostEffectParameterOverrides[passName];
		parameters.setUniform(parameterName, value);
		if (mGraphExecutor) mGraphExecutor->setPassParameterOverrides(passName, parameters);
	}

	void RenderPipeline::setGraphPassDebugOptions(GraphPassDebugOptions const& graphPasses)
	{
		mOptions.graphPasses = graphPasses;
	}

	void RenderPipeline::setDebugEnvironmentCube(bool enabled)
	{
		mOptions.debugEnvironmentCube = enabled;
	}

	void RenderPipeline::ensureBloomTargets(size_t width, size_t height)
	{
		if (!mOptions.bloom.enabled)
		{
			mBloomExtractTarget.reset();
			mBloomPingTarget.reset();
			mBloomPongTarget.reset();
			mBloomCompositeTarget.reset();
			return;
		}

		auto needsCreate = [&](RenderTargetPtr const& target)
		{
			return !target || target->getWidth() != width || target->getHeight() != height;
		};
		if (!needsCreate(mBloomExtractTarget))
		{
			return;
		}

		RenderTextureOptions options;
		options.numAttachments = 1;
		options.colourType = TextureInternalType::Float;
		options.colourNormalised = false;
		options.colourBitSize = 16;
		options.params.minFilter = GL_LINEAR;
		options.params.magFilter = GL_LINEAR;
		options.params.wrap = GL_CLAMP_TO_EDGE;
		mBloomExtractTarget = mRenderSystem->createRenderTexture(mName + ".BloomExtract", width, height, options);
		mBloomPingTarget = mRenderSystem->createRenderTexture(mName + ".BloomPing", width, height, options);
		mBloomPongTarget = mRenderSystem->createRenderTexture(mName + ".BloomPong", width, height, options);
		mBloomCompositeTarget = mRenderSystem->createRenderTexture(mName + ".BloomComposite", width, height, options);
	}

	void RenderPipeline::setPbrEnvironment(PbrEnvironmentPtr environment)
	{
		mOptions.environment = std::move(environment);
		// Re-arm the incomplete-environment warning. Callers routinely mutate the
		// PbrEnvironment in place and set the same shared_ptr back, so comparing
		// pointers would never re-arm it; the setter being called at all is the
		// signal that the environment may have changed. This is edit-driven rather
		// than per-frame, so re-arming cannot reintroduce per-frame log spam.
		mWarnedMissingPbrEnvironment = false;
	}

	void RenderPipeline::setShadowDomain(string const& shadowDomain)
	{
		mOptions.shadowDomain = shadowDomain;
	}

	RenderTargetPtr RenderPipeline::getOutputRenderTarget()
	{
		if (mOptions.bloom.enabled && mBloomCompositeTarget)
		{
			return mBloomCompositeTarget;
		}
		return mPasses.back()->getRenderTarget();
	}

	RenderTargetPtr RenderPipeline::getOutputRenderTarget(string const& outputName) const
	{
		auto const declared = find_if(mOptions.outputs.begin(), mOptions.outputs.end(), [&](auto const& output)
		{
			return output.name == outputName;
		});
		if (declared == mOptions.outputs.end())
			THROW_MPP("Render pipeline '" + mName + "' has no declared output named '" + outputName + "'.", __LINE__, __FILE__, __func__);
		auto const target = mNamedOutputTargets.find(outputName);
		if (target == mNamedOutputTargets.end() || !target->second)
			THROW_MPP("Render pipeline output '" + outputName + "' is unavailable before successful graph execution.", __LINE__, __FILE__, __func__);
		return target->second;
	}

	RenderTargetPtr RenderPipeline::getGraphImageRenderTarget(GraphImageHandle image) const
	{
		return mGraphTargets?mGraphTargets->get(image):nullptr;
	}

	AuxiliarySceneOutputs RenderPipeline::renderAuxiliaryScene(
		ScenePtr scene, CameraPtr hostCamera, AuxiliarySceneView const& view)
	{
		if (!scene || !hostCamera)
			THROW_MPP("An auxiliary scene requires a scene and host camera.", __LINE__, __FILE__, __func__);
		if (view.slot.empty())
			THROW_MPP("An auxiliary scene slot must not be empty.", __LINE__, __FILE__, __func__);

		auto& slot = mAuxiliarySlots[view.slot];
		auto const targetName = mName + ".Auxiliary." + view.slot;
		slot.diagnostics = {
			targetName,
			targetName + ".ColourHDR",
			targetName + ".Depth",
			view.width,
			view.height,
			false,
			{}
		};
		slot.completed = false;

		auto fail = [&](string const& reason)
		{
			slot.diagnostics.failureReason = reason.empty() ? "unknown auxiliary render failure" : reason;
			mRenderSystem->warnMessage("Auxiliary pass '" + slot.diagnostics.passName +
				"' failed (colour='" + slot.diagnostics.colourOutputName + "', depth='" +
				slot.diagnostics.depthOutputName + "', " + to_string(view.width) + "x" +
				to_string(view.height) + "): " + slot.diagnostics.failureReason);
		};

		// Everything a nested scene draw can change is captured before target
		// allocation or visibility evaluation. Restoration deliberately uses the
		// raw UBO bytes so pass-scoped light/camera data is exact, not reconstructed.
		auto const savedTarget = mRenderSystem->mRenderTarget;
		auto const savedViewportX = mRenderSystem->mViewportX;
		auto const savedViewportY = mRenderSystem->mViewportY;
		auto const savedViewportWidth = mRenderSystem->mViewportWidth;
		auto const savedViewportHeight = mRenderSystem->mViewportHeight;
		auto const savedRaster = mRenderSystem->captureRasterState(1);
		auto const savedCameraMatrix = mRenderSystem->m3dCameraMatrix;
		auto const savedProjectionMatrix = mRenderSystem->m3dProjectionMatrix;
		auto const savedModelMatrix = mRenderSystem->m3dModelMatrix;
		auto const savedMcpMatrix = mRenderSystem->m3dModelCameraProjectionMatrix;
		auto const savedProjectionType = mRenderSystem->mProjectionType;
		auto const savedPbrLights = mRenderSystem->mPbrLights;
		auto const savedEnvironment = mRenderSystem->mActivePbrEnvironment;
		auto const savedSamplerOverrides = mRenderSystem->mActivePipelineSamplerOverrides;
		auto const savedUniformOverrides = mRenderSystem->mActivePipelineUniformOverrides;
		auto const savedShadowTarget = mRenderSystem->mActiveShadowDepthTarget;
		auto const savedExpectedOutputs = mRenderSystem->mExpectedGraphColourOutputs;
		auto const savedCameraFrameView = mRenderSystem->mCameraFrameView;
		auto const savedCameraFrameProjection = mRenderSystem->mCameraFrameProjection;
		auto const savedCameraFrameData = mRenderSystem->mCameraFrameBuffer
			? mRenderSystem->mCameraFrameBuffer->getBufferData() : vector<int8_t>{};
		auto const savedLegacyLightData = mRenderSystem->mLightsBuffer
			? mRenderSystem->mLightsBuffer->getBufferData() : vector<int8_t>{};
		auto const savedPbrLightData = mRenderSystem->mPbrLightsBuffer
			? mRenderSystem->mPbrLightsBuffer->getBufferData() : vector<int8_t>{};
		bool restored = false;
		auto restore = [&]
		{
			if (restored) return;
			// A draw failure may make flushing fail again. State restoration must not
			// stop there: discard that secondary exception and restore every scoped
			// renderer value before propagating the original failure.
			try { mRenderSystem->flushVertexBuffers(); } catch (...) {}
			mRenderSystem->mActivePbrEnvironment = savedEnvironment;
			mRenderSystem->mActivePipelineSamplerOverrides = savedSamplerOverrides;
			mRenderSystem->mActivePipelineUniformOverrides = savedUniformOverrides;
			mRenderSystem->mActiveShadowDepthTarget = savedShadowTarget;
			mRenderSystem->mExpectedGraphColourOutputs = savedExpectedOutputs;
			mRenderSystem->mPbrLights = savedPbrLights;
			mRenderSystem->mCameraFrameView = savedCameraFrameView;
			mRenderSystem->mCameraFrameProjection = savedCameraFrameProjection;
			if (mRenderSystem->mCameraFrameBuffer && !savedCameraFrameData.empty())
			{
				mRenderSystem->mCameraFrameBuffer->getBufferData() = savedCameraFrameData;
				mRenderSystem->mCameraFrameBuffer->mapBufferData();
			}
			if (mRenderSystem->mLightsBuffer && !savedLegacyLightData.empty())
			{
				mRenderSystem->mLightsBuffer->getBufferData() = savedLegacyLightData;
				mRenderSystem->mLightsBuffer->mapBufferData();
			}
			if (mRenderSystem->mPbrLightsBuffer && !savedPbrLightData.empty())
			{
				mRenderSystem->mPbrLightsBuffer->getBufferData() = savedPbrLightData;
				mRenderSystem->mPbrLightsBuffer->mapBufferData();
			}
			mRenderSystem->m3dCameraMatrix = savedCameraMatrix;
			mRenderSystem->m3dProjectionMatrix = savedProjectionMatrix;
			mRenderSystem->m3dModelMatrix = savedModelMatrix;
			mRenderSystem->m3dModelCameraProjectionMatrix = savedMcpMatrix;
			mRenderSystem->mProjectionType = savedProjectionType;
			if (savedTarget && mRenderSystem->mRenderTarget != savedTarget)
				mRenderSystem->setRenderTarget(savedTarget);
			mRenderSystem->setViewport(savedViewportX, savedViewportY,
				savedViewportWidth, savedViewportHeight);
			mRenderSystem->applyRasterState(savedRaster, 1,
				savedViewportWidth, savedViewportHeight);
			restored = true;
		};
		struct RestoreGuard
		{
			function<void()> restore;
			~RestoreGuard() { try { restore(); } catch (...) {} }
		} restoreGuard{ restore };

		try
		{
			if (view.width == 0 || view.height == 0)
				THROW_MPP("Auxiliary output dimensions must be non-zero.", __LINE__, __FILE__, __func__);
			if (view.width > (uint32_t)mRenderSystem->getCaps().maxTextureSize ||
				view.height > (uint32_t)mRenderSystem->getCaps().maxTextureSize)
				THROW_MPP("Auxiliary output dimensions exceed the GPU maximum texture size.", __LINE__, __FILE__, __func__);
			auto const clipped = buildObliquelyClippedVirtualCamera(
				view.view, view.projection, view.worldClipPlane, view.seamBias);
			auto virtualCamera = make_shared<VirtualCamera>(clipped.view, clipped.projection,
				view.nearDistance, view.farDistance);

			if (!slot.target || slot.target->getWidth() != view.width || slot.target->getHeight() != view.height)
			{
				RenderTextureOptions options;
				options.numAttachments = 1;
				options.colourType = TextureInternalType::Float;
				options.colourNormalised = false;
				options.colourBitSize = 16;
				options.depthAttachment = RenderTextureDepthAttachment::DepthTexture;
				options.params.minFilter = GL_LINEAR;
				options.params.magFilter = GL_LINEAR;
				options.params.wrap = GL_CLAMP_TO_EDGE;
				slot.target = mRenderSystem->createRenderTexture(
					targetName, view.width, view.height, options);
			}

			mRenderSystem->setRenderTarget(slot.target);
			mRenderSystem->setViewport(0, 0, view.width, view.height);
			mRenderSystem->m3dCameraMatrix = clipped.view;
			mRenderSystem->m3dProjectionMatrix = clipped.projection;
			mRenderSystem->resetTransform();
			mRenderSystem->setCameraFrame(clipped.view, clipped.projection,
				glm::vec2((float)view.width, (float)view.height), view.nearDistance,
				view.farDistance, mRenderSystem->getElapsedSeconds());
			if (scene->ownsPbrLights()) mRenderSystem->setPbrLights(scene->getPbrLights());

			if (view.uniformOverrides.getUniformData().contains("MPP_VIRTUAL_CAMERA"))
				THROW_MPP("MPP_VIRTUAL_CAMERA is reserved by auxiliary scene execution.", __LINE__, __FILE__, __func__);
			UniformCollection uniforms = view.uniformOverrides;
			uniforms.setUniform("MPP_VIRTUAL_CAMERA", int32_t{ 1 });
			mRenderSystem->setActivePipelineUniformOverrides(uniforms);
			map<string, ResourcePtr> samplers;
			bool const pbr = mOptions.mode == RenderPipelineMode::PbrForward ||
				mOptions.mode == RenderPipelineMode::GraphPbrForward ||
				mOptions.mode == RenderPipelineMode::XmlGraphPbrForward;
			if (pbr)
			{
				mRenderSystem->setActivePbrEnvironment(mOptions.environment);
				auto const cubeFallback = mRenderSystem->getResourceManager()->getResource("__mpp_tex_pbr_ibl_cube__");
				auto const brdfFallback = mRenderSystem->getResourceManager()->getResource("__mpp_tex_pbr_brdf_lut__");
				samplers["PBR_IRRADIANCE_MAP"] = mOptions.environment && mOptions.environment->irradianceMap ? mOptions.environment->irradianceMap : cubeFallback;
				samplers["PBR_PREFILTERED_SPECULAR_MAP"] = mOptions.environment && mOptions.environment->prefilteredSpecularMap ? mOptions.environment->prefilteredSpecularMap : cubeFallback;
				samplers["PBR_BRDF_LUT"] = mOptions.environment && mOptions.environment->brdfIntegrationLut ? mOptions.environment->brdfIntegrationLut : brdfFallback;
			}
			for (auto const& [name, resource] : view.samplerOverrides)
				samplers[name] = resource;
			mRenderSystem->setActivePipelineSamplerOverrides(samplers);
			mRenderSystem->setActiveShadowDomain(mOptions.shadowDomain);
			mRenderSystem->setExpectedGraphColourOutputs(1);

			GraphRasterState raster;
			raster.explicitState = true;
			raster.frontFace = view.reverseWinding ? GraphFrontFace::Clockwise : GraphFrontFace::CounterClockwise;
			raster.cullMode = GraphCullMode::None;
			raster.depthTest = true;
			raster.depthWrite = true;
			raster.depthCompare = GraphCompareOp::Less;
			raster.blend = false;
			raster.multisample = false;
			// clearScreen temporarily enables scissoring. Even though drawing is
			// unscissored, its clear rectangle must cover the auxiliary target;
			// the default empty rectangle leaves depth uninitialized (often zero).
			raster.scissorRectangle = { 0, 0, view.width, view.height };
			mRenderSystem->applyRasterState(raster, 1, view.width, view.height);
			mRenderSystem->clearScreen(scene->getClearColour());

			// Visibility is deliberately evaluated after every pass-scoped state change:
			// a throwing application Scene exercises the same restoration path as a draw.
			auto const models = scene->get3dModelsInView(virtualCamera);
			if (scene->show3dModels() && !models.empty())
			{
				if (mOptions.depthPrepass)
					mRenderSystem->renderDepthPrepass(models, virtualCamera, 1);
				mPasses.back()->render(models, virtualCamera);
				mRenderSystem->flushVertexBuffers();
			}

			restore();
			slot.completed = true;
			slot.diagnostics.succeeded = true;
			return { slot.target, slot.target,
				slot.diagnostics.colourOutputName, slot.diagnostics.depthOutputName };
		}
		catch (std::exception const& error)
		{
			fail(error.what());
			try { restore(); } catch (...) {}
			throw;
		}
		catch (...)
		{
			fail("unknown auxiliary render failure");
			try { restore(); } catch (...) {}
			throw;
		}
	}

	AuxiliarySceneOutputs RenderPipeline::getAuxiliarySceneOutputs(string const& slotName) const
	{
		auto const found = mAuxiliarySlots.find(slotName);
		if (found == mAuxiliarySlots.end() || !found->second.completed || !found->second.target)
			THROW_MPP("Auxiliary scene slot '" + slotName + "' has no successfully completed outputs.", __LINE__, __FILE__, __func__);
		return { found->second.target, found->second.target,
			found->second.diagnostics.colourOutputName, found->second.diagnostics.depthOutputName };
	}

	AuxiliarySceneDiagnostics const& RenderPipeline::getAuxiliarySceneDiagnostics(string const& slotName) const
	{
		auto const found = mAuxiliarySlots.find(slotName);
		if (found == mAuxiliarySlots.end())
			THROW_MPP("Render pipeline '" + mName + "' has no auxiliary scene slot named '" + slotName + "'.", __LINE__, __FILE__, __func__);
		return found->second.diagnostics;
	}

	void RenderPipeline::requestGraphImageCapture()
	{
		mGraphImageCaptureRequested = true;
	}

	vector<GraphImageCapture> RenderPipeline::takeGraphImageCaptures()
	{
		return mGraphExecutor ? mGraphExecutor->takeImageCaptures() : vector<GraphImageCapture>{};
	}

	vector<GraphPassExecutionStats> const& RenderPipeline::getLastGraphExecutionStats() const
	{
		static vector<GraphPassExecutionStats> const empty;return mGraphExecutor?mGraphExecutor->getLastExecutionStats():empty;
	}

	vector<GraphPassHandle> const& RenderPipeline::getLastGraphExecutionOrder() const
	{
		static vector<GraphPassHandle> const empty;return mGraphExecutor?mGraphExecutor->getLastExecutionOrder():empty;
	}

	bool RenderPipeline::planarReflectionRuntimeFailed() const
	{
		return mPlanarReflectionRuntimeFailed;
	}

	string const& RenderPipeline::getPlanarReflectionFailureMessage() const
	{
		return mPlanarReflectionFailureMessage;
	}

	void RenderPipeline::disablePlanarReflectionsAfterFailure(string const& reason)
	{
		if (mPlanarReflectionRuntimeFailed) return;
		mPlanarReflectionRuntimeFailed = true;
		mPlanarReflectionFailureMessage = reason.empty() ? "unknown Planar reflection failure" : reason;
		mRenderSystem->warnMessage("Planar Water reflection disabled for pipeline '" + mName +
			"' without selecting Screen-space: " + mPlanarReflectionFailureMessage);
	}

	void RenderPipeline::setFlowTelemetryEnabled(bool enabled)
	{
		if(mFlowTelemetryEnabled==enabled)return;discardFlowSnapshot();mFlowTelemetryEnabled=enabled;mLastFlowSnapshot.reset();
	}

	bool RenderPipeline::isFlowTelemetryEnabled() const{return mFlowTelemetryEnabled;}
	RenderPipelineFlowSnapshotPtr RenderPipeline::getLastFlowSnapshot() const{return mLastFlowSnapshot;}

	void RenderPipeline::beginFlowSnapshot() noexcept
	{
		if(!mFlowTelemetryEnabled||!mGraphExecutor)return;try{mPendingFlowSnapshot=make_shared<RenderPipelineFlowSnapshot>();mPendingFlowSnapshot->frameSerial=mRenderSystem->getFrameSerial();mPendingFlowSnapshot->pipelineGeneration=mFlowGeneration;mRenderSystem->beginRenderFlowCapture(mPendingFlowSnapshot.get());}catch(...){mPendingFlowSnapshot.reset();}
	}

	void RenderPipeline::publishFlowSnapshot() noexcept
	{
		if(!mPendingFlowSnapshot)return;bool complete=mRenderSystem->endRenderFlowCapture();if(complete)try{mPendingFlowSnapshot->actualPassOrder=mGraphExecutor->getLastExecutionOrder();if(mOutputProcessor)mPendingFlowSnapshot->outputPlans=mOutputProcessor->getPlans();mLastFlowSnapshot=mPendingFlowSnapshot;}catch(...){}mPendingFlowSnapshot.reset();
	}

	void RenderPipeline::discardFlowSnapshot() noexcept
	{
		if(mPendingFlowSnapshot)mRenderSystem->endRenderFlowCapture();mPendingFlowSnapshot.reset();
	}

	uint64_t RenderPipeline::getOutputGeneration() const{return mOutputProcessor?mOutputProcessor->getGeneration():0;}
	vector<RenderPipelineOutputPlan> const& RenderPipeline::getOutputPlans() const{static vector<RenderPipelineOutputPlan> const empty;return mOutputProcessor?mOutputProcessor->getPlans():empty;}
	SceneOutputValidationCacheStats RenderPipeline::getSceneOutputValidationCacheStats() const
	{
		return { mOutputValidationHits, mOutputValidationMisses, mOutputValidationModelHits, mOutputValidationModelMisses, mOutputValidationPrograms.size() };
	}
	void RenderPipeline::prepareOutputs(RenderGraph const& graph,map<string,RenderTargetPtr> const& destinations){if(mOutputProcessor)mOutputProcessor->rebuild(mOptions.outputs,graph,destinations,mRenderSystem->getOptions().antiAliasing);}

	void RenderPipeline::resize(size_t width, size_t height)
	{
		for (auto const& pass : mPasses)
		{
			pass->resize(width, height);
		}
	}

	void RenderPipeline::addRenderPass(RenderPassPtr pass)
	{
		mPasses.push_back(pass);
	}

	void RenderPipeline::renderGraphForward(ScenePtr scene, CameraPtr camera, vector<SceneModel3dPtr> const& models, bool pbr)
	{
		GpuDebugScope graphScope("Pipeline " + mName + ": RenderGraph");
		bool const captureRequestedThisFrame = mGraphImageCaptureRequested;
		auto outputAntiAliasing=mOptions.outputs.empty()?AntiAliasingDefaults{}:resolveAntiAliasing(mRenderSystem->getOptions().antiAliasing,mOptions.outputs.front().antiAliasing);uint32_t physicalSamples=antiAliasingSampleCount(outputAntiAliasing.msaa);std::optional<TaaFrameContext> taaFrame;struct JitterReset{Camera* camera{};~JitterReset(){if(camera)camera->setProjectionJitter({0,0});}}jitterReset;
		if(outputAntiAliasing.taa){auto const& viewport=scene->getViewport();auto rasterWidth=ssaaDimension((uint32_t)viewport.width,outputAntiAliasing.ssaa),rasterHeight=ssaaDimension((uint32_t)viewport.height,outputAntiAliasing.ssaa);auto jitter=taaHaltonJitter(mTaaSequenceIndex++);camera->setProjectionJitter({2.0f*jitter.x/(float)rasterWidth,2.0f*jitter.y/(float)rasterHeight});jitterReset.camera=camera.get();auto direction=camera->getDirection();auto frameSerial=mRenderSystem->getFrameSerial();bool discontinuity=!mTaaCameraValid||camera->getCutRevision()!=mLastCameraCutRevision||(mLastTaaFrameSerial&&frameSerial!=mLastTaaFrameSerial+1)||glm::distance(camera->getPosition(),mLastCameraPosition)>std::max(1.0f,std::min(10.0f,camera->getFarClipDistance()*0.01f))||glm::dot(direction,mLastCameraDirection)<0.8f||std::abs(camera->getFov()-mLastCameraFov)>10.0f||std::abs(camera->getAspectRatio()-mLastCameraAspect)>0.001f||std::abs(camera->getNearClipDistance()-mLastCameraNear)>0.001f||std::abs(camera->getFarClipDistance()-mLastCameraFar)>0.01f;auto viewProjection=camera->getProjectionTransform()*camera->getViewTransform();taaFrame=TaaFrameContext{viewProjection,glm::inverse(viewProjection),frameSerial,discontinuity};mTaaCameraValid=true;mLastTaaFrameSerial=frameSerial;mLastCameraCutRevision=camera->getCutRevision();mLastCameraPosition=camera->getPosition();mLastCameraDirection=direction;mLastCameraFov=camera->getFov();mLastCameraAspect=camera->getAspectRatio();mLastCameraNear=camera->getNearClipDistance();mLastCameraFar=camera->getFarClipDistance();}
		if (!mGraphTargets)
		{
			mGraphTargets = make_unique<RenderGraphTargets>(mRenderSystem);
			mGraphExecutor = make_unique<RenderGraphExecutor>(mRenderSystem);
			registerBuiltInRenderGraphPasses(mGraphPassFactories);
			mGraphExecutor->setPassFactoryRegistry(&mGraphPassFactories);
		}
		if (mGraphImageCaptureRequested)
		{
			mGraphExecutor->requestImageCapture();
			mGraphImageCaptureRequested = false;
		}

		if (mOptions.graphTemplate)
		{
			ResourcePtr selectedTemplate = mOptions.graphTemplate;
			bool const useXmlMrt = pbr && mOptions.graphTemplateMrt && mOptions.bloom.enabled && mOptions.bloom.useMrtEmissiveMask &&
				mRenderSystem->getCaps().maxDrawBuffers >= 2 && mRenderSystem->getCaps().maxColourAttachments >= 2 &&
				sceneProgramsSupportOutputs(models, 2);
			if (useXmlMrt) selectedTemplate = mOptions.graphTemplateMrt;
			auto templateResource = dynamic_cast<RenderGraphTemplate*>(selectedTemplate.get());
			if (!templateResource) THROW_MPP("XmlGraphPbrForward requires a RenderGraph resource.", __LINE__, __FILE__, __func__);
			templateResource->create();
			templateResource->load();
			auto const& graph = templateResource->getGraph();
			if (!graph) THROW_MPP("XmlGraphPbrForward graph template is empty.", __LINE__, __FILE__, __func__);
			auto const& viewport = scene->getViewport();
			mGraphTargets->allocatePhysical(graph->buildAllocationPlan(glm::uvec2(ssaaDimension((uint32_t)viewport.width,outputAntiAliasing.ssaa),ssaaDimension((uint32_t)viewport.height,outputAntiAliasing.ssaa))),physicalSamples);
			RenderGraphImportRegistry imports;
			for(auto const& entry:mOptions.graphImports)imports.registerImport(entry.first,entry.second);
			if(!imports.findImport("screen"))imports.registerImport("screen", mRenderSystem->getScreenRenderTarget());
			if (!mOptions.shadowDomain.empty()&&!imports.findImport("shadowDepth")) imports.registerImport("shadowDepth", mRenderSystem->getShadowDomainDepthTarget(mOptions.shadowDomain));
			mGraphTargets->bindImports(*graph, imports);
			struct PreparedOutput{string name;GraphImageHandle image;GraphImageHandle depth;RenderTargetPtr destination;RenderTargetPtr source;bool external;};vector<PreparedOutput> preparedOutputs;
			if(mOutputProcessor)
			{
				map<string,RenderTargetPtr> destinations;
				for(auto const& output:mOptions.outputs)
				{
					GraphImageHandle handle;GraphImageInfo info;for(uint32_t id=0;id<graph->getImageCount();++id){auto candidate=graph->getImageInfo({id,0});if(candidate.name==output.image){handle={id,0};info=candidate;break;}}if(!handle.isValid())THROW_MPP("Named output '"+output.name+"' references an unknown graph image.",__LINE__,__FILE__,__func__);for(uint32_t pass=0;pass<graph->getPassCount();++pass){auto const& passInfo=graph->getPassInfo({pass});for(auto const& attachment:passInfo.colourOutputs)if(attachment.image.id==handle.id&&attachment.image.version>handle.version)handle=attachment.image;}
					GraphImageHandle depthHandle;if(!output.taaDepth.empty()){for(uint32_t id=0;id<graph->getImageCount();++id)if(graph->getImageInfo({id,0}).name==output.taaDepth){depthHandle={id,0};break;}for(uint32_t pass=0;pass<graph->getPassCount();++pass)for(auto const& attachment:graph->getPassInfo({pass}).depthOutputs)if(attachment.image.id==depthHandle.id&&attachment.image.version>depthHandle.version)depthHandle=attachment.image;}auto destination=info.desc.external?imports.findImport(info.importName):mGraphTargets->get(handle);if(!destination)THROW_MPP("Named output '"+output.name+"' has no render target.",__LINE__,__FILE__,__func__);destinations.emplace(output.name,destination);preparedOutputs.push_back({output.name,handle,depthHandle,destination,{},info.desc.external});
				}
				mOutputProcessor->rebuild(mOptions.outputs,*graph,destinations,mRenderSystem->getOptions().antiAliasing);
				map<uint32_t,RenderTargetPtr> externalSources;for(auto& output:preparedOutputs)if(output.external){auto [found,inserted]=externalSources.emplace(output.image.id,mOutputProcessor->getInput(output.name));output.source=found->second;if(inserted)mGraphTargets->bindImported(output.image,output.source);}
			}
			// XML supplies defaults; current pipeline controls override dynamic
			// per-frame values without recompiling or mutating the template.
			for (uint32_t id = 0; id < graph->getPassCount(); ++id)
			{
				GraphPassHandle pass{ id };
				auto const info = graph->getPassInfo(pass);
				UniformCollection overrides;
				if (info.name == "BloomExtract")
				{
					overrides.setUniform("THRESHOLD", mOptions.bloom.threshold);
					mGraphExecutor->setPassParameterOverrides(info.name, overrides);
				}
				else if (info.name == "BloomComposite")
				{
					overrides.setUniform("INTENSITY", mOptions.bloom.intensity);
					mGraphExecutor->setPassParameterOverrides(info.name, overrides);
				}
				else if (info.name == "ToneMapPresentation")
				{
					overrides.setUniform("EXPOSURE", mOptions.exposure);
					overrides.setUniform("TONE_MAP_OPERATOR", (int32_t)(mOptions.toneMapOperator == PbrToneMapOperator::Aces ? 1 : 0));
					mGraphExecutor->setPassParameterOverrides(info.name, overrides);
				}
			}
			RenderGraphFrameContext frameContext{ mRenderSystem, scene, camera, models, &mOptions, mPasses.back(), graphHasWaterPass(*graph), mOptions.waterReflections.enabled };
			mGraphExecutor->setFrameContext(&frameContext);
			if (mOptions.graphPasses.particles && graphHasParticlePass(*graph)) mRenderSystem->simulateParticles();
			if (mOptions.graphPasses.particles && graphHasTrailPass(*graph)) mRenderSystem->simulateTrails();
			beginFlowSnapshot();
			try
			{
				mGraphExecutor->execute(*templateResource, *mGraphTargets, mRenderSystem->getCaps());
				mGraphExecutor->setFrameContext(nullptr);
				for(auto const& output:preparedOutputs){auto depth=output.depth.isValid()?mGraphTargets->get(output.depth):output.destination;mOutputProcessor->present(output.name,output.destination,output.external?output.source:mGraphTargets->get(output.image),depth,taaFrame?&*taaFrame:nullptr);}
				map<string,RenderTargetPtr> completedOutputs;for(auto const& output:preparedOutputs)completedOutputs.emplace(output.name,output.destination);mNamedOutputTargets=std::move(completedOutputs);
				publishFlowSnapshot();
			}
			catch(...){mGraphExecutor->setFrameContext(nullptr);discardFlowSnapshot();throw;}
			return;
		}

		RenderGraph graph;
		auto makeColour = [](GraphImageFormat format, bool external = false)
		{
			GraphImageDesc desc;
			desc.format = format;
			desc.usage = GraphImageUsage::ColourAttachment | (external ? GraphImageUsage::Presentation : GraphImageUsage::Sampled);
			desc.external = external;
			desc.transient = !external;
			desc.params.minFilter = GL_LINEAR;
			desc.params.magFilter = GL_LINEAR;
			desc.params.wrap = GL_CLAMP_TO_EDGE;
			return desc;
		};
		size_t const sceneExtraOutputCount = mOptions.sceneExtraOutputs.size();
		bool const useMrtNormals = mOptions.ambientOcclusion.method == AmbientOcclusionMethod::Gtao &&
			mOptions.graphPasses.ambientOcclusion && mOptions.ambientOcclusion.gtao.normalSource == GTAONormalSource::Mrt;
		// Client-declared extras are appended immediately after whichever
		// built-in MRT slots are in play, so a hard-requirement path (GTAO MRT
		// normals; extras with no fallback of their own) must budget for them
		// too. Bloom's MRT emissive mask keeps its existing graceful fallback
		// and is deliberately not extended here -- it silently reverts to a
		// threshold extract rather than throwing.
		size_t const mrtNormalsRequiredCount = 3 + sceneExtraOutputCount;
		if (useMrtNormals && (mRenderSystem->getCaps().maxDrawBuffers < mrtNormalsRequiredCount || mRenderSystem->getCaps().maxColourAttachments < mrtNormalsRequiredCount))
		{
			THROW_MPP("GTAO MRT normal sourcing" + (sceneExtraOutputCount ? " plus " + to_string(sceneExtraOutputCount) + " scene extra output(s)" : string()) +
				" requires at least " + to_string(mrtNormalsRequiredCount) + " colour attachments and " + to_string(mrtNormalsRequiredCount) +
				" draw buffers (location 0 scene colour, location 1 bloom mask, location 2 RG16F normals, remaining locations scene extra outputs).", __LINE__, __FILE__, __func__);
		}
		if (useMrtNormals && !sceneProgramsSupportOutputs(models, mrtNormalsRequiredCount))
		{
			THROW_MPP("GTAO MRT normal sourcing" + (sceneExtraOutputCount ? " plus scene extra outputs" : string()) +
				" requires every visible scene material program to declare active fragment outputs at locations 0.." + to_string(mrtNormalsRequiredCount - 1) +
				"; update the incompatible material shader or select normalSource=depth.", __LINE__, __FILE__, __func__);
		}
		bool const useMrtEmissiveMask = pbr && mOptions.bloom.enabled && mOptions.bloom.useMrtEmissiveMask &&
			mRenderSystem->getCaps().maxDrawBuffers >= 2 && mRenderSystem->getCaps().maxColourAttachments >= 2 &&
			(useMrtNormals || sceneProgramsSupportOutputs(models, 2));
		auto sceneHdr = graph.createImage(pbr ? "SceneHdr" : "SceneLdr", makeColour(pbr ? GraphImageFormat::Rgba16f : GraphImageFormat::Rgba8));
		GraphImageHandle bloomMask;
		if (useMrtEmissiveMask || useMrtNormals)
		{
			bloomMask = graph.createImage(useMrtEmissiveMask ? "BloomMaskHdr" : "SceneMrtReserved1", makeColour(GraphImageFormat::Rgba16f));
		}
		GraphImageHandle sceneNormals;
		if (useMrtNormals) sceneNormals = graph.createImage("SceneShadingNormals", makeColour(GraphImageFormat::Rg16f));
		// Extras with no MRT normals in play still need their own fail-fast
		// budget/program-support check (the normals branch above already
		// covers the combined case). Bloom's mask carries no such hard
		// requirement, so it is excluded from the required-location count.
		if (sceneExtraOutputCount && !useMrtNormals)
		{
			size_t const requiredCount = 1 + (bloomMask.isValid() ? 1 : 0) + sceneExtraOutputCount;
			if (mRenderSystem->getCaps().maxDrawBuffers < requiredCount || mRenderSystem->getCaps().maxColourAttachments < requiredCount ||
				!sceneProgramsSupportOutputs(models, requiredCount))
			{
				THROW_MPP("Scene extra outputs require at least " + to_string(requiredCount) + " colour attachments and " + to_string(requiredCount) +
					" draw buffers, and every visible scene material program to declare active fragment outputs through location " + to_string(requiredCount - 1) + ".",
					__LINE__, __FILE__, __func__);
			}
		}
		std::vector<GraphImageHandle> sceneExtraOutputImages;
		sceneExtraOutputImages.reserve(sceneExtraOutputCount);
		for (auto const& extra : mOptions.sceneExtraOutputs)
			sceneExtraOutputImages.push_back(graph.createImage("SceneExtra." + extra.name, makeColour(extra.format)));
		GraphImageDesc sceneDepthDesc;
		sceneDepthDesc.format = GraphImageFormat::Depth24;
		bool const reflectionEnabled = mOptions.waterReflections.enabled && !mPlanarReflectionRuntimeFailed;
		bool const screenSpaceWater = mOptions.generatedWater && reflectionEnabled &&
			mOptions.waterReflections.technique == WaterReflectionTechnique::ScreenSpace;
		bool const planarRequested = mOptions.generatedWater &&
			mOptions.waterReflections.technique == WaterReflectionTechnique::Planar &&
			!mOptions.waterReflections.planarPlanes.empty();
		bool const planarWater = planarRequested && reflectionEnabled;
		bool const reflectionFailureWater = planarRequested && !reflectionEnabled;
		bool const sampleSceneDepth = screenSpaceWater || planarWater || reflectionFailureWater || outputAntiAliasing.taa ||
			(mOptions.generatedParticles && mOptions.graphPasses.particles) ||
			(mOptions.ambientOcclusion.method != AmbientOcclusionMethod::None && mOptions.graphPasses.ambientOcclusion);
		sceneDepthDesc.usage = GraphImageUsage::DepthAttachment | (sampleSceneDepth ? GraphImageUsage::Sampled : GraphImageUsage::None);
		auto sceneDepth = graph.createImage("SceneDepth", sceneDepthDesc);

		GraphImageHandle shadowDepth;
		GraphImageHandle shadowDepthOutput;
		RenderTargetPtr shadowTarget;
		std::vector<GraphPassHandle> shadowPasses;
		std::vector<SceneModel3dPtr> shadowModels = models;
		bool renderShadowPasses = false;
		if (!mOptions.shadowDomain.empty() && mOptions.graphPasses.shadow)
		{
			// Disabled and hardware-fallback domains deliberately have no depth
			// target. Keep their disabled ShadowFrame active, but do not declare a
			// graph import that can only be bound to null.
			shadowTarget = mRenderSystem->getShadowDomainDepthTarget(mOptions.shadowDomain);
			if (shadowTarget)
			{
				auto const& configuredShadow = mRenderSystem->getShadowDomainOptions(mOptions.shadowDomain);
				if (configuredShadow.light.type == ShadowLightType::Point)
					shadowModels = scene->get3dModelsInSphere(configuredShadow.light.position, configuredShadow.light.range);
				renderShadowPasses = mRenderSystem->prepareShadowDomain(mOptions.shadowDomain, shadowModels);
				GraphImageDesc shadowDesc;
				shadowDesc.format = GraphImageFormat::Depth24;
				shadowDesc.usage = GraphImageUsage::DepthAttachment | GraphImageUsage::Sampled;
				shadowDesc.depthCompare = true;
				shadowDesc.external = true;
				shadowDesc.transient = false;
				auto const& shadowOptions = mRenderSystem->getShadowDomainOptions(mOptions.shadowDomain);
				shadowDesc.absoluteSize = glm::uvec2((uint32_t)shadowOptions.resolution);
				bool const pointShadow = shadowOptions.light.type == ShadowLightType::Point;
				shadowDesc.shape = pointShadow ? GraphImageShape::CubeMap : GraphImageShape::Texture2D;
				shadowDepth = graph.createImage(pointShadow ? "PointShadowDepthCube" : "ShadowDepth", shadowDesc);
				shadowDepthOutput = shadowDepth;
				if (pointShadow && renderShadowPasses)
				{
					static constexpr char const* faces[] = { "+X", "-X", "+Y", "-Y", "+Z", "-Z" };
					shadowDepthOutput = shadowDepth;
					for (uint32_t face = 0; face < 6; ++face)
					{
						auto pass = graph.addPass("PointShadowFace " + std::string(faces[face]), GraphPassType::Scene);
						shadowPasses.push_back(pass);
						// A cubemap-face write only replaces that subresource. Loading the
						// preceding logical version after the first face keeps the version
						// chain live, so graph culling cannot discard faces +X through +Z
						// when the scene samples only the completed sixth version.
						shadowDepthOutput = graph.writeDepth(pass, shadowDepthOutput,
							face == 0 ? GraphLoadOp::Clear : GraphLoadOp::Load,
							GraphStoreOp::Store, 1.0f, 0, face);
					}
				}
				else if (!pointShadow && renderShadowPasses)
				{
					auto pass = graph.addPass("ShadowDepth", GraphPassType::Scene);
					shadowPasses.push_back(pass);
					shadowDepthOutput = graph.writeDepth(pass, shadowDepth, GraphLoadOp::Clear, GraphStoreOp::Store);
				}
			}
		}

		std::vector<GraphImageHandle> planarReflections;
		if (planarWater)
		{
			auto const resolutionDivisor = mOptions.waterReflections.planarResolution == PlanarReflectionResolution::Full ? 1u :
				(mOptions.waterReflections.planarResolution == PlanarReflectionResolution::Half ? 2u : 4u);
			auto const& planarViewport = scene->getViewport();
			auto const worldTargetSize = glm::uvec2(
				ssaaDimension((uint32_t)planarViewport.width, outputAntiAliasing.ssaa),
				ssaaDimension((uint32_t)planarViewport.height, outputAntiAliasing.ssaa));
			auto const planarSize = glm::uvec2(
				std::max(1u, (worldTargetSize.x + resolutionDivisor - 1) / resolutionDivisor),
				std::max(1u, (worldTargetSize.y + resolutionDivisor - 1) / resolutionDivisor));
			for (size_t index = 0; index < mOptions.waterReflections.planarPlanes.size(); ++index)
			{
				auto const suffix = std::to_string(index);
				auto reflectionDesc = makeColour(pbr ? GraphImageFormat::Rgba16f : GraphImageFormat::Rgba8);
				reflectionDesc.absoluteSize = planarSize;
				reflectionDesc.relativeSize = glm::vec2(0.0f);
				reflectionDesc.mipLevels = 1;
				reflectionDesc.params.minFilter = GL_LINEAR;
				reflectionDesc.params.magFilter = GL_LINEAR;
				reflectionDesc.params.wrap = GL_CLAMP_TO_EDGE;
				auto reflection = graph.createImage("PlanarReflection" + suffix, reflectionDesc);
				GraphImageDesc reflectionDepthDesc;
				reflectionDepthDesc.format = GraphImageFormat::Depth24;
				reflectionDepthDesc.usage = GraphImageUsage::DepthAttachment;
				reflectionDepthDesc.absoluteSize = planarSize;
				reflectionDepthDesc.relativeSize = glm::vec2(0.0f);
				auto reflectionDepth = graph.createImage("PlanarReflectionDepth" + suffix, reflectionDepthDesc);
				auto reflectionPass = graph.addPass("PlanarReflection" + suffix, GraphPassType::Scene);
				graph.setPassCallbackFactory(reflectionPass, "MPP.PlanarReflectionScene");
				if (shadowDepthOutput.isValid()) graph.readSampled(reflectionPass, shadowDepthOutput);
				UniformCollection reflectionParameters;
				reflectionParameters.setUniform("ELEVATION", mOptions.waterReflections.planarPlanes[index].elevation);
				reflectionParameters.setUniform("VIEWER_SIDE", int32_t{ mOptions.waterReflections.planarPlanes[index].viewerSide == ReflectionPlaneSide::Above ? 0 : 1 });
				graph.setPassParameters(reflectionPass, reflectionParameters);
				reflection = graph.writeColour(reflectionPass, reflection, GraphLoadOp::Clear, GraphStoreOp::Store, glm::vec4(0.0f));
				reflectionDepth = graph.writeDepth(reflectionPass, reflectionDepth, GraphLoadOp::Clear, GraphStoreOp::DontCare);
				GraphRasterState reflectionRaster;
				reflectionRaster.explicitState = true;
				reflectionRaster.frontFace = GraphFrontFace::Clockwise;
				// Individual scene meshes enable back-face culling as required; starting
				// disabled is what preserves deliberately two-sided materials/models.
				reflectionRaster.cullMode = GraphCullMode::None;
				reflectionRaster.depthTest = true;
				reflectionRaster.depthWrite = true;
				reflectionRaster.depthCompare = GraphCompareOp::Less;
				reflectionRaster.blend = false;
				reflectionRaster.multisample = false;
				graph.setPassRasterState(reflectionPass, reflectionRaster);
				planarReflections.push_back(reflection);
			}
		}

		auto scenePass = graph.addPass(pbr ? "PbrScene" : "LegacyScene", GraphPassType::Scene);
		if (mOptions.generatedWater) graph.setPassCallbackFactory(scenePass, pbr ? "MPP.PbrScene" : "MPP.LegacyScene");
		if (shadowDepthOutput.isValid()) graph.readSampled(scenePass, shadowDepthOutput);
		sceneHdr = graph.writeColour(scenePass, sceneHdr, GraphLoadOp::Clear, GraphStoreOp::Store,
			glm::vec4(scene->getClearColour().red, scene->getClearColour().green, scene->getClearColour().blue, scene->getClearColour().alpha));
		if (bloomMask.isValid()) bloomMask = graph.writeColour(scenePass, bloomMask, GraphLoadOp::Clear, useMrtEmissiveMask ? GraphStoreOp::Store : GraphStoreOp::DontCare);
		if (useMrtNormals) sceneNormals = graph.writeColour(scenePass, sceneNormals, GraphLoadOp::Clear, GraphStoreOp::Store);
		for (auto& extraImage : sceneExtraOutputImages) extraImage = graph.writeColour(scenePass, extraImage, GraphLoadOp::Clear, GraphStoreOp::Store);
		sceneDepth=graph.writeDepth(scenePass,sceneDepth,GraphLoadOp::Clear,sampleSceneDepth?GraphStoreOp::Store:GraphStoreOp::DontCare);

		GraphImageHandle presentationTexture = sceneHdr;
		enum class SsaoGraphStep { Raw, Blur, Composite };
		vector<GraphPassHandle> ssaoPasses;
		vector<SsaoGraphStep> ssaoSteps;
		vector<GraphImageHandle> ssaoInputs;
		GraphImageHandle ssaoModulationImage;
		if (mOptions.ambientOcclusion.method != AmbientOcclusionMethod::None && mOptions.graphPasses.ambientOcclusion)
		{
			// Keep ambient occlusion's fixed placement, with a shared depth-aware
			// denoise and composite sequence after the selected raw method.
			auto rawOcclusion = graph.createImage("AmbientOcclusionRaw", makeColour(GraphImageFormat::Rgba8));
			auto rawPass = graph.addPass(mOptions.ambientOcclusion.method == AmbientOcclusionMethod::Ssao ? "SSAO" : "GTAO", GraphPassType::Fullscreen);
			graph.readSampled(rawPass, sceneDepth);
			if (useMrtNormals) graph.readSampled(rawPass, sceneNormals);
			rawOcclusion = graph.writeColour(rawPass, rawOcclusion);
			ssaoPasses.push_back(rawPass);
			ssaoSteps.push_back(SsaoGraphStep::Raw);
			ssaoInputs.push_back({});

			auto blurredOcclusion = graph.createImage("AmbientOcclusionBlur", makeColour(GraphImageFormat::Rgba8));
			auto blurPass = graph.addPass("AmbientOcclusionBlur", GraphPassType::Fullscreen);
			graph.readSampled(blurPass, rawOcclusion);
			graph.readSampled(blurPass, sceneDepth);
			blurredOcclusion = graph.writeColour(blurPass, blurredOcclusion);
			ssaoPasses.push_back(blurPass);
			ssaoSteps.push_back(SsaoGraphStep::Blur);
			ssaoInputs.push_back(rawOcclusion);

			if (!mOptions.ambientOcclusion.modulationInput.empty())
			{
				// Resolve the latest version of the named image, mirroring the
				// named-output resolution below: find the image by name, then
				// walk passes added so far for a higher-versioned colour write.
				for (uint32_t id = 0; id < graph.getImageCount(); ++id)
				{
					auto candidate = graph.getImageInfo({ id, 0 });
					if (candidate.name == mOptions.ambientOcclusion.modulationInput) { ssaoModulationImage = { id, 0 }; break; }
				}
				if (!ssaoModulationImage.isValid())
					THROW_MPP("Ambient occlusion modulationInput '" + mOptions.ambientOcclusion.modulationInput + "' does not name a known graph image.", __LINE__, __FILE__, __func__);
				for (uint32_t pass = 0; pass < graph.getPassCount(); ++pass)
				{
					auto const& passInfo = graph.getPassInfo({ pass });
					for (auto const& attachment : passInfo.colourOutputs)
						if (attachment.image.id == ssaoModulationImage.id && attachment.image.version > ssaoModulationImage.version)
							ssaoModulationImage = attachment.image;
				}
			}
			auto ssaoComposite = graph.createImage("AmbientOcclusionComposite", makeColour(pbr ? GraphImageFormat::Rgba16f : GraphImageFormat::Rgba8));
			auto compositePass = graph.addPass("AmbientOcclusionComposite", GraphPassType::Fullscreen);
			graph.readSampled(compositePass, sceneHdr);
			graph.readSampled(compositePass, blurredOcclusion);
			if (ssaoModulationImage.isValid()) graph.readSampled(compositePass, ssaoModulationImage);
			presentationTexture = graph.writeColour(compositePass, ssaoComposite);
			ssaoPasses.push_back(compositePass);
			ssaoSteps.push_back(SsaoGraphStep::Composite);
			ssaoInputs.push_back(blurredOcclusion);
		}
		GraphImageHandle shadedSceneTexture = presentationTexture;
		// Reflection-source topology starts at this seam. Screen-space owns the
		// frozen, mipmapped scene copy; Planar deliberately owns none of that
		// topology and will add its per-plane sources independently.
		if (screenSpaceWater || planarWater || reflectionFailureWater)
		{
			GraphImageHandle waterBase = shadedSceneTexture;
			if (screenSpaceWater)
			{
				GraphImageDesc resolvedDesc = makeColour(pbr ? GraphImageFormat::Rgba16f : GraphImageFormat::Rgba8);
				auto const& waterViewport = scene->getViewport();
				auto const resolvedSize = glm::uvec2(
					ssaaDimension((uint32_t)waterViewport.width, outputAntiAliasing.ssaa),
					ssaaDimension((uint32_t)waterViewport.height, outputAntiAliasing.ssaa));
				resolvedDesc.mipLevels = std::min(5u, maxGraphImageMipLevels(resolvedSize));
				resolvedDesc.params.minFilter = resolvedDesc.mipLevels > 1 ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR;
				auto resolvedScene = graph.createImage("SceneColourResolved", resolvedDesc);
				auto copyPass = graph.addPass("SceneColourCopy", GraphPassType::Fullscreen);
				graph.setPassCallbackFactory(copyPass, "MPP.SceneColourCopy");
				graph.bindSampler(copyPass, "TEX1", shadedSceneTexture);
				waterBase = graph.writeColour(copyPass, resolvedScene, GraphLoadOp::DontCare, GraphStoreOp::Store);
			}

			auto waterComposite = graph.createImage("WaterComposite", makeColour(pbr ? GraphImageFormat::Rgba16f : GraphImageFormat::Rgba8));
			auto waterPass = graph.addPass("WaterScene", GraphPassType::Scene);
			graph.setPassCallbackFactory(waterPass, "MPP.WaterScene");
			// Input zero is also the image copied into WaterComposite before the
			// interface draw. Planar has no frozen Screen-space copy, so it uses the
			// already shaded opaque scene directly and binds its reflected source(s)
			// under distinct names for the generated Water material to consume.
			graph.bindSampler(waterPass, "PBR_SCENE_COLOUR_RESOLVED", waterBase);
			graph.bindSampler(waterPass, "PBR_SCENE_DEPTH", sceneDepth);
			// The Water shader declares the bounded sampler set once. Bind inactive
			// slots to the existing scene source so zero through four dynamic Planar
			// branches can share that program without allocating placeholder images.
			for (size_t index = 0; index < WaterReflectionOptions::MaxPlanarPlanes; ++index)
				graph.bindSampler(waterPass, "PBR_PLANAR_REFLECTION_" + std::to_string(index),
					index < planarReflections.size() ? planarReflections[index] : waterBase);
			if (shadowDepthOutput.isValid()) graph.bindSampler(waterPass, "SHADOW_MAP", shadowDepthOutput);
			shadedSceneTexture = graph.writeColour(waterPass, waterComposite, GraphLoadOp::DontCare, GraphStoreOp::Store);
			GraphRasterState waterRaster;
			waterRaster.explicitState = true;
			waterRaster.depthTest = false;
			waterRaster.depthWrite = false;
			waterRaster.cullMode = GraphCullMode::None;
			graph.setPassRasterState(waterPass, waterRaster);
			presentationTexture = shadedSceneTexture;
		}

		if (mOptions.generatedParticles && mOptions.graphPasses.particles)
		{
			// Real meshes are depth-tested material geometry and therefore run in a
			// separate pass before transparent billboard/ribbon composition.
			auto meshParticlePass = graph.addPass("ParticleMeshes", GraphPassType::Scene);
			graph.setPassCallbackFactory(meshParticlePass, "MPP.ParticleMeshScene");
			if (shadowDepthOutput.isValid()) graph.bindSampler(meshParticlePass, "SHADOW_MAP", shadowDepthOutput);
			presentationTexture = graph.writeColour(meshParticlePass, presentationTexture, GraphLoadOp::Load, GraphStoreOp::Store);
			if (useMrtEmissiveMask)
				bloomMask = graph.writeColour(meshParticlePass, bloomMask, GraphLoadOp::Load, GraphStoreOp::Store);
			sceneDepth = graph.writeDepth(meshParticlePass, sceneDepth, GraphLoadOp::Load, GraphStoreOp::Store);
			GraphRasterState meshParticleRaster;
			meshParticleRaster.explicitState = true;
			meshParticleRaster.depthTest = true;
			meshParticleRaster.depthWrite = true;
			meshParticleRaster.cullMode = GraphCullMode::Back;
			meshParticleRaster.blend = false;
			graph.setPassRasterState(meshParticlePass, meshParticleRaster);

			// Emitter-level proxy spheres add depth-aware inscattering to the HDR scene
			// and emissive mask. Draw count is bounded by live emitters, never particles.
			auto volumetricLightingPass = graph.addPass("ParticleVolumetricLighting", GraphPassType::Scene);
			graph.setPassCallbackFactory(volumetricLightingPass, "MPP.ParticleVolumetricLighting");
			graph.bindSampler(volumetricLightingPass, "DEPTH", sceneDepth);
			presentationTexture = graph.writeColour(volumetricLightingPass, presentationTexture, GraphLoadOp::Load, GraphStoreOp::Store);
			if (useMrtEmissiveMask)
				bloomMask = graph.writeColour(volumetricLightingPass, bloomMask, GraphLoadOp::Load, GraphStoreOp::Store);
			GraphRasterState volumetricLightingRaster;
			volumetricLightingRaster.explicitState = true;
			volumetricLightingRaster.depthTest = false;
			volumetricLightingRaster.depthWrite = false;
			volumetricLightingRaster.cullMode = GraphCullMode::None;
			volumetricLightingRaster.blend = true;
			volumetricLightingRaster.sourceColourBlend = GraphBlendFactor::One;
			volumetricLightingRaster.destinationColourBlend = GraphBlendFactor::One;
			volumetricLightingRaster.sourceAlphaBlend = GraphBlendFactor::One;
			volumetricLightingRaster.destinationAlphaBlend = GraphBlendFactor::One;
			volumetricLightingRaster.multisample = false;
			graph.setPassRasterState(volumetricLightingPass, volumetricLightingRaster);

			// Weighted OIT is an authored accumulation pass plus an authored resolve.
			// Optical depth is additive (-log(revealage)), so both accumulation targets
			// share one ordinary GraphRasterState and remain order-independent.
			auto oitAccumulation = graph.createImage("ParticleOitAccumulation", makeColour(GraphImageFormat::Rgba16f));
			auto oitOpticalDepth = graph.createImage("ParticleOitOpticalDepth", makeColour(GraphImageFormat::R16f));
			auto oitPass = graph.addPass("ParticleWeightedOit", GraphPassType::Scene);
			graph.setPassCallbackFactory(oitPass, "MPP.ParticleWeightedOit");
			graph.bindSampler(oitPass, "DEPTH", sceneDepth);
			oitAccumulation = graph.writeColour(oitPass, oitAccumulation, GraphLoadOp::Clear, GraphStoreOp::Store, glm::vec4(0.0f));
			oitOpticalDepth = graph.writeColour(oitPass, oitOpticalDepth, GraphLoadOp::Clear, GraphStoreOp::Store, glm::vec4(0.0f));
			GraphRasterState oitRaster;
			oitRaster.explicitState = true;
			oitRaster.depthTest = false;
			oitRaster.depthWrite = false;
			oitRaster.cullMode = GraphCullMode::None;
			oitRaster.blend = true;
			oitRaster.sourceColourBlend = GraphBlendFactor::One;
			oitRaster.destinationColourBlend = GraphBlendFactor::One;
			oitRaster.sourceAlphaBlend = GraphBlendFactor::One;
			oitRaster.destinationAlphaBlend = GraphBlendFactor::One;
			oitRaster.multisample = false;
			graph.setPassRasterState(oitPass, oitRaster);

			auto oitResolve = graph.addPass("ParticleWeightedOitResolve", GraphPassType::Fullscreen);
			graph.setPassCallbackFactory(oitResolve, "MPP.ParticleWeightedOitResolve");
			graph.bindSampler(oitResolve, "SCENE", presentationTexture);
			graph.bindSampler(oitResolve, "ACCUMULATION", oitAccumulation);
			graph.bindSampler(oitResolve, "OPTICAL_DEPTH", oitOpticalDepth);
			if (useMrtEmissiveMask) graph.bindSampler(oitResolve, "BLOOM", bloomMask);
			auto oitComposite = graph.createImage("ParticleOitComposite", makeColour(pbr ? GraphImageFormat::Rgba16f : GraphImageFormat::Rgba8));
			presentationTexture = graph.writeColour(oitResolve, oitComposite, GraphLoadOp::DontCare, GraphStoreOp::Store);
			if (useMrtEmissiveMask)
			{
				auto oitBloomComposite = graph.createImage("ParticleOitBloomComposite", makeColour(GraphImageFormat::Rgba16f));
				bloomMask = graph.writeColour(oitResolve, oitBloomComposite, GraphLoadOp::DontCare, GraphStoreOp::Store);
			}

			// Conventional classes remain separate authored passes with complete raster
			// state. BLEND_MODE only selects their matching indirect-command span.
			auto addParticlePrimitivePass = [&](std::string const& name, std::string const& factory,
				ParticleBlendClass blendClass, GraphBlendFactor destinationColour, GraphBlendFactor sourceAlpha)
			{
				auto pass = graph.addPass(name, GraphPassType::Scene);
				graph.setPassCallbackFactory(pass, factory);
				graph.bindSampler(pass, "DEPTH", sceneDepth);
				UniformCollection parameters;
				parameters.setUniform("BLEND_MODE", int32_t(blendClass));
				graph.setPassParameters(pass, parameters);
				presentationTexture = graph.writeColour(pass, presentationTexture, GraphLoadOp::Load, GraphStoreOp::Store);
				if (useMrtEmissiveMask)
					bloomMask = graph.writeColour(pass, bloomMask, GraphLoadOp::Load, GraphStoreOp::Store);

				GraphRasterState raster;
				raster.explicitState = true;
				raster.depthTest = false;
				raster.depthWrite = false;
				raster.cullMode = GraphCullMode::None;
				raster.blend = true;
				raster.sourceColourBlend = GraphBlendFactor::SourceAlpha;
				raster.destinationColourBlend = destinationColour;
				raster.sourceAlphaBlend = sourceAlpha;
				raster.destinationAlphaBlend = blendClass == ParticleBlendClass::Additive
					? GraphBlendFactor::One : GraphBlendFactor::OneMinusSourceAlpha;
				raster.multisample = false;
				graph.setPassRasterState(pass, raster);
			};
			// Conventional alpha is laid down first; additive energy is independent of
			// ordering. Authors remain free to choose another order in graph templates.
			addParticlePrimitivePass("ParticleAlpha", "MPP.ParticleScene", ParticleBlendClass::Alpha,
				GraphBlendFactor::OneMinusSourceAlpha, GraphBlendFactor::One);
			addParticlePrimitivePass("TrailAlpha", "MPP.TrailScene", ParticleBlendClass::Alpha,
				GraphBlendFactor::OneMinusSourceAlpha, GraphBlendFactor::One);
			addParticlePrimitivePass("TrailAdditive", "MPP.TrailScene", ParticleBlendClass::Additive,
				GraphBlendFactor::One, GraphBlendFactor::Zero);
			addParticlePrimitivePass("ParticleAdditive", "MPP.ParticleScene", ParticleBlendClass::Additive,
				GraphBlendFactor::One, GraphBlendFactor::Zero);

			// Distortion is an orthogonal appearance output: opted-in billboards are
			// drawn again into an additive signed RG field, then composited over the
			// completed particle scene as the first image-space post effect.
			auto distortion = graph.createImage("ParticleDistortion", makeColour(GraphImageFormat::Rg16f));
			auto distortionPass = graph.addPass("ParticleDistortion", GraphPassType::Scene);
			graph.setPassCallbackFactory(distortionPass, "MPP.ParticleDistortion");
			graph.bindSampler(distortionPass, "DEPTH", sceneDepth);
			distortion = graph.writeColour(distortionPass, distortion, GraphLoadOp::Clear, GraphStoreOp::Store, glm::vec4(0.0f));
			GraphRasterState distortionRaster;
			distortionRaster.explicitState = true;
			distortionRaster.depthTest = false;
			distortionRaster.depthWrite = false;
			distortionRaster.cullMode = GraphCullMode::None;
			distortionRaster.blend = true;
			distortionRaster.sourceColourBlend = GraphBlendFactor::One;
			distortionRaster.destinationColourBlend = GraphBlendFactor::One;
			distortionRaster.sourceAlphaBlend = GraphBlendFactor::One;
			distortionRaster.destinationAlphaBlend = GraphBlendFactor::One;
			distortionRaster.multisample = false;
			graph.setPassRasterState(distortionPass, distortionRaster);

			auto distortionComposite = graph.addPass("ParticleDistortionComposite", GraphPassType::Fullscreen);
			graph.setPassCallbackFactory(distortionComposite, "MPP.ParticleDistortionComposite");
			graph.bindSampler(distortionComposite, "SCENE", presentationTexture);
			graph.bindSampler(distortionComposite, "DISTORTION", distortion);
			auto distortedScene = graph.createImage("ParticleDistortionComposite", makeColour(pbr ? GraphImageFormat::Rgba16f : GraphImageFormat::Rgba8));
			presentationTexture = graph.writeColour(distortionComposite, distortedScene, GraphLoadOp::DontCare, GraphStoreOp::Store);
			shadedSceneTexture = presentationTexture;
		}

		enum class BloomGraphStep { Extract, Horizontal, Vertical, Composite };
		vector<GraphPassHandle> bloomPasses;
		vector<GraphImageHandle> bloomInputs;
		vector<BloomGraphStep> bloomSteps;
		if (mOptions.bloom.enabled && mOptions.graphPasses.bloom)
		{
			GraphImageHandle blurred = bloomMask;
			if (!useMrtEmissiveMask)
			{
				auto bloomExtract = graph.createImage("BloomExtract", makeColour(GraphImageFormat::Rgba16f));
				auto extractPass = graph.addPass("BloomExtract", GraphPassType::Fullscreen);
				graph.readSampled(extractPass, shadedSceneTexture);
				blurred = graph.writeColour(extractPass, bloomExtract);
				bloomPasses.push_back(extractPass);
				bloomInputs.push_back(shadedSceneTexture);
				bloomSteps.push_back(BloomGraphStep::Extract);
			}
			for (uint32_t index = 0; index < mOptions.bloom.blurPasses; ++index)
			{
				auto ping = graph.createImage("BloomPing" + to_string(index), makeColour(GraphImageFormat::Rgba16f));
				auto pingPass = graph.addPass("BloomBlurHorizontal" + to_string(index), GraphPassType::Fullscreen);
				// Declare the blur level rather than leaving the pass to read it back
				// off the digits in its own name.
				UniformCollection blurIteration; blurIteration.setUniform("ITERATION", (int32_t)index);
				graph.setPassParameters(pingPass, blurIteration);
				graph.readSampled(pingPass, blurred);
				ping = graph.writeColour(pingPass, ping);
				bloomPasses.push_back(pingPass);
				bloomInputs.push_back(blurred);
				bloomSteps.push_back(BloomGraphStep::Horizontal);

				auto pong = graph.createImage("BloomPong" + to_string(index), makeColour(GraphImageFormat::Rgba16f));
				auto pongPass = graph.addPass("BloomBlurVertical" + to_string(index), GraphPassType::Fullscreen);
				graph.setPassParameters(pongPass, blurIteration);
				graph.readSampled(pongPass, ping);
				blurred = graph.writeColour(pongPass, pong);
				bloomPasses.push_back(pongPass);
				bloomInputs.push_back(ping);
				bloomSteps.push_back(BloomGraphStep::Vertical);
			}
			auto composite = graph.createImage("BloomComposite", makeColour(GraphImageFormat::Rgba16f));
			auto compositePass = graph.addPass("BloomComposite", GraphPassType::Fullscreen);
			graph.readSampled(compositePass, shadedSceneTexture);
			graph.readSampled(compositePass, blurred);
			presentationTexture = graph.writeColour(compositePass, composite);
			bloomPasses.push_back(compositePass);
			bloomInputs.push_back(blurred);
			bloomSteps.push_back(BloomGraphStep::Composite);
		}

		auto screen = graph.createImage("Presentation", makeColour(GraphImageFormat::Rgba8, true));
		auto toneMapPass = graph.addPass("ToneMapPresentation", GraphPassType::Present);
		graph.readSampled(toneMapPass, presentationTexture);
		graph.writeColour(toneMapPass, screen, GraphLoadOp::Clear, GraphStoreOp::Store, glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));

		auto const& viewport = scene->getViewport();
		auto plan = graph.buildAllocationPlan(glm::uvec2(ssaaDimension((uint32_t)viewport.width,outputAntiAliasing.ssaa),ssaaDimension((uint32_t)viewport.height,outputAntiAliasing.ssaa)));
		try
		{
			mGraphTargets->allocatePhysical(plan,physicalSamples);
		}
		catch (std::exception const& error)
		{
			if (!planarWater) throw;
			disablePlanarReflectionsAfterFailure("reflection-image allocation failed: " + string(error.what()));
			mGraphImageCaptureRequested = captureRequestedThisFrame;
			renderGraphForward(scene, camera, models, pbr);
			return;
		}
		catch (...)
		{
			if (!planarWater) throw;
			disablePlanarReflectionsAfterFailure("reflection-image allocation failed with an unknown error");
			mGraphImageCaptureRequested = captureRequestedThisFrame;
			renderGraphForward(scene, camera, models, pbr);
			return;
		}
		mGraphTargets->bindImported(screen, mRenderSystem->getScreenRenderTarget());
		struct DynamicPreparedOutput{string name;GraphImageHandle image;GraphImageHandle depth;RenderTargetPtr destination;RenderTargetPtr source;bool external;};vector<DynamicPreparedOutput> dynamicOutputs;
		if(mOutputProcessor)
		{
			map<string,RenderTargetPtr> destinations;for(auto const& output:mOptions.outputs){GraphImageHandle handle;GraphImageInfo info;for(uint32_t id=0;id<graph.getImageCount();++id){auto candidate=graph.getImageInfo({id,0});if(candidate.name==output.image){handle={id,0};info=candidate;break;}}if(!handle.isValid())THROW_MPP("Named output '"+output.name+"' references an unknown generated graph image.",__LINE__,__FILE__,__func__);for(uint32_t pass=0;pass<graph.getPassCount();++pass){auto const& passInfo=graph.getPassInfo({pass});for(auto const& attachment:passInfo.colourOutputs)if(attachment.image.id==handle.id&&attachment.image.version>handle.version)handle=attachment.image;}auto destination=info.desc.external?mRenderSystem->getScreenRenderTarget():mGraphTargets->get(handle);destinations.emplace(output.name,destination);GraphImageHandle depthHandle;if(!output.taaDepth.empty()){for(uint32_t id=0;id<graph.getImageCount();++id)if(graph.getImageInfo({id,0}).name==output.taaDepth){depthHandle={id,0};break;}for(uint32_t pass=0;pass<graph.getPassCount();++pass)for(auto const& attachment:graph.getPassInfo({pass}).depthOutputs)if(attachment.image.id==depthHandle.id&&attachment.image.version>depthHandle.version)depthHandle=attachment.image;}dynamicOutputs.push_back({output.name,handle,depthHandle,destination,{},info.desc.external});}mOutputProcessor->rebuild(mOptions.outputs,graph,destinations,mRenderSystem->getOptions().antiAliasing);map<uint32_t,RenderTargetPtr> externalSources;for(auto& output:dynamicOutputs)if(output.external){auto [found,inserted]=externalSources.emplace(output.image.id,mOutputProcessor->getInput(output.name));output.source=found->second;if(inserted)mGraphTargets->bindImported(output.image,output.source);}
		}
		if (shadowDepth.isValid()) mGraphTargets->bindImported(shadowDepth, shadowTarget);

		mGraphExecutor->clearPassCallbacks();
		if (shadowDepth.isValid())
		{
			bool const pointShadow = mRenderSystem->getShadowDomainOptions(mOptions.shadowDomain).light.type == ShadowLightType::Point;
			for (uint32_t index = 0; index < shadowPasses.size(); ++index)
			{
				auto pass = shadowPasses[index];
				mGraphExecutor->setPassCallback(graph, pass, [this, shadowModels, pointShadow, index](RenderGraphExecutionContext const&)
				{
					mRenderSystem->renderShadowDomain(mOptions.shadowDomain, shadowModels, pointShadow ? index : UINT32_MAX);
				});
			}
		}
		if (!mOptions.generatedWater)
		{
			mGraphExecutor->setPassCallback(graph, scenePass, [this, scene, models, camera](RenderGraphExecutionContext const& context)
			{
				if (mOptions.graphPasses.scene && !models.empty() && scene->show3dModels() && mPasses.back())
				{
					if (mOptions.depthPrepass)
						mRenderSystem->renderDepthPrepass(
							models, camera, context.getPass().colourOutputs.size());
					mPasses.back()->render(models, camera);
					mRenderSystem->flushVertexBuffers();
				}
			});
		}
		for (size_t index = 0; index < ssaoPasses.size(); ++index)
		{
			auto pass = ssaoPasses[index];
			auto input = ssaoInputs[index];
			switch (ssaoSteps[index])
			{
			case SsaoGraphStep::Raw:
				mGraphExecutor->setPassCallback(graph, pass, [this, sceneDepth, sceneNormals, camera](RenderGraphExecutionContext const& context)
				{
					auto depthTexture = dynamic_cast<RenderTexture*>(context.getImage(sceneDepth).get());
					auto projection = camera->getProjectionTransform();
					if (mOptions.ambientOcclusion.method == AmbientOcclusionMethod::Ssao)
						mRenderSystem->renderSSAORaw(depthTexture, projection, glm::inverse(projection), mOptions.ambientOcclusion.ssao);
					else
						mRenderSystem->renderGTAORaw(depthTexture, sceneNormals.isValid() ? dynamic_cast<Texture*>(context.getImage(sceneNormals).get()) : nullptr, projection, glm::inverse(projection), mOptions.ambientOcclusion.gtao);
				});
				break;
			case SsaoGraphStep::Blur:
				mGraphExecutor->setPassCallback(graph, pass, [this, input, sceneDepth](RenderGraphExecutionContext const& context)
				{
					mRenderSystem->renderSSAOBlur(dynamic_cast<Texture*>(context.getImage(input).get()),
						dynamic_cast<RenderTexture*>(context.getImage(sceneDepth).get()), (mOptions.ambientOcclusion.method == AmbientOcclusionMethod::Ssao ? mOptions.ambientOcclusion.ssao.blurRadius : mOptions.ambientOcclusion.gtao.blurRadius));
				});
				break;
			case SsaoGraphStep::Composite:
				mGraphExecutor->setPassCallback(graph, pass, [this, sceneHdr, input, ssaoModulationImage](RenderGraphExecutionContext const& context)
				{
					mRenderSystem->renderSSAOCombine(dynamic_cast<Texture*>(context.getImage(sceneHdr).get()), dynamic_cast<Texture*>(context.getImage(input).get()),
						ssaoModulationImage.isValid() ? dynamic_cast<Texture*>(context.getImage(ssaoModulationImage).get()) : nullptr);
				});
				break;
			}
		}
		for (size_t index = 0; index < bloomPasses.size(); ++index)
		{
			auto pass = bloomPasses[index];
			auto input = bloomInputs[index];
			switch (bloomSteps[index])
			{
			case BloomGraphStep::Extract:
				mGraphExecutor->setPassCallback(graph, pass, [this, input](RenderGraphExecutionContext const& context)
				{
					mRenderSystem->renderBloomExtract(static_cast<RenderTexture*>(context.getImage(input).get()), mOptions.bloom.threshold);
				});
				break;
			case BloomGraphStep::Horizontal:
			case BloomGraphStep::Vertical:
			{
				bool const horizontal = bloomSteps[index] == BloomGraphStep::Horizontal;
				mGraphExecutor->setPassCallback(graph, pass, [this, input, horizontal](RenderGraphExecutionContext const& context)
				{
					mRenderSystem->renderBloomBlur(static_cast<RenderTexture*>(context.getImage(input).get()), horizontal ? glm::vec2(1.0f, 0.0f) : glm::vec2(0.0f, 1.0f));
				});
				break;
			}
			case BloomGraphStep::Composite:
			{
				auto sceneInput = shadedSceneTexture;
				mGraphExecutor->setPassCallback(graph, pass, [this, sceneInput, input](RenderGraphExecutionContext const& context)
				{
					mRenderSystem->renderBloomCombine(static_cast<RenderTexture*>(context.getImage(sceneInput).get()), static_cast<RenderTexture*>(context.getImage(input).get()), mOptions.bloom.intensity);
				});
				break;
			}
			}
		}
		mGraphExecutor->setPassCallback(graph, toneMapPass, [this, presentationTexture, pbr](RenderGraphExecutionContext const& context)
		{
			if (!mOptions.graphPasses.presentation) return;
			auto texture = static_cast<RenderTexture*>(context.getImage(presentationTexture).get());
			if (pbr)
			{
				mRenderSystem->renderToneMappedFullscreenQuad(texture, mOptions.exposure, mOptions.toneMapOperator == PbrToneMapOperator::Aces);
			}
			else
			{
				mRenderSystem->renderFullscreenQuad(texture, BlendMode::One, BlendMode::Zero);
			}
		});
		RenderGraphFrameContext frameContext;
		frameContext.renderSystem = mRenderSystem;
		frameContext.scene = scene;
		frameContext.camera = camera;
		frameContext.visibleModels = models;
		frameContext.pipelineOptions = &mOptions;
		frameContext.sceneRenderPass = mPasses.back();
		frameContext.hasWaterPass = graphHasWaterPass(graph);
		frameContext.waterReflectionEnabled = reflectionEnabled;
		mGraphExecutor->setFrameContext(&frameContext);
		if (mOptions.graphPasses.particles && graphHasParticlePass(graph)) mRenderSystem->simulateParticles();
		if (mOptions.graphPasses.particles && graphHasTrailPass(graph)) mRenderSystem->simulateTrails();
		beginFlowSnapshot();
		try
		{
			mGraphExecutor->execute(graph, *mGraphTargets, mRenderSystem->getCaps());
			mGraphExecutor->setFrameContext(nullptr);
			for(auto const& output:dynamicOutputs){auto depth=output.depth.isValid()?mGraphTargets->get(output.depth):output.destination;mOutputProcessor->present(output.name,output.destination,output.external?output.source:mGraphTargets->get(output.image),depth,taaFrame?&*taaFrame:nullptr);}
			map<string,RenderTargetPtr> completedOutputs;for(auto const& output:dynamicOutputs)completedOutputs.emplace(output.name,output.destination);mNamedOutputTargets=std::move(completedOutputs);
			publishFlowSnapshot();
		}
		catch (std::exception const& error)
		{
			auto const failedPass = mGraphExecutor->getLastFailedPassName();
			mGraphExecutor->setFrameContext(nullptr);
			discardFlowSnapshot();
			if (!planarWater || failedPass.rfind("PlanarReflection", 0) != 0) throw;
			disablePlanarReflectionsAfterFailure("reflected-scene pass '" + failedPass + "' failed: " + error.what());
			mGraphImageCaptureRequested = captureRequestedThisFrame;
			renderGraphForward(scene, camera, models, pbr);
			return;
		}
		catch (...)
		{
			auto const failedPass = mGraphExecutor->getLastFailedPassName();
			mGraphExecutor->setFrameContext(nullptr);
			discardFlowSnapshot();
			if (!planarWater || failedPass.rfind("PlanarReflection", 0) != 0) throw;
			disablePlanarReflectionsAfterFailure("reflected-scene pass '" + failedPass + "' failed with an unknown error");
			mGraphImageCaptureRequested = captureRequestedThisFrame;
			renderGraphForward(scene, camera, models, pbr);
			return;
		}
	}

	void RenderPipeline::render(ScenePtr scene, CameraPtr camera,
		glm::vec2 const& offset2d, ScenePassOverrides const& overrides)
	{
		GpuDebugScope pipelineScope("RenderPipeline: " + mName + " [" + pipelineModeName(mOptions.mode) + "]");
		// Set viewport
		auto const& viewport = scene->getViewport();
		mRenderSystem->setViewport(viewport.x, viewport.y, (size_t)viewport.width, (size_t)viewport.height);

		// Lazily create shadow-domain resources for any participating pipeline.
		// The depth pass and shader consumption follow in later shadow milestones.
		if (!mOptions.shadowDomain.empty())
		{
			mRenderSystem->ensureShadowDomainResources(mOptions.shadowDomain);
		}

		// Scene passes
		mRenderSystem->setProjection3dPerspective(
			camera->getFov(),
			camera->getNearClipDistance(),
			camera->getFarClipDistance());
		

		auto const& models = scene->get3dModelsInView(camera);
		bool const graphPbr = mOptions.mode == RenderPipelineMode::GraphPbrForward || mOptions.mode == RenderPipelineMode::XmlGraphPbrForward;
		bool const graphLegacy = mOptions.mode == RenderPipelineMode::GraphLegacyForward;
		bool const graphForward = graphPbr || graphLegacy;
		if ((mOptions.mode == RenderPipelineMode::PbrForward || graphPbr) && scene->ownsPbrLights()) mRenderSystem->setPbrLights(scene->getPbrLights());
		// Published once per frame alongside the lights so any scene material
		// program can work in view space. MPP.WaterScene republishes it with its
		// own target's dimensions, which are the viewport its ray march projects
		// into and need not be the scene viewport.
		mRenderSystem->setCameraFrame(camera->getViewTransform(), camera->getProjectionTransform(),
			glm::vec2((float)viewport.width, (float)viewport.height),
			camera->getNearClipDistance(), camera->getFarClipDistance(), mRenderSystem->getElapsedSeconds());
		if (!graphForward && !mOptions.shadowDomain.empty())
		{
			GpuDebugScope shadowScope("Pass: ShadowDomain [" + mOptions.shadowDomain + "]");
			auto shadowModels = models;
			auto const& shadow = mRenderSystem->getShadowDomainOptions(mOptions.shadowDomain);
			if (shadow.light.type == ShadowLightType::Point)
				shadowModels = scene->get3dModelsInSphere(shadow.light.position, shadow.light.range);
			if (mRenderSystem->prepareShadowDomain(mOptions.shadowDomain, shadowModels))
				mRenderSystem->renderShadowDomain(mOptions.shadowDomain, shadowModels);
		}
		mRenderSystem->setActiveShadowDomain(mOptions.shadowDomain);

		map<string, ResourcePtr> pipelineSamplerOverrides;
		if (overrides.uniforms.getUniformData().contains("MPP_VIRTUAL_CAMERA"))
			THROW_MPP("MPP_VIRTUAL_CAMERA is reserved by scene execution.", __LINE__, __FILE__, __func__);
		auto const savedSamplerOverrides = mRenderSystem->mActivePipelineSamplerOverrides;
		auto const savedUniformOverrides = mRenderSystem->mActivePipelineUniformOverrides;
		bool passOverridesRestored = false;
		auto restorePassOverrides = [&]
		{
			if (passOverridesRestored) return;
			mRenderSystem->mActivePipelineSamplerOverrides = savedSamplerOverrides;
			mRenderSystem->mActivePipelineUniformOverrides = savedUniformOverrides;
			passOverridesRestored = true;
		};
		struct PassOverrideRestoreGuard
		{
			function<void()> restore;
			~PassOverrideRestoreGuard() { try { restore(); } catch (...) {} }
		} passOverrideRestoreGuard{ restorePassOverrides };
		UniformCollection pipelineUniformOverrides = overrides.uniforms;
		pipelineUniformOverrides.setUniform("MPP_VIRTUAL_CAMERA", int32_t{ 0 });
		mRenderSystem->setActivePipelineUniformOverrides(pipelineUniformOverrides);
		if (mOptions.mode == RenderPipelineMode::PbrForward || graphPbr)
		{
			mRenderSystem->setActivePbrEnvironment(mOptions.environment);
			auto const cubeFallback = mRenderSystem->getResourceManager()->getResource("__mpp_tex_pbr_ibl_cube__");
			auto const brdfFallback = mRenderSystem->getResourceManager()->getResource("__mpp_tex_pbr_brdf_lut__");
			pipelineSamplerOverrides["PBR_IRRADIANCE_MAP"] = mOptions.environment && mOptions.environment->irradianceMap ? mOptions.environment->irradianceMap : cubeFallback;
			pipelineSamplerOverrides["PBR_PREFILTERED_SPECULAR_MAP"] = mOptions.environment && mOptions.environment->prefilteredSpecularMap ? mOptions.environment->prefilteredSpecularMap : cubeFallback;
			pipelineSamplerOverrides["PBR_BRDF_LUT"] = mOptions.environment && mOptions.environment->brdfIntegrationLut ? mOptions.environment->brdfIntegrationLut : brdfFallback;
			if ((!mOptions.environment || !mOptions.environment->irradianceMap || !mOptions.environment->prefilteredSpecularMap || !mOptions.environment->brdfIntegrationLut) && !mWarnedMissingPbrEnvironment)
			{
				mRenderSystem->warnMessage("PBR pipeline '" + mName + "' has no complete environment; neutral IBL fallbacks are active.");
				mWarnedMissingPbrEnvironment = true;
			}
		}
		for (auto const& [name, resource] : overrides.samplers)
			pipelineSamplerOverrides[name] = resource;
		mRenderSystem->setActivePipelineSamplerOverrides(pipelineSamplerOverrides);
		if (graphForward)
		{
			renderGraphForward(scene, camera, models, graphPbr);
		}
		else
		{
			for (size_t passIndex = 0; passIndex < mPasses.size(); ++passIndex)
			{
				auto const& pass = mPasses[passIndex];
				GpuDebugScope sceneScope("Pass: Scene " + to_string(passIndex) + (mOptions.mode == RenderPipelineMode::PbrForward ? " [HDR PBR]" : " [LDR Legacy]"));
				// Start pass
				pass->bindRenderTarget();

				// Clear
				mRenderSystem->clearScreen(scene->getClearColour());

				// Render pass
				if (!models.empty() && scene->show3dModels())
				{
					if (mOptions.depthPrepass)
						mRenderSystem->renderDepthPrepass(models, camera, 1);
					pass->render(models, camera);

					// Flush
					mRenderSystem->flushVertexBuffers();
				}
			}
		}
		restorePassOverrides();
		mRenderSystem->setActiveShadowDomain("");
		if (mOptions.mode == RenderPipelineMode::PbrForward || graphPbr)
		{
			mRenderSystem->setActivePbrEnvironment(nullptr);
		}

		// Graph execution restores the target that was active before the graph.
		// Explicitly reactivate the screen before UI/2D rendering; otherwise the
		// next frame can draw UI into a transient graph target and flicker.
		if (graphForward)
		{
			mRenderSystem->renderToScreen();
			// With presentation disabled no graph pass writes the double-buffered
			// backbuffer. Clear it explicitly so alternating stale buffers do not
			// look like graph/UI flicker during pass isolation.
			if (!mOptions.graphPasses.presentation)
			{
				mRenderSystem->clearScreen(scene->getClearColour());
			}
		}

		// Reset viewport
		mRenderSystem->resetViewport();

		if (!graphForward)
		{
			// Pipeline image effects run after all material shading. PBR bloom is
			// therefore composed in HDR before tone mapping; legacy uses the same
			// effect sequence on its completed LDR scene target.
			mRenderSystem->setProjection2dOrthographic();
			mRenderSystem->resetTransform();
			auto sceneTexture = static_cast<RenderTexture*>(mPasses.back()->getRenderTarget().get());
			Texture* presentationTexture = sceneTexture;
			ensureBloomTargets(sceneTexture->getWidth(), sceneTexture->getHeight());
			if (mOptions.bloom.enabled)
			{
				GpuDebugScope bloomScope(string("Post: Bloom [") + (mOptions.mode == RenderPipelineMode::PbrForward ? "HDR" : "LDR") + "]");
				{
					GpuDebugScope extractScope("Bloom: Extract");
					mRenderSystem->setRenderTarget(mBloomExtractTarget);
					mRenderSystem->renderBloomExtract(sceneTexture, mOptions.bloom.threshold);
				}

				Texture* blurredTexture = static_cast<RenderTexture*>(mBloomExtractTarget.get());
				for (uint32_t pass = 0; pass < mOptions.bloom.blurPasses; ++pass)
				{
					{
						GpuDebugScope blurScope("Bloom: Blur Horizontal " + to_string(pass));
						mRenderSystem->setRenderTarget(mBloomPingTarget);
						mRenderSystem->renderBloomBlur(blurredTexture, glm::vec2(1.0f, 0.0f));
					}
					{
						GpuDebugScope blurScope("Bloom: Blur Vertical " + to_string(pass));
						mRenderSystem->setRenderTarget(mBloomPongTarget);
						mRenderSystem->renderBloomBlur(static_cast<RenderTexture*>(mBloomPingTarget.get()), glm::vec2(0.0f, 1.0f));
					}
					blurredTexture = static_cast<RenderTexture*>(mBloomPongTarget.get());
				}

				{
					GpuDebugScope compositeScope("Bloom: Composite");
					mRenderSystem->setRenderTarget(mBloomCompositeTarget);
					mRenderSystem->renderBloomCombine(sceneTexture, blurredTexture, mOptions.bloom.intensity);
				}
				presentationTexture = static_cast<RenderTexture*>(mBloomCompositeTarget.get());
			}

			// Render to screen
			mRenderSystem->resetTransform();
			mRenderSystem->renderToScreen();
			mRenderSystem->clearScreen(scene->getClearColour());

			if (mOptions.mode == RenderPipelineMode::PbrForward)
			{
				GpuDebugScope presentationScope("Post: Tone Map + Presentation [" + string(mOptions.toneMapOperator == PbrToneMapOperator::Aces ? "ACES" : "Reinhard") + "]");
				mRenderSystem->renderToneMappedFullscreenQuad(presentationTexture, mOptions.exposure, mOptions.toneMapOperator == PbrToneMapOperator::Aces);
			}
			else
			{
				GpuDebugScope presentationScope("Post: Presentation [LDR]");
				mRenderSystem->renderFullscreenQuad(presentationTexture, mpp::BlendMode::One, mpp::BlendMode::Zero);
			}
		}
		else
		{
			mRenderSystem->setProjection2dOrthographic();
			mRenderSystem->resetTransform();
		}

		// 2d models
		if (scene->show2dModels())
		{
			GpuDebugScope overlayScope("Pass: 2D Overlay");
			auto orderedModels = scene->get2dModelsInView();

			// Sort models.
			sort(orderedModels.begin(), orderedModels.end(), [](auto const& a, auto const& b)
			{
				return a.second < b.second;
			});

			mRenderSystem->pushModelMatrix();

			for (auto orderedModel: orderedModels)
			{
				auto model = orderedModel.first;

				auto const& origin = model->getOrigin();
				auto const& offset = model->getOffset();
				float angle = model->getAngle();
				float orbit = model->getOrbitAngle();
				auto const& scale = model->getScale();

				mRenderSystem->resetTransform();

				mRenderSystem->translateTransform2d(glm::vec2(-offset2d.x, -offset2d.y));

				// Scale and rotate object, then rotate around the origin, then move to world position.
				mRenderSystem->translateTransform2d(origin);
				mRenderSystem->rotateTransform2d(orbit);
				mRenderSystem->translateTransform2d(offset);
				mRenderSystem->rotateTransform2d(angle);
				mRenderSystem->scaleTransform2d(scale);

				model->render(camera);
			}

			mRenderSystem->popModelMatrix();

			// In case final model was batched and we're not rendering anything else this frame
			mRenderSystem->flushVertexBuffers();  
		}
	}
}