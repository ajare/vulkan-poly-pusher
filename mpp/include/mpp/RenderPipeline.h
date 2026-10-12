#pragma once

#include <vector>
#include <memory>
#include <string>
#include <map>
#include <limits>

#pragma warning(push)
#pragma warning(disable : 4201)
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <glm/mat4x4.hpp>
#pragma warning(pop)

#include "mpp/Config.h"
#include "mpp/AmbientOcclusion.h"
#include "mpp/RenderPass.h"
#include "mpp/Scene.h"
#include "mpp/UniformCollection.h"
#include "mpp/RenderGraphPassFactoryRegistry.h"
#include "mpp/RenderGraphExecutor.h"
#include "mpp/RenderPipelineOutput.h"
#include "mpp/RenderPipelineFlow.h"
#include "mpp/RenderOutputProcessor.h"

namespace mpp
{
	class RenderSystem;
	class Program;

	struct _MPPAPI SceneOutputValidationCacheStats
	{
		uint64_t hits{ 0 };
		uint64_t misses{ 0 };
		uint64_t modelHits{ 0 };
		uint64_t modelMisses{ 0 };
		size_t uniquePrograms{ 0 };
	};

	struct _MPPAPI PbrEnvironment
	{
		ResourcePtr irradianceMap;
		ResourcePtr prefilteredSpecularMap;
		ResourcePtr brdfIntegrationLut;
		ResourcePtr backgroundMap;
		ResourcePtr environmentMap;
	};

	typedef std::shared_ptr<PbrEnvironment> PbrEnvironmentPtr;

	enum class RenderPipelineMode
	{
		LegacyForward,
		PbrForward,
		// Explicit opt-in graph paths. Default/PbrForward retain validated manual
		// target/presentation sequences until graph output is independently proven.
		GraphPbrForward,
		GraphLegacyForward,
		XmlGraphPbrForward
	};

	enum class PbrToneMapOperator
	{
		Reinhard,
		Aces
	};

	// A pipeline-owned image-space effect. It operates on the completed scene
	// target, so PBR and legacy materials need no material-level bloom binding.
	struct _MPPAPI BloomOptions
	{
		bool enabled{ false };
		// GraphPbrForward may use an authored emissive mask in scene colour[1].
		// It falls back to threshold extract when MRT is unavailable.
		bool useMrtEmissiveMask{ false };
		float threshold{ 1.0f };
		float intensity{ 0.15f };
		uint32_t blurPasses{ 2 };
	};

	enum class ShadowLightType
	{
		Directional,
		Point
	};

	// Shadow lights deliberately do not reuse PbrLight or the legacy light
	// API: a named shadow domain can be shared by arbitrary forward pipelines.
	struct _MPPAPI ShadowLight
	{
		ShadowLightType type{ ShadowLightType::Directional };
		glm::vec3 direction{ 0.0f, -1.0f, 0.0f };
		glm::vec3 focusPoint{ 0.0f };
		glm::vec3 position{ 0.0f };
		float range{ 192.0f };
		// Index in the receiving pipeline's light array. Visibility is applied
		// only to this direct-light contribution.
		uint32_t lightIndex{ 0 };
	};

	enum class ShadowFilterMode
	{
		Hard,
		Pcf3x3
	};

	// Optional World-space filtering for a point-shadow caster pass. Geometry in
	// the negative half-space is discarded. Geometry on the plane remains a
	// caster except inside the declared rectangular opening. This is generic
	// folded-view state: applications may use it for portals, mirrors, or any
	// other pass whose point-shadow visibility crosses a bounded plane opening.
	struct _MPPAPI PointShadowCasterClip
	{
		bool enabled{ false };
		// dot(xyz, worldPosition) + w >= 0 is retained.
		glm::vec4 retainedWorldPlane{ 0.0f, 1.0f, 0.0f, 0.0f };
		glm::vec3 openingCentre{ 0.0f };
		glm::vec3 openingTangent{ 1.0f, 0.0f, 0.0f };
		glm::vec3 openingBitangent{ 0.0f, 1.0f, 0.0f };
		glm::vec2 openingHalfSize{ 0.0f };
		float planeTolerance{ 0.01f };
	};

	struct _MPPAPI ShadowOptions
	{
		bool enabled{ false };
		ShadowLight light;
		size_t resolution{ 2048 };
		float orthoHalfWidth{ 450.0f };
		float nearPlane{ 0.25f };
		float farPlane{ 1800.0f };
		float constantBias{ 0.0008f };
		float normalBias{ 0.0025f };
		float filterRadiusTexels{ 1.0f };
		ShadowFilterMode filterMode{ ShadowFilterMode::Pcf3x3 };
		// Fraction of the point-light range where visibility starts fading to
		// fully unshadowed. Directional domains ignore this value.
		float fadeStartNormalized{ 0.9f };
		PointShadowCasterClip pointCasterClip;
	};

	enum class ShadowInvalidationReason
	{
		None,
		InitialConfiguration,
		LightChanged,
		OptionsChanged,
		SceneChanged,
		Explicit
	};

	struct _MPPAPI ShadowDomainDiagnostics
	{
		uint64_t revision{ 0 };
		uint64_t renderedRevision{ 0 };
		uint64_t renderedFrame{ 0 };
		uint64_t reuseCount{ 0 };
		uint64_t regenerationCount{ 0 };
		uint64_t facePassCount{ 0 };
		size_t selectedModelCount{ 0 };
		bool reusedLastRequest{ false };
		bool cacheComplete{ false };
		ShadowInvalidationReason invalidationReason{ ShadowInvalidationReason::None };
	};

	struct _MPPAPI GraphPassDebugOptions
	{
		bool shadow{ true };
		bool scene{ true };
		bool ambientOcclusion{ true };
		bool bloom{ true };
		bool particles{ true };
		bool presentation{ true };
	};

	// A client-declared extra scene-pass colour output, appended after the
	// built-in colour/bloom-mask/normals MRT slots at whichever location comes
	// next. `name` must match a fragment shader `@Out(... NAME)` declaration;
	// nothing in mpp reads the resulting image today -- it exists purely so a
	// host material can publish a per-pixel value for a later, host-authored
	// pass to consume.
	struct _MPPAPI RenderPipelineSceneExtraOutput
	{
		std::string name;
		GraphImageFormat format{ GraphImageFormat::R8 };
	};

	enum class WaterReflectionTechnique
	{
		ScreenSpace,
		Planar
	};

	enum class PlanarReflectionResolution
	{
		Full,
		Half,
		Quarter
	};

	enum class ReflectionPlaneSide
	{
		Above,
		Below
	};

	// A generic horizontal reflection plane supplied by the host. MPP does not
	// know what authored or generated feature selected it. Optional matching
	// bounds identify which horizontal surfaces consume this plane's image;
	// omitted bounds retain the one-plane 0.01-unit matching contract.
	struct _MPPAPI PlanarReflectionPlaneDescriptor
	{
		float elevation{ 0.0f };
		ReflectionPlaneSide viewerSide{ ReflectionPlaneSide::Above };
		float minimumMatchingElevation{ std::numeric_limits<float>::quiet_NaN() };
		float maximumMatchingElevation{ std::numeric_limits<float>::quiet_NaN() };
	};

	// The complete virtual-camera state used by one horizontal Planar pass.
	// Exposed as a pure seam so callers and GPU-independent tests can verify the
	// reflection and oblique-clipping contract without reproducing its maths.
	struct _MPPAPI PlanarReflectionView
	{
		glm::vec3 position;
		glm::vec3 direction;
		glm::vec3 up;
		glm::mat4 view{ 1.0f };
		glm::mat4 projection{ 1.0f };
	};

	// The renderer-wide seam allowance for oblique world-plane clipping. The
	// retained half-space is expanded 0.05 world units into the rejected side so
	// tiny raster/projection disagreements cannot open a visible border seam.
	inline constexpr float AuxiliaryViewClipSeamBias = 0.05f;

	struct _MPPAPI VirtualCameraTransforms
	{
		glm::mat4 view{ 1.0f };
		glm::mat4 projection{ 1.0f };
	};

	// `worldClipPlane` uses dot(normal, worldPosition) + w >= 0 for the retained
	// side. Its normal is normalized before the world-unit seam bias is applied,
	// so horizontal and arbitrary vertical planes share exactly the same maths.
	_MPPAPI VirtualCameraTransforms buildObliquelyClippedVirtualCamera(
		glm::mat4 const& view, glm::mat4 const& projection,
		glm::vec4 const& worldClipPlane,
		float seamBias = AuxiliaryViewClipSeamBias);

	_MPPAPI PlanarReflectionView buildPlanarReflectionView(
		Camera& camera, PlanarReflectionPlaneDescriptor const& plane,
		float aspectRatio);

	// Application values attached only while one scene colour pass executes.
	// Sampler names are resolved by the receiving material program; a depth-only
	// RenderTexture binds its depth image, including a comparison cubemap.
	struct _MPPAPI ScenePassOverrides
	{
		UniformCollection uniforms;
		std::map<std::string, ResourcePtr> samplers;
	};

	// One application-described virtual-camera render. `view` and `projection`
	// are consumed exactly before oblique clipping modifies only the projection's
	// near plane. `slot` is a deterministic application key and output name stem.
	struct _MPPAPI AuxiliarySceneView
	{
		std::string slot{ "Auxiliary0" };
		glm::mat4 view{ 1.0f };
		glm::mat4 projection{ 1.0f };
		glm::vec4 worldClipPlane{ 0.0f, 1.0f, 0.0f, 0.0f };
		uint32_t width{ 1 };
		uint32_t height{ 1 };
		float nearDistance{ 0.1f };
		float farDistance{ 1000.0f };
		float seamBias{ AuxiliaryViewClipSeamBias };
		// Reflection matrices reverse handedness; rigid portal/camera transforms do not.
		bool reverseWinding{ false };
		// Application uniforms attached only while this auxiliary scene pass is
		// executing. They override material/model values and are restored together
		// with the renderer's other pass-scoped state, including on failure.
		// MPP_VIRTUAL_CAMERA is reserved and supplied by the renderer.
		UniformCollection uniformOverrides;
		// Generic pass-owned resources, merged after pipeline environment maps.
		// They are restored with the uniforms, target, shadow domain, and raster
		// state on both successful and failed auxiliary execution.
		std::map<std::string, ResourcePtr> samplerOverrides;
	};

	struct _MPPAPI AuxiliarySceneOutputs
	{
		// Colour and depth are two inspectable attachments on the same target.
		RenderTargetPtr colour;
		RenderTargetPtr depth;
		std::string colourName;
		std::string depthName;
	};

	struct _MPPAPI AuxiliarySceneDiagnostics
	{
		std::string passName;
		std::string colourOutputName;
		std::string depthOutputName;
		uint32_t width{ 0 };
		uint32_t height{ 0 };
		bool succeeded{ false };
		std::string failureReason;
	};

	struct _MPPAPI WaterReflectionOptions
	{
		static constexpr size_t MaxPlanarPlanes = 4;
		// False keeps WaterScene compositing active when a requested reflection
		// source has failed, but binds no reflection topology or radiance.
		bool enabled{ true };
		WaterReflectionTechnique technique{ WaterReflectionTechnique::ScreenSpace };
		PlanarReflectionResolution planarResolution{ PlanarReflectionResolution::Half };
		std::vector<PlanarReflectionPlaneDescriptor> planarPlanes;
	};

	struct _MPPAPI RenderPipelineOptions
	{
		RenderPipelineMode mode{ RenderPipelineMode::LegacyForward };
		float exposure{ 1.0f };
		PbrToneMapOperator toneMapOperator{ PbrToneMapOperator::Aces };
		PbrEnvironmentPtr environment;
		// Optional immutable XML graph topology for XmlGraphPbrForward.
		ResourcePtr graphTemplate;
		// Host-owned graph imports override built-in screen/shadow registrations.
		std::map<std::string, RenderTargetPtr> graphImports;
		// Named logical outputs share the XML pipeline descriptor format.
		std::vector<RenderPipelineOutput> outputs;
		// Optional emissive-mask MRT variant. XmlGraphPbrForward selects it only
		// when requested and all hardware/material output requirements validate.
		ResourcePtr graphTemplateMrt;
		BloomOptions bloom;
		AmbientOcclusionOptions ambientOcclusion;
		// Client-declared extra scene-pass colour outputs. Empty (the default)
		// changes nothing about the generated graph or its attachment budget.
		std::vector<RenderPipelineSceneExtraOutput> sceneExtraOutputs;
		// Adds generated Water topology. Authored graph templates keep their own
		// topology. The reflection source is selected explicitly below rather than
		// being implied by this graph opt-in. Default-off for compatibility.
		bool generatedWater{ false };
		WaterReflectionOptions waterReflections;
		// Adds a generated MPP.ParticleScene pass after the opaque/water scene and
		// before bloom, so particles bloom like any other emissive. Authored graph
		// templates place their own particle passes instead. Default-off: a
		// pipeline that never asked for particles must not grow a pass.
		bool generatedParticles{ false };
		// Draw opaque scene geometry with depth-only programs before material
		// shading. The material pass then uses LessEqual against that depth.
		bool depthPrepass{ true };
		GraphPassDebugOptions graphPasses;
		bool debugEnvironmentCube{ false };
		// Empty means this pipeline is not a shadow-domain participant.
		std::string shadowDomain;
		// Root resource name (e.g. a PbrPipelineRuntime generation's declared root)
		// that MPP.FullscreenEffect passes prefix a bare programResource with when
		// resolving it, before falling back to a plain global lookup. Lets a
		// PbrPipelineDocument-authored pass reference one of its own LocalResources
		// by its authored name, even though that resource is only ever registered
		// under a dynamically-generated root the document can't predict itself.
		// Empty for pipelines with no document-local post-effect materials (e.g.
		// DemoSuite's XmlGraphPBR, which declares its materials as global names).
		std::string resourceRoot;
	};

	class _MPPAPI RenderPipeline
	{
		std::string mName;

		RenderSystem* mRenderSystem;

		RenderPipelineOptions mOptions;

		std::vector<RenderPassPtr> mPasses;

		RenderTargetPtr mBloomExtractTarget;
		RenderTargetPtr mBloomPingTarget;
		RenderTargetPtr mBloomPongTarget;
		RenderTargetPtr mBloomCompositeTarget;

		std::unique_ptr<class RenderGraphTargets> mGraphTargets;
		std::unique_ptr<RenderOutputProcessor> mOutputProcessor;
		std::unique_ptr<class RenderGraphExecutor> mGraphExecutor;
		RenderGraphPassFactoryRegistry mGraphPassFactories;
		// Published only after graph execution and output processing complete.
		// Replaced every successful frame so allocation changes cannot leave a
		// declared output pointing at an earlier graph target.
		std::map<std::string, RenderTargetPtr> mNamedOutputTargets;
		struct AuxiliarySlot
		{
			RenderTargetPtr target;
			AuxiliarySceneDiagnostics diagnostics;
			bool completed{ false };
		};
		std::map<std::string, AuxiliarySlot> mAuxiliarySlots;
		bool mWarnedMissingPbrEnvironment{ false };
		uint32_t mTaaSequenceIndex{ 0 };
		bool mTaaCameraValid{ false };
		uint64_t mLastTaaFrameSerial{ 0 }, mLastCameraCutRevision{ 0 };
		glm::vec3 mLastCameraPosition{ 0.0f }, mLastCameraDirection{ 0.0f, 0.0f, -1.0f };
		float mLastCameraFov{ 0.0f }, mLastCameraAspect{ 0.0f }, mLastCameraNear{ 0.0f }, mLastCameraFar{ 0.0f };
		// Keyed by graph pass name. setPostEffectEnabled/setPostEffectParameter each
		// update this pass's entry and push the merged result, since
		// RenderGraphExecutor::setPassParameterOverrides replaces a pass's entire
		// effective parameter set rather than merging into it -- accumulating here
		// is what lets ENABLED and a numeric parameter be set independently
		// without one call clobbering the other.
		std::map<std::string, UniformCollection> mPostEffectParameterOverrides;
		bool mFlowTelemetryEnabled{ false };
		bool mGraphImageCaptureRequested{ false };
		bool mPlanarReflectionRuntimeFailed{ false };
		std::string mPlanarReflectionFailureMessage;
		uint64_t mFlowGeneration{ 0 };
		RenderPipelineFlowSnapshotPtr mLastFlowSnapshot;
		std::shared_ptr<RenderPipelineFlowSnapshot> mPendingFlowSnapshot;

		struct ProgramOutputKey
		{
			std::shared_ptr<Program> program;
			uint64_t reflectionRevision{ 0 };
			bool operator ==(ProgramOutputKey const&) const = default;
		};
		struct SceneModelProgramCache
		{
			class Model const* model{ nullptr };
			uint64_t modelMaterialRevision{ 0 };
			uint64_t parameterRevision{ 0 };
			std::vector<std::pair<ResourcePtr, uint64_t>> materials;
			std::vector<std::shared_ptr<Program>> programs;
		};
		std::map<SceneModel3d const*, SceneModelProgramCache> mSceneModelProgramCache;
		std::vector<ProgramOutputKey> mOutputValidationPrograms;
		size_t mOutputValidationRequiredCount{ 0 };
		bool mOutputValidationKnown{ false };
		bool mOutputValidationResult{ false };
		uint64_t mOutputValidationHits{ 0 };
		uint64_t mOutputValidationMisses{ 0 };
		uint64_t mOutputValidationModelHits{ 0 };
		uint64_t mOutputValidationModelMisses{ 0 };

		bool sceneProgramsSupportOutputs(std::vector<SceneModel3dPtr> const& models, size_t requiredCount);
		void ensureBloomTargets(size_t width, size_t height);
		void renderGraphForward(ScenePtr scene, CameraPtr camera, std::vector<SceneModel3dPtr> const& models, bool pbr);
		void beginFlowSnapshot() noexcept;
		void publishFlowSnapshot() noexcept;
		void discardFlowSnapshot() noexcept;

	public:

		RenderPipeline(std::string const& name, RenderSystem* renderSystem, RenderPipelineOptions const& options = {});

		virtual ~RenderPipeline();

		std::string const& getName() const;

		RenderPipelineOptions const& getOptions() const;

		void setExposure(float exposure);

		void setToneMapOperator(PbrToneMapOperator toneMapOperator);

		void setBloomOptions(BloomOptions const& bloomOptions);

		void setAmbientOcclusionOptions(AmbientOcclusionOptions const& ambientOcclusionOptions);

		// Generic post-effect-chain tuning: enable/disable or set a named uniform
		// on the MPP.FullscreenEffect graph pass identified by `passName`. Replaces
		// BloomOptions-style typed options for chain-authored effects -- new
		// effects need no new C++ here, just a pass name and parameter name.
		void setPostEffectEnabled(std::string const& passName, bool enabled);
		void setPostEffectParameter(std::string const& passName, std::string const& parameterName, float value);

		void setGraphPassDebugOptions(GraphPassDebugOptions const& graphPasses);
		void setDebugEnvironmentCube(bool enabled);

		void setPbrEnvironment(PbrEnvironmentPtr environment);

		void setShadowDomain(std::string const& shadowDomain);

		RenderTargetPtr getOutputRenderTarget();
		// Returns the current completed target for a declared pipeline output.
		// Unknown names and outputs unavailable before successful graph execution
		// are errors rather than null or positional graph-image fallbacks.
		RenderTargetPtr getOutputRenderTarget(std::string const& outputName) const;
		RenderTargetPtr getGraphImageRenderTarget(GraphImageHandle image) const;
		AuxiliarySceneOutputs renderAuxiliaryScene(ScenePtr scene, CameraPtr hostCamera,
			AuxiliarySceneView const& view);
		AuxiliarySceneOutputs getAuxiliarySceneOutputs(std::string const& slot) const;
		AuxiliarySceneDiagnostics const& getAuxiliarySceneDiagnostics(std::string const& slot) const;
		void requestGraphImageCapture();
		std::vector<GraphImageCapture> takeGraphImageCaptures();
		std::vector<GraphPassExecutionStats> const& getLastGraphExecutionStats() const;
		std::vector<GraphPassHandle> const& getLastGraphExecutionOrder() const;
		// Failure is sticky for this pipeline. The selected technique remains
		// Planar; only reflected radiance is disabled while WaterScene continues.
		bool planarReflectionRuntimeFailed() const;
		std::string const& getPlanarReflectionFailureMessage() const;
		void disablePlanarReflectionsAfterFailure(std::string const& reason);
		void setFlowTelemetryEnabled(bool enabled);
		bool isFlowTelemetryEnabled() const;
		RenderPipelineFlowSnapshotPtr getLastFlowSnapshot() const;
		uint64_t getOutputGeneration() const;
		std::vector<RenderPipelineOutputPlan> const& getOutputPlans() const;
		SceneOutputValidationCacheStats getSceneOutputValidationCacheStats() const;
		void prepareOutputs(RenderGraph const& graph, std::map<std::string, RenderTargetPtr> const& destinations);

		void resize(size_t width, size_t height);

		void addRenderPass(RenderPassPtr pass);

		virtual void render(ScenePtr scene, CameraPtr camera, glm::vec2 const& offset2d,
			ScenePassOverrides const& overrides = {});

	};

	typedef std::shared_ptr<RenderPipeline> RenderPipelinePtr;
}