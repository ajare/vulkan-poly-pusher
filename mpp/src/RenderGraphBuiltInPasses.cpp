#include <algorithm>
#include <cmath>
#include <string>

#include "mpp/RenderGraphBuiltInPasses.h"
#include "mpp/RenderGraphExecutor.h"
#include "mpp/RenderGraphPassFactoryRegistry.h"
#include "mpp/RenderGraphScenePass.h"
#include "mpp/RenderSystem.h"
#include "mpp/RenderPipeline.h"
#include "mpp/ResourceManager.h"
#include "mpp/Model.h"
#include "mpp/PbrMaterial.h"
#include "mpp/PostEffectMaterial.h"
#include "mpp/ParticleData.h"
#include "mpp/MppException.h"

namespace mpp
{
	namespace
	{
		class ShadowDepthPass final : public RenderGraphScenePass
		{
		public:
			void execute(RenderGraphExecutionContext const& context) override
			{
				auto const& frame = context.getFrame();
				if (frame.pipelineOptions && frame.pipelineOptions->graphPasses.shadow && !frame.pipelineOptions->shadowDomain.empty())
					frame.renderSystem->renderShadowDomain(frame.pipelineOptions->shadowDomain, frame.visibleModels);
			}
		};

		float parameter(RenderGraphExecutionContext const& context, std::string const& name, float fallback)
		{
			auto const& values = context.getParameters().getUniformData();
			auto const found = values.find(name);
			return found == values.end() || found->second.size < sizeof(float) ? fallback : *reinterpret_cast<float const*>(found->second.data);
		}

		int32_t integerParameter(RenderGraphExecutionContext const& context, std::string const& name, int32_t fallback)
		{
			auto const& values = context.getParameters().getUniformData();
			auto const found = values.find(name);
			return found == values.end() || found->second.size < sizeof(int32_t) ? fallback : *reinterpret_cast<int32_t const*>(found->second.data);
		}

		Texture* input(RenderGraphExecutionContext const& context, size_t index)
		{
			auto const& bindings = context.getPass().samplerBindings;
			return index < bindings.size() ? dynamic_cast<Texture*>(context.getImage(bindings[index].image).get()) : nullptr;
		}
		Texture* input(RenderGraphExecutionContext const& context, std::string const& sampler)
		{
			auto const& bindings = context.getPass().samplerBindings;
			auto found = std::find_if(bindings.begin(), bindings.end(), [&](auto const& binding)
			{
				return binding.sampler == sampler;
			});
			return found != bindings.end() ? dynamic_cast<Texture*>(context.getImage(found->image).get()) : nullptr;
		}

		ResourcePtr resourceInput(RenderGraphExecutionContext const& context, std::string const& sampler)
		{
			auto const& bindings = context.getPass().samplerBindings;
			auto found = std::find_if(bindings.begin(), bindings.end(), [&](auto const& binding)
			{
				return binding.sampler == sampler;
			});
			if (found == bindings.end()) return {};
			auto texture = std::dynamic_pointer_cast<RenderTexture>(context.getImage(found->image));
			return texture ? std::static_pointer_cast<Resource>(texture) : ResourcePtr{};
		}

		// The one generic post-effect-chain pass type: a fullscreen quad shaded by
		// whichever PostEffectMaterial the authored pass names via programResource,
		// with sampler inputs bound by name (RenderGraphExecutionContext's existing
		// generic mechanism) and default uniforms taken from the material,
		// overridden by any graph-authored/executor-overridden parameters. Adding a
		// new post effect is therefore a PostEffectMaterial + shader, never a new
		// pass class -- this replaced the old bespoke BloomExtractPass/BloomBlurPass/
		// BloomCompositePass/ToneMapPresentPass classes (see doc/POST_EFFECT_CHAIN_IMPLEMENTATION_PLAN.md).
		class FullscreenEffectPass final : public RenderGraphScenePass
		{
		public:
			void execute(RenderGraphExecutionContext const& context) override
			{
				auto const& frame = context.getFrame();
				auto const& pass = context.getPass();
				Texture* primary = input(context, 0);
				bool const enabled = integerParameter(context, "ENABLED", 1) != 0;
				if (!enabled || pass.programResource.empty())
				{
					// Copy-through: a disabled effect must not break the chain for
					// whatever reads its output next (matches the existing bloom-blur
					// disabled behaviour this pass generalizes).
					frame.renderSystem->renderFullscreenQuad(primary, BlendMode::One, BlendMode::Zero);
					return;
				}
				// A PbrPipelineDocument-authored pass can only name its own
				// LocalResources by their bare authored name -- the document has no
				// way to predict the dynamically-generated root
				// (RenderPipelineOptions::resourceRoot) they end up registered
				// under. Try that qualified name first, matching
				// PbrPipelineRuntime::resolve()'s exact fallback order, before
				// falling back to a plain global lookup (DemoSuite's XmlGraphPBR
				// declares its materials as global names and never sets
				// resourceRoot, so it always takes this fallback).
				auto* resourceMgr = frame.renderSystem->getResourceManager();
				ResourcePtr materialResource;
				if (frame.pipelineOptions && !frame.pipelineOptions->resourceRoot.empty())
					materialResource = resourceMgr->getResource(frame.pipelineOptions->resourceRoot + "/" + pass.programResource, true);
				if (!materialResource) materialResource = resourceMgr->getResource(pass.programResource);
				materialResource->load();
				auto* material = dynamic_cast<PostEffectMaterial*>(materialResource.get());
				if (!material)
					THROW_MPP("FullscreenEffectPass '" + pass.name + "' programResource '" + pass.programResource + "' is not a PostEffectMaterial.", __LINE__, __FILE__, __func__);

				std::vector<std::pair<std::string, Texture*>> samplers;
				for (auto const& binding : pass.samplerBindings)
					samplers.push_back({ binding.sampler, dynamic_cast<Texture*>(context.getImage(binding.image).get()) });

				// Material defaults, then per-pass overrides on top -- the same layering
				// setPostEffectParameter/setPassParameterOverrides already give every
				// other graph pass.
				UniformCollection parameters = material->getUniforms();
				for (auto const& [name, value] : context.getParameters().getUniformData())
					parameters.setUniform(name, value.type, value.count, value.numElements, value.data);

				frame.renderSystem->renderGraphFullscreen(material->getProgram(), samplers, parameters);
			}
		};

		class SSAORawPass final : public RenderGraphScenePass
		{
		public:
			void execute(RenderGraphExecutionContext const& context) override
			{
				auto const& frame = context.getFrame();
				auto* depth = dynamic_cast<RenderTexture*>(input(context, "DEPTH"));
				if (!depth || !frame.camera) THROW_MPP("SSAORawPass requires depth and a camera.", __LINE__, __FILE__, __func__);
				SSAOOptions options;
				options.radius = parameter(context, "RADIUS", options.radius);
				options.intensity = parameter(context, "INTENSITY", options.intensity);
				options.bias = parameter(context, "BIAS", options.bias);
				options.power = parameter(context, "POWER", options.power);
				options.sampleCount = integerParameter(context, "SAMPLE_COUNT", options.sampleCount);
				auto projection = frame.camera->getProjectionTransform();
				frame.renderSystem->renderSSAORaw(depth, projection, glm::inverse(projection), options);
			}
		};

		class GTAORawPass final : public RenderGraphScenePass
		{
		public:
			void execute(RenderGraphExecutionContext const& context) override
			{
				auto const& frame = context.getFrame();
				auto* depth = dynamic_cast<RenderTexture*>(input(context, "DEPTH"));
				if (!depth || !frame.camera) THROW_MPP("GTAORawPass requires depth and a camera.", __LINE__, __FILE__, __func__);
				GTAOOptions options;
				options.radius = parameter(context, "RADIUS", options.radius);
				options.intensity = parameter(context, "INTENSITY", options.intensity);
				options.thickness = parameter(context, "THICKNESS", options.thickness);
				options.horizonBias = parameter(context, "HORIZON_BIAS", options.horizonBias);
				options.falloffStart = parameter(context, "FALLOFF_START", options.falloffStart);
				options.falloffEnd = parameter(context, "FALLOFF_END", options.falloffEnd);
				options.sliceCount = integerParameter(context, "SLICE_COUNT", options.sliceCount);
				options.stepsPerSlice = integerParameter(context, "STEPS_PER_SLICE", options.stepsPerSlice);
				options.power = parameter(context, "POWER", options.power);
				options.normalSource = integerParameter(context, "NORMAL_SOURCE", 0) == 0 ? GTAONormalSource::Depth : GTAONormalSource::Mrt;
				auto projection = frame.camera->getProjectionTransform();
				frame.renderSystem->renderGTAORaw(depth, input(context, "NORMALS"), projection, glm::inverse(projection), options);
			}
		};

		class SSAOBlurPass final : public RenderGraphScenePass
		{
		public:
			void execute(RenderGraphExecutionContext const& context) override
			{
				auto* ambientOcclusion = input(context, "AO");
				auto* depth = dynamic_cast<RenderTexture*>(input(context, "DEPTH"));
				if (!ambientOcclusion || !depth) THROW_MPP("SSAOBlurPass requires occlusion and depth textures.", __LINE__, __FILE__, __func__);
				context.getFrame().renderSystem->renderSSAOBlur(ambientOcclusion, depth, integerParameter(context, "BLUR_RADIUS", 2));
			}
		};

		class SSAOCompositePass final : public RenderGraphScenePass
		{
		public:
			void execute(RenderGraphExecutionContext const& context) override
			{
				auto* scene = input(context, "SCENE"); auto* ambientOcclusion = input(context, "AO");
				if (!scene || !ambientOcclusion) THROW_MPP("SSAOCompositePass requires scene and occlusion textures.", __LINE__, __FILE__, __func__);
				context.getFrame().renderSystem->renderSSAOCombine(scene, ambientOcclusion);
			}
		};

		std::vector<GraphImageFormat> colourFormats()
		{
			return { GraphImageFormat::R8, GraphImageFormat::Rg8, GraphImageFormat::Rgba8, GraphImageFormat::Srgb8Alpha8,
				GraphImageFormat::R16f, GraphImageFormat::Rg16f, GraphImageFormat::Rgba16f, GraphImageFormat::R32f,
				GraphImageFormat::Rg32f, GraphImageFormat::Rgba32f, GraphImageFormat::R11g11b10f, GraphImageFormat::Rgb10a2 };
		}
		std::vector<GraphImageFormat> depthFormats()
		{
			return { GraphImageFormat::Depth16, GraphImageFormat::Depth24, GraphImageFormat::Depth32f, GraphImageFormat::Depth24Stencil8, GraphImageFormat::Depth32fStencil8 };
		}
		GraphPassAuthoringMetadata metadata(std::string displayName, std::string category, GraphPassType type)
		{
			GraphPassAuthoringMetadata result;
			result.displayName = std::move(displayName); result.category = std::move(category); result.type = type; result.supportsRasterState = true;
			return result;
		}

		// Water surfaces read the shaded opaque scene while drawing into it, so they
		// cannot be part of the opaque pass -- they render afterwards, from a frozen
		// copy. The split is per model: a model is water if any of its meshes uses a
		// PbrMaterial with the Water feature.
		bool usesWaterMaterial(SceneModel3dPtr const& sceneModel)
		{
			if (!sceneModel) return false;
			if (sceneModel->getDeferToWaterPass()) return true;
			auto const* model = dynamic_cast<Model const*>(sceneModel->getModel().get());
			if (!model) return false;
			for (int mesh = 0; mesh < model->getNumMeshes(); ++mesh)
			{
				auto const* material = dynamic_cast<PbrMaterial const*>(model->getMesh(mesh)->getMaterial().get());
				if (material && hasPbrFeature(material->getFeatures(), PbrMaterialFeature::Water)) return true;
			}
			return false;
		}

		std::vector<SceneModel3dPtr> selectModels(std::vector<SceneModel3dPtr> const& models, bool water)
		{
			std::vector<SceneModel3dPtr> result;
			for (auto const& model : models) if (usesWaterMaterial(model) == water) result.push_back(model);
			return result;
		}

		bool usesTransparentMaterial(SceneModel3dPtr const& sceneModel)
		{
			if (!sceneModel) return false;
			auto const* model = dynamic_cast<Model const*>(sceneModel->getModel().get());
			if (!model) return false;
			auto const& meshParams = sceneModel->getParams()->getMeshParams();
			auto const defaults = meshParams.find("");
			for (int meshIndex = 0; meshIndex < model->getNumMeshes(); ++meshIndex)
			{
				auto mesh = model->getMesh(meshIndex);
				auto const specific = meshParams.find(mesh->getName());
				auto const* params = specific != meshParams.end() ? &specific->second :
					(defaults != meshParams.end() ? &defaults->second : nullptr);
				if (params && (params->flags & ModelRenderParams::Flag_Visible) == 0) continue;
				auto resource = params && params->material ? params->material : mesh->getMaterial();
				auto const* material = dynamic_cast<Material const*>(resource.get());
				if ((material && material->isTransparent()) || (params && params->blend.value_or(false))) return true;
			}
			return false;
		}

		class PlanarReflectionScenePass final : public RenderGraphScenePass
		{
		public:
			void execute(RenderGraphExecutionContext const& context) override
			{
				auto const& frame = context.getFrame();
				if (!frame.pipelineOptions->graphPasses.scene || !frame.scene || !frame.camera ||
					!frame.sceneRenderPass || !frame.scene->show3dModels()) return;
				auto const& outputs = context.getPass().colourOutputs;
				auto target = outputs.empty() ? nullptr : context.getImage(outputs.front().image);
				if (!target) THROW_MPP("PlanarReflectionScenePass requires a colour target.", __LINE__, __FILE__, __func__);

				PlanarReflectionPlaneDescriptor plane;
				plane.elevation = parameter(context, "ELEVATION", 0.0f);
				plane.viewerSide = integerParameter(context, "VIEWER_SIDE", 0) == 0
					? ReflectionPlaneSide::Above : ReflectionPlaneSide::Below;
				auto const reflected = buildPlanarReflectionView(*frame.camera, plane,
					(float)target->getWidth() / (float)target->getHeight());
				auto reflectedCamera = std::make_shared<VirtualCamera>(
					reflected.view, reflected.projection,
					frame.camera->getNearClipDistance(), frame.camera->getFarClipDistance());
				auto reflectedModels = selectModels(frame.scene->get3dModelsInView(reflectedCamera), false);
				reflectedModels.erase(std::remove_if(reflectedModels.begin(), reflectedModels.end(),
					usesTransparentMaterial), reflectedModels.end());

				auto const savedUniforms = frame.renderSystem->getActivePipelineUniformOverrides();
				auto reflectionUniforms = savedUniforms;
				if (reflectionUniforms.getUniformData().contains("MPP_VIRTUAL_CAMERA"))
					reflectionUniforms.updateUniform("MPP_VIRTUAL_CAMERA", int32_t{ 1 });
				else reflectionUniforms.setUniform("MPP_VIRTUAL_CAMERA", int32_t{ 1 });
				frame.renderSystem->setActivePipelineUniformOverrides(reflectionUniforms);
				frame.renderSystem->setCameraFrame(reflectedCamera->getViewTransform(), reflectedCamera->getProjectionTransform(),
					glm::vec2((float)target->getWidth(), (float)target->getHeight()),
					reflectedCamera->getNearClipDistance(), reflectedCamera->getFarClipDistance(),
					frame.renderSystem->getElapsedSeconds());

				auto restore = [&]
				{
					frame.renderSystem->setActivePipelineUniformOverrides(savedUniforms);
					auto const& viewport = frame.scene->getViewport();
					frame.renderSystem->setCameraFrame(frame.camera->getViewTransform(), frame.camera->getProjectionTransform(),
						glm::vec2((float)viewport.width, (float)viewport.height),
						frame.camera->getNearClipDistance(), frame.camera->getFarClipDistance(),
						frame.renderSystem->getElapsedSeconds());
				};
				try
				{
					if (frame.pipelineOptions->depthPrepass && !reflectedModels.empty())
						frame.renderSystem->renderDepthPrepass(reflectedModels, reflectedCamera, outputs.size());
					if (frame.pipelineOptions->debugEnvironmentCube && frame.pipelineOptions->environment)
					{
						auto const& environment = frame.pipelineOptions->environment;
						auto resource = environment->environmentMap ? environment->environmentMap : environment->backgroundMap;
						frame.renderSystem->renderEnvironmentDebugCube(dynamic_cast<Texture*>(resource.get()), reflectedCamera.get());
					}
					if (!reflectedModels.empty())
					{
						frame.sceneRenderPass->render(reflectedModels, reflectedCamera);
						frame.renderSystem->flushVertexBuffers();
					}
				}
				catch (...) { restore(); throw; }
				restore();
			}
		};

		class ScenePass final : public RenderGraphScenePass
		{
		public:
			void execute(RenderGraphExecutionContext const& context) override
			{
				auto const& frame = context.getFrame();
				if (!frame.pipelineOptions->graphPasses.scene) return;
				auto const opaque = frame.hasWaterPass
					? selectModels(frame.visibleModels, false) : frame.visibleModels;
				if (frame.pipelineOptions->depthPrepass && frame.scene &&
					frame.scene->show3dModels() && !opaque.empty())
				{
					frame.renderSystem->renderDepthPrepass(
						opaque, frame.camera, context.getPass().colourOutputs.size());
				}
				if (frame.pipelineOptions->debugEnvironmentCube && frame.pipelineOptions->environment)
				{
					auto const& environment = frame.pipelineOptions->environment;
					auto resource = environment->environmentMap ? environment->environmentMap : environment->backgroundMap;
					frame.renderSystem->renderEnvironmentDebugCube(dynamic_cast<Texture*>(resource.get()), frame.camera.get());
				}
				if (frame.scene && frame.scene->show3dModels() && frame.sceneRenderPass && !opaque.empty())
				{
					// Water is drawn by MPP.WaterScene after this pass's colour has
					// been copied -- but only if the graph has such a pass. Without
					// one, water shades here and falls back to the cubemap, which is
					// a degraded look rather than an invisible surface.
					frame.sceneRenderPass->render(opaque, frame.camera);
					frame.renderSystem->flushVertexBuffers();
				}
			}
		};

		// A frozen, mip-chained copy of the opaque scene colour for water to sample.
		// SSR reads the shaded scene while drawing into it, which is a read/write
		// hazard against the live target; this is the same copy-then-process shape
		// the bloom chain already uses. The mip chain is regenerated by
		// RenderTexture::deactivate() when the pass target is popped, which is what
		// gives the roughness blur something to sample.
		class SceneColourCopyPass final : public RenderGraphScenePass
		{
		public:
			void execute(RenderGraphExecutionContext const& context) override
			{
				auto const& frame = context.getFrame();
				auto* source = input(context, 0);
				if (!source) THROW_MPP("SceneColourCopyPass '" + context.getPass().name + "' has no source image bound.", __LINE__, __FILE__, __func__);
				frame.renderSystem->renderFullscreenQuad(source, BlendMode::One, BlendMode::Zero);
			}
		};

		// Water-only scene pass: draws the water materials the opaque pass skipped,
		// on top of the opaque result, with the resolved scene colour and the opaque
		// depth buffer bound as pipeline samplers so PBR_SPEC_WATER can march them.
		// A pure draw pass. Simulation is dispatched once per frame outside the
		// graph (ADR 0005), so this may appear any number of times in a graph and
		// runs unchanged from a planar reflection's mirrored camera or a
		// point-shadow face expansion -- none of which advance the particles.
		class ParticleScenePass final : public RenderGraphScenePass
		{
		public:
			void execute(RenderGraphExecutionContext const& context) override
			{
				auto const& frame = context.getFrame();
				if (frame.pipelineOptions && !frame.pipelineOptions->graphPasses.particles) return;
				int32_t const blendMode = integerParameter(context, "BLEND_MODE", -1);
				if (blendMode < int32_t(ParticleBlendClass::Additive) || blendMode > int32_t(ParticleBlendClass::Alpha))
					THROW_MPP("ParticleScenePass '" + context.getPass().name + "' requires BLEND_MODE additive=0 or alpha=1.", __LINE__, __FILE__, __func__);
				auto depth = resourceInput(context, "DEPTH");
				frame.renderSystem->renderParticles(ParticleBlendClass(blendMode), depth);
			}
		};

		// Real mesh particles bind vertex arrays and Material resources, so they are
		// intentionally not an appearance variant of the attribute-less billboard pass.
		class ParticleMeshScenePass final : public RenderGraphScenePass
		{
		public:
			void execute(RenderGraphExecutionContext const& context) override
			{
				auto const& frame = context.getFrame();
				if (frame.pipelineOptions && !frame.pipelineOptions->graphPasses.particles) return;

				// Mesh particles use ordinary materials, so named graph inputs must be
				// exposed through the same authoritative sampler overrides as other
				// material-driven scene passes. This covers shadows and permits authored
				// materials to consume additional pass-local images.
				auto const restore = frame.renderSystem->getActivePipelineSamplerOverrides();
				auto overrides = restore;
				for (auto const& binding : context.getPass().samplerBindings)
					if (auto resource = std::dynamic_pointer_cast<Resource>(context.getImage(binding.image)))
						overrides[binding.sampler] = resource;
				frame.renderSystem->setActivePipelineSamplerOverrides(overrides);
				try { frame.renderSystem->renderMeshParticles(); }
				catch (...)
				{
					frame.renderSystem->setActivePipelineSamplerOverrides(restore);
					throw;
				}
				frame.renderSystem->setActivePipelineSamplerOverrides(restore);
			}
		};

		class TrailScenePass final : public RenderGraphScenePass
		{
		public:
			void execute(RenderGraphExecutionContext const& context) override
			{
				auto const& frame = context.getFrame();
				if (frame.pipelineOptions && !frame.pipelineOptions->graphPasses.particles) return;
				int32_t const blendMode = integerParameter(context, "BLEND_MODE", -1);
				if (blendMode < int32_t(ParticleBlendClass::Additive) || blendMode > int32_t(ParticleBlendClass::Alpha))
					THROW_MPP("TrailScenePass '" + context.getPass().name + "' requires BLEND_MODE additive=0 or alpha=1.", __LINE__, __FILE__, __func__);
				auto depth = resourceInput(context, "DEPTH");
				frame.renderSystem->renderTrails(ParticleBlendClass(blendMode), depth);
			}
		};

		class ParticleVolumetricLightingPass final : public RenderGraphScenePass
		{
		public:
			void execute(RenderGraphExecutionContext const& context) override
			{
				auto const& frame = context.getFrame();
				if (frame.pipelineOptions && !frame.pipelineOptions->graphPasses.particles) return;
				frame.renderSystem->renderParticleVolumetricLighting(resourceInput(context, "DEPTH"));
			}
		};

		class ParticleDistortionPass final : public RenderGraphScenePass
		{
		public:
			void execute(RenderGraphExecutionContext const& context) override
			{
				auto const& frame = context.getFrame();
				if (frame.pipelineOptions && !frame.pipelineOptions->graphPasses.particles) return;
				frame.renderSystem->renderParticleDistortion(resourceInput(context, "DEPTH"));
			}
		};

		class ParticleDistortionCompositePass final : public RenderGraphScenePass
		{
		public:
			void execute(RenderGraphExecutionContext const& context) override
			{
				context.getFrame().renderSystem->renderParticleDistortionComposite(
					input(context, "SCENE"), input(context, "DISTORTION"));
			}
		};

		class ParticleWeightedOitPass final : public RenderGraphScenePass
		{
		public:
			void execute(RenderGraphExecutionContext const& context) override
			{
				auto const& frame = context.getFrame();
				if (frame.pipelineOptions && !frame.pipelineOptions->graphPasses.particles) return;
				auto depth = resourceInput(context, "DEPTH");
				frame.renderSystem->renderParticles(ParticleBlendClass::WeightedOit, depth);
			}
		};

		class ParticleWeightedOitResolvePass final : public RenderGraphScenePass
		{
		public:
			void execute(RenderGraphExecutionContext const& context) override
			{
				auto* scene = input(context, "SCENE");
				auto* accumulation = input(context, "ACCUMULATION");
				auto* opticalDepth = input(context, "OPTICAL_DEPTH");
				auto* bloom = input(context, "BLOOM");
				context.getFrame().renderSystem->renderParticleWeightedOitResolve(scene, accumulation, opticalDepth, bloom);
			}
		};

		class WaterScenePass final : public RenderGraphScenePass
		{
		public:
			void execute(RenderGraphExecutionContext const& context) override
			{
				auto const& frame = context.getFrame();
				if (!frame.pipelineOptions->graphPasses.scene) return;
				if (!frame.scene || !frame.scene->show3dModels() || !frame.sceneRenderPass || !frame.camera) return;
				auto* resolvedScene = input(context, 0);
				if (!resolvedScene) THROW_MPP("WaterScenePass '" + context.getPass().name + "' has no resolved scene colour bound.", __LINE__, __FILE__, __func__);
				// Generated graphs write a distinct WaterComposite image, so seed it
				// with the frozen shaded scene before alpha-compositing water. This is a
				// scene pass, so the executor deliberately leaves the 3D camera active;
				// install the target-sized fullscreen transform only for the copy, then
				// restore the camera before drawing water geometry.
				auto const& outputs = context.getPass().colourOutputs;
				auto target = outputs.empty() ? nullptr : context.getImage(outputs.front().image);
				glm::vec2 const viewport = target
					? glm::vec2((float)target->getWidth(), (float)target->getHeight())
					: glm::vec2((float)frame.renderSystem->getWindowWidth(), (float)frame.renderSystem->getWindowHeight());
				frame.renderSystem->pushProjectionMatrix();
				frame.renderSystem->pushCameraMatrix();
				frame.renderSystem->pushModelMatrix();
				try
				{
					frame.renderSystem->setProjection2dOrthographic();
					frame.renderSystem->resetTransform();
					frame.renderSystem->scaleTransform2d(glm::vec2(
						viewport.x / (float)frame.renderSystem->getWindowWidth(),
						viewport.y / (float)frame.renderSystem->getWindowHeight()));
					frame.renderSystem->renderFullscreenQuad(resolvedScene, BlendMode::One, BlendMode::Zero);
				}
				catch (...)
				{
					frame.renderSystem->popModelMatrix();
					frame.renderSystem->popCameraMatrix();
					frame.renderSystem->popProjectionMatrix();
					throw;
				}
				frame.renderSystem->popModelMatrix();
				frame.renderSystem->popCameraMatrix();
				frame.renderSystem->popProjectionMatrix();

				auto const water = selectModels(frame.visibleModels, true);
				if (water.empty()) return;

				// Pipeline sampler overrides are the same mechanism that binds the IBL
				// maps: authoritative for a matching shader sampler name, so a water
				// material never has to own a slot for a render-graph image.
				auto const restore = frame.renderSystem->getActivePipelineSamplerOverrides();
				auto overrides = restore;
				for (auto const& binding : context.getPass().samplerBindings)
					if (auto resource = std::dynamic_pointer_cast<Resource>(context.getImage(binding.image)))
						overrides[binding.sampler] = resource;
				frame.renderSystem->setActivePipelineSamplerOverrides(overrides);

				// Publish reflection-source metadata alongside the graph samplers. A host
				// Water shader can therefore project its own world-space interface point
				// without knowing how MPP built the reflected camera.
				auto const restoreUniforms = frame.renderSystem->getActivePipelineUniformOverrides();
				auto waterUniforms = restoreUniforms;
				auto const planar = frame.pipelineOptions->waterReflections.technique ==
					WaterReflectionTechnique::Planar;
				waterUniforms.setUniform("MPP_WATER_REFLECTION_TECHNIQUE", int32_t{ planar ? 1 : 0 });
				waterUniforms.setUniform("MPP_PLANAR_REFLECTION_COUNT", int32_t{
					frame.waterReflectionEnabled ? static_cast<int32_t>(frame.pipelineOptions->waterReflections.planarPlanes.size()) : 0 });
				if (!frame.waterReflectionEnabled)
					waterUniforms.setUniform("LIQUID_REFLECTION_ENABLED", int32_t{ 0 });
				for (size_t index = 0; frame.waterReflectionEnabled && index < frame.pipelineOptions->waterReflections.planarPlanes.size(); ++index)
				{
					auto const& plane = frame.pipelineOptions->waterReflections.planarPlanes[index];
					auto reflected = buildPlanarReflectionView(
						*frame.camera, plane, viewport.x / viewport.y);
					auto const suffix = std::to_string(index);
					waterUniforms.setUniform(
						"MPP_PLANAR_REFLECTION_VIEW_PROJECTION_" + suffix,
						reflected.projection * reflected.view);
					waterUniforms.setUniform(
						"MPP_PLANAR_REFLECTION_ELEVATION_" + suffix,
						plane.elevation);
					waterUniforms.setUniform(
						"MPP_PLANAR_REFLECTION_MINIMUM_ELEVATION_" + suffix,
						std::isfinite(plane.minimumMatchingElevation)
							? plane.minimumMatchingElevation : plane.elevation - 0.01f);
					waterUniforms.setUniform(
						"MPP_PLANAR_REFLECTION_MAXIMUM_ELEVATION_" + suffix,
						std::isfinite(plane.maximumMatchingElevation)
							? plane.maximumMatchingElevation : plane.elevation + 0.01f);
				}
				frame.renderSystem->setActivePipelineUniformOverrides(waterUniforms);

				// The march works in view space against this pass's own target, so the
				// viewport it reports has to be the target's, not the window's.
				frame.renderSystem->setCameraFrame(frame.camera->getViewTransform(), frame.camera->getProjectionTransform(),
					viewport, frame.camera->getNearClipDistance(), frame.camera->getFarClipDistance(),
					frame.renderSystem->getElapsedSeconds());

				try
				{
					frame.sceneRenderPass->render(water, frame.camera);
					frame.renderSystem->flushVertexBuffers();
				}
				catch (...)
				{
					frame.renderSystem->setActivePipelineSamplerOverrides(restore);
					frame.renderSystem->setActivePipelineUniformOverrides(restoreUniforms);
					throw;
				}
				frame.renderSystem->setActivePipelineSamplerOverrides(restore);
				frame.renderSystem->setActivePipelineUniformOverrides(restoreUniforms);
			}
		};
	}

	void registerBuiltInRenderGraphPasses(RenderGraphPassFactoryRegistry& registry)
	{
		auto shadow = metadata("Shadow Depth", "Shadows", GraphPassType::Scene);
		shadow.outputs.push_back({ "Depth", true, true, depthFormats() });
		registry.registerScenePassFactory("MPP.ShadowDepth", [] { return std::make_unique<ShadowDepthPass>(); }, shadow);

		auto fullscreenEffect = metadata("Post Effect", "Post Effects", GraphPassType::Fullscreen);
		fullscreenEffect.inputs.push_back({ "Input", "TEX0", true, colourFormats(), {} });
		fullscreenEffect.outputs.push_back({ "Output", false, true, colourFormats() });
		fullscreenEffect.parameters.push_back({ "ENABLED", program::GLSLType::Int, 1, 1, false, false, 0.0, 1.0, "bool" });
		fullscreenEffect.acceptsProgram = true;
		fullscreenEffect.allowAdditionalInputs = true;
		fullscreenEffect.allowAdditionalParameters = true;
		registry.registerScenePassFactory("MPP.FullscreenEffect", [] { return std::make_unique<FullscreenEffectPass>(); }, fullscreenEffect);

		auto ssaoRaw = metadata("SSAO Raw", "Post Effects", GraphPassType::Fullscreen);
		ssaoRaw.inputs.push_back({ "Depth", "DEPTH", true, depthFormats(), {} }); ssaoRaw.outputs.push_back({ "Occlusion", false, true, colourFormats() });
		ssaoRaw.parameters = { { "RADIUS", program::GLSLType::Float, 1, 1, false, false, 0.0, 0.0, "" }, { "INTENSITY", program::GLSLType::Float, 1, 1, false, false, 0.0, 0.0, "" }, { "BIAS", program::GLSLType::Float, 1, 1, false, false, 0.0, 0.0, "" }, { "POWER", program::GLSLType::Float, 1, 1, false, false, 0.0, 0.0, "" }, { "SAMPLE_COUNT", program::GLSLType::Int, 1, 1, false, false, 0.0, 0.0, "" } };
		registry.registerScenePassFactory("MPP.SSAORaw", [] { return std::make_unique<SSAORawPass>(); }, ssaoRaw);
		auto gtaoRaw = metadata("GTAO Raw", "Post Effects", GraphPassType::Fullscreen);
		gtaoRaw.inputs.push_back({ "Depth", "DEPTH", true, depthFormats(), {} }); gtaoRaw.inputs.push_back({ "Normals", "NORMALS", false, { GraphImageFormat::Rg16f }, {} }); gtaoRaw.outputs.push_back({ "Occlusion", false, true, colourFormats() });
		gtaoRaw.parameters = { { "RADIUS", program::GLSLType::Float, 1, 1, false, false, 0.0, 0.0, "" }, { "INTENSITY", program::GLSLType::Float, 1, 1, false, false, 0.0, 0.0, "" }, { "THICKNESS", program::GLSLType::Float, 1, 1, false, false, 0.0, 0.0, "" }, { "HORIZON_BIAS", program::GLSLType::Float, 1, 1, false, false, 0.0, 0.0, "" }, { "FALLOFF_START", program::GLSLType::Float, 1, 1, false, false, 0.0, 1.0, "" }, { "FALLOFF_END", program::GLSLType::Float, 1, 1, false, false, 0.0, 1.0, "" }, { "SLICE_COUNT", program::GLSLType::Int, 1, 1, false, false, 1.0, 16.0, "" }, { "STEPS_PER_SLICE", program::GLSLType::Int, 1, 1, false, false, 1.0, 16.0, "" }, { "POWER", program::GLSLType::Float, 1, 1, false, false, 0.0, 0.0, "" }, { "NORMAL_SOURCE", program::GLSLType::Int, 1, 1, false, false, 0.0, 1.0, "depth=0, mrt=1" } };
		registry.registerScenePassFactory("MPP.GTAORaw", [] { return std::make_unique<GTAORawPass>(); }, gtaoRaw);
		auto ssaoBlur = metadata("SSAO Blur", "Post Effects", GraphPassType::Fullscreen);
		ssaoBlur.inputs.push_back({ "Occlusion", "AO", true, colourFormats(), {} }); ssaoBlur.inputs.push_back({ "Depth", "DEPTH", true, depthFormats(), {} }); ssaoBlur.outputs.push_back({ "Occlusion", false, true, colourFormats() }); ssaoBlur.parameters.push_back({ "BLUR_RADIUS", program::GLSLType::Int, 1, 1, false, false, 0.0, 0.0, "" });
		registry.registerScenePassFactory("MPP.SSAOBlur", [] { return std::make_unique<SSAOBlurPass>(); }, ssaoBlur);
		registry.registerScenePassFactory("MPP.AmbientOcclusionBlur", [] { return std::make_unique<SSAOBlurPass>(); }, ssaoBlur);
		auto ssaoComposite = metadata("SSAO Composite", "Post Effects", GraphPassType::Fullscreen);
		ssaoComposite.inputs.push_back({ "Scene", "SCENE", true, colourFormats(), {} }); ssaoComposite.inputs.push_back({ "Occlusion", "AO", true, colourFormats(), {} }); ssaoComposite.outputs.push_back({ "Output", false, true, colourFormats() });
		registry.registerScenePassFactory("MPP.SSAOComposite", [] { return std::make_unique<SSAOCompositePass>(); }, ssaoComposite);
		registry.registerScenePassFactory("MPP.AmbientOcclusionComposite", [] { return std::make_unique<SSAOCompositePass>(); }, ssaoComposite);

		auto scene = metadata("PBR Scene", "Scene", GraphPassType::Scene);
		scene.inputs.push_back({ "Shadow", "SHADOW_MAP", false, depthFormats(), "NeutralShadow" });
		scene.outputs.push_back({ "HDR Colour", false, true, colourFormats() }); scene.outputs.push_back({ "Emissive MRT", false, false, colourFormats() }); scene.outputs.push_back({ "GTAO shading normals", false, false, { GraphImageFormat::Rg16f } }); scene.outputs.push_back({ "Depth", true, true, depthFormats() });
		scene.materialSlots.push_back("SceneMaterials");
		registry.registerScenePassFactory("MPP.PbrScene", [] { return std::make_unique<ScenePass>(); }, scene);
		scene.displayName = "Legacy Scene";
		registry.registerScenePassFactory("MPP.LegacyScene", [] { return std::make_unique<ScenePass>(); }, scene);

		auto planarReflection = metadata("Planar Reflection", "Scene", GraphPassType::Scene);
		planarReflection.inputs.push_back({ "Shadow", "SHADOW_MAP", false, depthFormats(), "NeutralShadow" });
		planarReflection.outputs.push_back({ "Reflected Scene", false, true, colourFormats() });
		planarReflection.outputs.push_back({ "Depth", true, true, depthFormats() });
		planarReflection.parameters.push_back({ "ELEVATION", program::GLSLType::Float, 1, 1, false, false, 0.0, 0.0, "world units" });
		planarReflection.parameters.push_back({ "VIEWER_SIDE", program::GLSLType::Int, 1, 1, false, false, 0.0, 1.0, "above=0, below=1" });
		planarReflection.materialSlots.push_back("SceneMaterials");
		registry.registerScenePassFactory("MPP.PlanarReflectionScene", [] { return std::make_unique<PlanarReflectionScenePass>(); }, planarReflection);

		auto sceneColourCopy = metadata("Scene Colour Copy", "Scene", GraphPassType::Fullscreen);
		sceneColourCopy.inputs.push_back({ "Scene Colour", "TEX1", true, colourFormats(), {} });
		sceneColourCopy.outputs.push_back({ "Resolved Scene Colour", false, true, colourFormats() });
		registry.registerScenePassFactory("MPP.SceneColourCopy", [] { return std::make_unique<SceneColourCopyPass>(); }, sceneColourCopy);

		auto waterScene = metadata("Water Scene", "Scene", GraphPassType::Scene);
		waterScene.inputs.push_back({ "Resolved Scene Colour", "PBR_SCENE_COLOUR_RESOLVED", true, colourFormats(), {} });
		waterScene.inputs.push_back({ "Scene Depth", "PBR_SCENE_DEPTH", true, depthFormats(), {} });
		waterScene.inputs.push_back({ "Shadow", "SHADOW_MAP", false, depthFormats(), "NeutralShadow" });
		// Generated Planar branches append one reflected-scene sampler per plane.
		waterScene.allowAdditionalInputs = true;
		waterScene.outputs.push_back({ "HDR Colour", false, true, colourFormats() });
		waterScene.outputs.push_back({ "Emissive MRT", false, false, colourFormats() });
		// Deliberately no depth attachment. Water needs the opaque depth buffer as a
		// sampled input for its ray march, and the graph allocates a separate
		// physical target per image version -- so attaching the same depth image
		// would hand this pass an uninitialized copy to test against, quite apart
		// from being a sampler feedback loop. PBR_SPEC_WATER discards fragments
		// behind the sampled depth instead, which is the same occlusion result.
		waterScene.materialSlots.push_back("SceneMaterials");
		registry.registerScenePassFactory("MPP.WaterScene", [] { return std::make_unique<WaterScenePass>(); }, waterScene);

		auto particleScene = metadata("Particles", "Scene", GraphPassType::Scene);
		// DEPTH is an ordinary optional named graph input. HardParticles is the
		// declared no-depth fallback; the callback implements it by disabling the
		// depth sample rather than manufacturing a copy of the live image.
		particleScene.inputs.push_back({ "Scene Depth", "DEPTH", false, depthFormats(), "HardParticles" });
		particleScene.outputs.push_back({ "HDR Colour", false, true, colourFormats() });
		particleScene.outputs.push_back({ "Emissive MRT", false, false, colourFormats() });
		particleScene.parameters.push_back({ "BLEND_MODE", program::GLSLType::Int, 1, 1, true, true, 0.0, 1.0, "additive=0, alpha=1" });
		registry.registerScenePassFactory("MPP.ParticleScene", [] { return std::make_unique<ParticleScenePass>(); }, particleScene);

		auto particleMeshScene = metadata("Mesh Particles", "Scene", GraphPassType::Scene);
		particleMeshScene.inputs.push_back({ "Shadow", "SHADOW_MAP", false, depthFormats(), "NeutralShadow" });
		particleMeshScene.outputs.push_back({ "HDR Colour", false, true, colourFormats() });
		particleMeshScene.outputs.push_back({ "Emissive MRT", false, false, colourFormats() });
		particleMeshScene.outputs.push_back({ "Depth", true, true, depthFormats() });
		particleMeshScene.materialSlots.push_back("ParticleMeshMaterials");
		particleMeshScene.allowAdditionalInputs = true;
		registry.registerScenePassFactory("MPP.ParticleMeshScene", [] { return std::make_unique<ParticleMeshScenePass>(); }, particleMeshScene);

		auto trailScene = metadata("Trails", "Scene", GraphPassType::Scene);
		trailScene.inputs.push_back({ "Scene Depth", "DEPTH", false, depthFormats(), "HardTrails" });
		trailScene.outputs.push_back({ "HDR Colour", false, true, colourFormats() });
		trailScene.outputs.push_back({ "Emissive MRT", false, false, colourFormats() });
		trailScene.parameters.push_back({ "BLEND_MODE", program::GLSLType::Int, 1, 1, true, true, 0.0, 1.0, "additive=0, alpha=1" });
		registry.registerScenePassFactory("MPP.TrailScene", [] { return std::make_unique<TrailScenePass>(); }, trailScene);

		auto particleVolumetricLighting = metadata("Particle Volumetric Lighting", "Lighting", GraphPassType::Scene);
		particleVolumetricLighting.inputs.push_back({ "Scene Depth", "DEPTH", false, depthFormats(), "UnoccludedParticleVolumes" });
		particleVolumetricLighting.outputs.push_back({ "HDR Volumetric Lighting", false, true, colourFormats() });
		particleVolumetricLighting.outputs.push_back({ "Emissive MRT", false, false, colourFormats() });
		registry.registerScenePassFactory("MPP.ParticleVolumetricLighting", [] { return std::make_unique<ParticleVolumetricLightingPass>(); }, particleVolumetricLighting);

		auto particleDistortion = metadata("Particle Distortion", "Scene", GraphPassType::Scene);
		particleDistortion.inputs.push_back({ "Scene Depth", "DEPTH", false, depthFormats(), "HardParticles" });
		particleDistortion.outputs.push_back({ "Distortion Vectors", false, true, { GraphImageFormat::Rg16f, GraphImageFormat::Rg32f } });
		registry.registerScenePassFactory("MPP.ParticleDistortion", [] { return std::make_unique<ParticleDistortionPass>(); }, particleDistortion);

		auto particleDistortionComposite = metadata("Particle Distortion Composite", "Post Effects", GraphPassType::Fullscreen);
		particleDistortionComposite.inputs.push_back({ "Scene", "SCENE", true, colourFormats(), {} });
		particleDistortionComposite.inputs.push_back({ "Distortion Vectors", "DISTORTION", true, { GraphImageFormat::Rg16f, GraphImageFormat::Rg32f }, {} });
		particleDistortionComposite.outputs.push_back({ "Distorted Scene", false, true, colourFormats() });
		registry.registerScenePassFactory("MPP.ParticleDistortionComposite", [] { return std::make_unique<ParticleDistortionCompositePass>(); }, particleDistortionComposite);

		auto particleOit = metadata("Particle Weighted OIT", "Scene", GraphPassType::Scene);
		particleOit.inputs.push_back({ "Scene Depth", "DEPTH", false, depthFormats(), "HardParticles" });
		particleOit.outputs.push_back({ "Weighted Accumulation", false, true, { GraphImageFormat::Rgba16f, GraphImageFormat::Rgba32f } });
		particleOit.outputs.push_back({ "Optical Depth", false, true, { GraphImageFormat::R16f, GraphImageFormat::R32f } });
		registry.registerScenePassFactory("MPP.ParticleWeightedOit", [] { return std::make_unique<ParticleWeightedOitPass>(); }, particleOit);

		auto particleOitResolve = metadata("Particle Weighted OIT Resolve", "Scene", GraphPassType::Fullscreen);
		particleOitResolve.inputs.push_back({ "Scene", "SCENE", true, colourFormats(), {} });
		particleOitResolve.inputs.push_back({ "Weighted Accumulation", "ACCUMULATION", true, { GraphImageFormat::Rgba16f, GraphImageFormat::Rgba32f }, {} });
		particleOitResolve.inputs.push_back({ "Optical Depth", "OPTICAL_DEPTH", true, { GraphImageFormat::R16f, GraphImageFormat::R32f }, {} });
		particleOitResolve.inputs.push_back({ "Emissive MRT", "BLOOM", false, colourFormats(), {} });
		particleOitResolve.outputs.push_back({ "Composited Scene", false, true, colourFormats() });
		particleOitResolve.outputs.push_back({ "Composited Emissive MRT", false, false, colourFormats() });
		registry.registerScenePassFactory("MPP.ParticleWeightedOitResolve", [] { return std::make_unique<ParticleWeightedOitResolvePass>(); }, particleOitResolve);

		auto customFullscreen = metadata("Custom Fullscreen", "Custom", GraphPassType::Fullscreen);
		customFullscreen.acceptsProgram = true;
		customFullscreen.allowAdditionalInputs = true;
		customFullscreen.allowAdditionalOutputs = true;
		customFullscreen.allowAdditionalParameters = true;
		registry.registerMetadata("MPP.CustomFullscreen", customFullscreen);

		auto customMaterialRaster = metadata("Custom Material Raster", "Custom", GraphPassType::Scene);
		customMaterialRaster.allowAdditionalInputs = true;
		customMaterialRaster.allowAdditionalOutputs = true;
		customMaterialRaster.allowAdditionalParameters = true;
		customMaterialRaster.materialSlots.push_back("SceneMaterials");
		registry.registerScenePassFactory("MPP.CustomMaterialRaster", [] { return std::make_unique<ScenePass>(); }, customMaterialRaster);
	}
}
