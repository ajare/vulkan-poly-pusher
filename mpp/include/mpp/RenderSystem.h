#pragma once

#include <cstdlib>
#include <map>
#include <deque>
#include <queue>
#include <stack>
#include <memory>

#pragma warning(push)
#pragma warning(disable : 4201)
#include <glm/gtc/matrix_transform.hpp>
#pragma warning(pop)

#include "mpp/Config.h"
#include "mpp/Caps.h"
#include "mpp/AntiAliasing.h"
#include "mpp/ResourceWrangler.h"
#include "mpp/Pool.h"
#include "mpp/Resource.h"
#include "mpp/RenderPipeline.h"
#include "mpp/RenderTarget.h"
#include "mpp/RenderTexture.h"
#include "mpp/GpuDebugScope.h"
#include "mpp/IblEnvironmentCache.h"
#include "mpp/ClipRectangle.h"
#include "mpp/BlendMode.h"
#include "mpp/RenderInfo.h"
#include "mpp/Colour.h"
#include "mpp/Model.h"
#include "mpp/Font.h"
#include "mpp/Texture.h"
#include "mpp/TextureTile.h"
#include "mpp/QuadBatch.h"
#include "mpp/LineBatch.h"
#include "mpp/ModelRenderParams.h"
#include "mpp/ModelInstance.h"
#include "mpp/UniformCollection.h"
#include "mpp/UniformBuffer.h"
#include "mpp/Vertex2d.h"
#include "mpp/Logger.h"
#include "mpp/PbrLight.h"
#include "mpp/SceneFactory.h"

namespace mpp
{
	class Program;
	class Camera;
	class ParticleSystem; // Forward-declared so as to not pollute client apps.
	class TrailSystem;
	enum class ParticleBlendClass : uint32_t;
	class RenderTexture;
	class Profiler; // Forward-declared so as to not pollute client apps.
	class ResourceManager;

	class _MPPAPI RenderSystem : public ResourceWrangler
	{
		friend class ParticleSystem;
		friend class TrailSystem;
		enum class ProjectionType
		{
			Unknown,
			Ortho2D,
			Perspective3D
		};

	public:

		enum class TimeUnit
		{
			Seconds,
			Milliseconds,
			Microseconds,
			Nanoseconds
		};

		enum class SizeUnit
		{
			Bytes,
			Kilobytes,
			Megabytes,
			Gigabytes
		};

	public:

		enum class TextureTiling
		{
			Stretch,
			Tile
		};

		enum class TextureDiagnosticMode
		{
			Colour,
			Red,
			Green,
			Blue,
			Alpha,
			Luminance,
			Depth,
			HdrToneMap,
			HdrHeatMap
		};

		struct TextureDiagnosticOptions
		{
			TextureDiagnosticMode mode{ TextureDiagnosticMode::Colour };
			uint32_t mipLevel{ 0 };
			float exposure{ 1.0f };
			float depthNear{ 0.1f };
			float depthFar{ 100.0f };
		};

	private:

		struct SortedRenderCommand
		{
			uint64_t key;
			float viewDistanceSquared;
			VertexBufferRenderCommand cmd;
			MeshInstance* meshInstance;
		};

	private:

		typedef std::pair<uint64_t, MeshInstance*> SortableMeshInstance;

		const uint32_t MaxTextGlyphs = 4096;
		
	private:

		Logger* mLogger;

		RenderSystemOptions mOptions;

		size_t mWindowWidth, mWindowHeight;

		int mViewportX{ 0 }, mViewportY{ 0 };
		size_t mViewportWidth, mViewportHeight;

		ResourceManager* mResourceMgr;

		Caps mCaps;
		IblEnvironmentCache mIblEnvironmentCache;

		//
		// Rendering
		//
		std::map<std::string, RenderPipelinePtr> mPipelines;

		RenderTargetPtr mRenderTarget;

		std::stack<RenderTargetPtr> mRenderTargetStack;
		bool mCubemapFaceRenderActive{ false };

		std::stack<ClipRectangle> mClipStack;

		RenderTargetPtr mScreen;
		uint64_t mFrameSerial{ 0 };
		uint64_t mParticleSimulationFrameSerial{ 0 };
		bool mParticleSimulationFrameValid{ false };
		uint64_t mTrailSimulationFrameSerial{ 0 };
		bool mTrailSimulationFrameValid{ false };

		// Set only while a PBR pipeline is flushing its scene pass. Environment
		// samplers then override per-material placeholder bindings.
		PbrEnvironmentPtr mActivePbrEnvironment;

		// Pipeline-owned samplers (environment now; shadow maps later) override
		// material bindings by shader sampler name during a scene flush.
		std::map<std::string, ResourcePtr> mActivePipelineSamplerOverrides;

		// Pass-scoped values applied after material and instance uniforms. Virtual
		// camera passes use this without mutating host-owned scene-model state.
		UniformCollection mActivePipelineUniformOverrides;

		struct ShadowDomainState
		{
			ShadowOptions options;
			RenderTargetPtr depthTarget;
			std::shared_ptr<UniformBuffer> frameBuffer;
			bool fallbackWarningIssued{ false };
			bool cacheDirty{ true };
			bool regenerationStarted{ false };
			uint8_t renderedFaces{ 0 };
			std::vector<uint64_t> casterState;
			ShadowInvalidationReason pendingReason{ ShadowInvalidationReason::InitialConfiguration };
			ShadowDomainDiagnostics diagnostics;
		};
		std::map<std::string, ShadowDomainState> mShadowDomains;
		RenderTargetPtr mActiveShadowDepthTarget;
		std::shared_ptr<UniformBuffer> mShadowDisabledFrameBuffer;

		ProjectionType mProjectionType;

		// List of models to render
		Pool<ModelInstance>* mModelInstances;

		Pool<MeshInstance>* mMeshInstances;

		// Optional diagnostic path for measuring front-to-back rendering. The
		// default keeps the established program/transparent ordering.
		bool mSortGeometryFrontToBack{ false };

		float mGamma;

		//
		// Core resources
		//

		// Default material: 2d with PTC and no texture
		ResourcePtr mDefaultMaterial, mInternalMaterial;

		// Default programs
		ResourcePtr mDefaultProgram2d, mDefaultProgram3d;

		ResourcePtr mActiveProgram;
		size_t mExpectedGraphColourOutputs{ 0 };

		// Scope for the renderer's own single-output core programs (the fullscreen
		// quad and the debug quad) when they draw inside a multi-output graph pass.
		// GL leaves every attachment a fragment shader does not write undefined for
		// the fragments it covers, so an unguarded quad would corrupt a loaded
		// attachment such as SceneEmissive -- and setUsedProgram would rightly reject
		// the program for having no output at location one. Masking writes to the
		// attachments beyond the first states the intent instead: these helpers only
		// ever shade colour attachment zero.
		class PrimaryColourOutputDraw
		{
			RenderSystem* mRenderSystem{ nullptr };
			GraphRasterState mSavedState;
			size_t mColourOutputs{ 0 };

		public:
			explicit PrimaryColourOutputDraw(RenderSystem* renderSystem);
			~PrimaryColourOutputDraw();

			PrimaryColourOutputDraw(PrimaryColourOutputDraw const&) = delete;
			PrimaryColourOutputDraw& operator =(PrimaryColourOutputDraw const&) = delete;
		};

		// Render-thread-only, non-owning process-flow recorder state. A pipeline
		// owns the mutable candidate until endRenderFlowCapture() succeeds.
		RenderPipelineFlowSnapshot* mFlowCapture{ nullptr };
		GraphPassHandle mCurrentFlowPass;
		uint64_t mFlowSequence{ 0 };
		bool mFlowCaptureFailed{ false };
		size_t mFlowBatchHighWater{ 0 };
		size_t mFlowEventHighWater{ 0 };

		// Internal programs
		ResourcePtr mInternalProgram2d;
		ResourcePtr mShadowDepthProgram;
		ResourcePtr mAlphaShadowDepthProgram;
		ResourcePtr mPointShadowDepthProgram;
		ResourcePtr mPointAlphaShadowDepthProgram;

		// Internal textures
		ResourcePtr mNoTexture;

		ResourcePtr mInternalFontTexture;

		// Fullscreen effects
		ResourcePtr mFullscreenQuad, mFullscreenProgram, mToneMapProgram, mTextureDiagnosticProgram;
		ResourcePtr mPbrBrdfIntegrationLut;
		ResourcePtr mBloomExtractProgram, mBloomBlurProgram, mBloomCombineProgram, mParticleWeightedOitResolveProgram, mParticleDistortionCompositeProgram, mSsaoRawProgram, mGtaoRawProgram, mSsaoBlurProgram, mSsaoCombineProgram, mSsaoCombineModulatedProgram, mEnvironmentDebugCubeProgram, mSsaaLanczosProgram, mTaaProgram, mFxaaProgram, mEquirectangularToCubemapProgram, mDiffuseIrradianceProgram, mPrefilteredSpecularProgram, mPbrBrdfIntegrationProgram;

		// Text rendering
		ResourcePtr mTextMesh, mColouredTextMesh;
		bool mTextAsPoints{ false };

		// Lookup for ease
		std::vector<ResourcePtr> mCoreResources;

		//
		// Internal font
		//
		Font* mInternalFont;

		std::shared_ptr<UniformCollection> mTextUniforms;

		std::shared_ptr<ModelRenderParams> mTextParams;

		//
		// Internal buffer objects for 2d rendering
		//
		uint32_t mInternalVBO, mInternalIBO;

		//
		// 3d Transforms
		//
#pragma warning(push)
#pragma warning(disable: 4324)
		alignas(16) glm::mat4 m3dModelCameraProjectionMatrix;
		alignas(16) glm::mat4 m3dCameraMatrix;
		alignas(16) glm::mat4 m3dProjectionMatrix;
		alignas(16) glm::mat4 m3dModelMatrix;

		alignas(16) std::stack<glm::mat4> m3dCameraMatrixStack;
		alignas(16) std::stack<glm::mat4> m3dProjectionMatrixStack;
		alignas(16) std::stack<glm::mat4> m3dModelMatrixStack;
#pragma warning(pop)

		float mFarPlaneDistance;

#ifdef MPP_PROFILE_BUILD
		Profiler* mProfiler;

		LineBatch* mProfileLines;

		int mSamplesToRecord;

		struct TimeSample
		{
			int frameTime,
				driverWaitsGPU,
				driverWaitsKernel,
				driverWaitsLock,
				driverWaitsRender,
				driverWaitsSwap;
		};

		std::deque<TimeSample> mProfileTimeSamples;
#endif
		
		//
		// Debug and info
		//
		std::vector<std::string> mDebugPreMessages;
		
		std::vector<std::string> mDebugPostMessages;

		bool mShowDebugPanel;

		TimeUnit mTimeUnit;

		SizeUnit mSizeUnit;
		
		RenderInfo mRenderInfo;

		GraphRasterState mRasterStateCache;
		bool mRasterStateCacheKnown{ false };
		bool mPolygonOffsetFill{ false };
		float mPolygonOffsetFactor{ 0.0f };
		float mPolygonOffsetUnits{ 0.0f };
		bool mProgramPointSize{ false };
		bool mPointSprite{ false };
		bool mCubeMapSeamless{ false };
		uint64_t mStateChangesApplied{ 0 };
		uint64_t mStateChangesSkipped{ 0 };
		uint64_t mStateVerificationCounter{ 0 };

		//
		// Built-in lights
		//
		UniformBuffer* mLightsBuffer{ nullptr };
		UniformBuffer* mPbrLightsBuffer{ nullptr };
		std::vector<PbrLight> mPbrLights;
		static constexpr size_t MaxPbrLights{ 8 };

		//
		// Per-frame camera state for scene passes that need to work in view space
		// (currently water SSR). Fullscreen passes hand-wire the equivalent
		// uniforms; a scene pass shades through material programs and cannot.
		//
		UniformBuffer* mCameraFrameBuffer{ nullptr };
		glm::mat4 mCameraFrameView{ 1.0f };
		glm::mat4 mCameraFrameProjection{ 1.0f };

		//
		// GPU particles. Created with the core resources; its GPU resources are
		// built on first use, so a pipeline whose graph has no MPP.ParticleScene
		// pass never pays for compute programs or shader storage buffers.
		//
		std::unique_ptr<ParticleSystem> mParticleSystem;
		// Trails are a distinct primitive with their own point histories, GPU
		// buffers, update kernel and ribbon draw path.
		std::unique_ptr<TrailSystem> mTrailSystem;

		//
		// Scenes
		//
		std::map<std::string, SceneFactory> mSceneFactories;

	private:

		void checkExtensions();

		void checkCaps();

		void renderDebugPanel();
		
		// Methods to be used by Resources
		friend class Program;
		friend class Texture;
		friend class Material;
		friend class Model;
		friend class RenderGraphTargets;
		friend class RenderOutputProcessor;
		friend class RenderPipeline;
		friend _MPPAPI bool runRenderGraphGpuTests(RenderSystem* renderSystem, std::string* failure);

		void useDefaultProgram();

		void setUsedProgram(ResourcePtr program);

		ResourcePtr getUsedProgram();
		
		void setDefaultTexture();

		int buildTextVertexBuffer(VertexBuffer* buffer, std::string const& text, int& offset, int x, int y);

		int buildColouredTextVertexBuffer(VertexBuffer* buffer, std::string const& text, int& offset, int x, int y);

		void createLightsData();

		void destroyLightsData();

		void createPbrLightsData();

		void destroyPbrLightsData();

		void uploadPbrLights();

		void createCameraFrameData();

		void destroyCameraFrameData();

		void destroyShadowDomains();

		void createShadowDomainResources(std::string const& name, ShadowDomainState& domain);

		std::vector<uint64_t> captureShadowCasterState(std::vector<SceneModel3dPtr> const& models) const;

		void markShadowDomainDirty(ShadowDomainState& domain, ShadowInvalidationReason reason);

		void createShadowDisabledFrameBuffer();

		void addCoreResource(ResourcePtr resource, bool load);

		void setupRenderMeshInstance(MeshInstance* meshInstance, VertexBufferRenderCommand const& renderCmd, uint64_t sortKey, uint64_t* currentProgramKey, std::vector<uint64_t>* currentTextureKeys, Material** currentMaterial, std::vector<std::string>* flowStateChanges);

		void teardownRenderMeshInstance(MeshInstance* meshInstance);

		void setDepthTestState(bool enabled, bool force = false);
		void setDepthWriteState(bool enabled, bool force = false);
		void setDepthCompareState(GraphCompareOp compare, bool force = false);
		void setCullState(GraphCullMode mode, bool force = false);
		void setFrontFaceState(GraphFrontFace face, bool force = false);
		void setFillModeState(GraphFillMode mode, bool force = false);
		void setBlendState(bool enabled, bool force = false);
		void setBlendEquationState(GraphBlendOp colour, GraphBlendOp alpha, bool force = false);
		void setBlendFunctionState(GraphBlendFactor sourceColour, GraphBlendFactor destinationColour, GraphBlendFactor sourceAlpha, GraphBlendFactor destinationAlpha, bool force = false);
		void setMultisampleState(bool enabled, bool force = false);
		void setAlphaToCoverageState(bool enabled, bool force = false);
		void setScissorState(bool enabled, bool force = false);
		void setScissorRectangleState(glm::uvec4 const& rectangle, bool force = false);
		void setColourMaskState(size_t output, GraphColourWriteMask const& mask, bool force = false);
		void setAllColourMasksState(GraphColourWriteMask const& mask, bool force = false);
		void setPolygonOffsetFillState(bool enabled);
		void setPolygonOffsetState(float factor, float units);
		void setProgramPointSizeState(bool enabled);
		void setPointSpriteState(bool enabled);
		void setCubeMapSeamlessState(bool enabled);

		RenderTargetPtr createPhysicalRenderTexture(std::string const& name, size_t width, size_t height, RenderTextureOptions const& options, uint32_t samples);
		void validateEquirectangularConversionSource(Texture const* source, std::string const& generatedName, uint32_t faceSize, uint32_t mipLevels) const;
		void renderEquirectangularCubemapFace(Texture* source, RenderTargetPtr const& destination, uint32_t face, uint32_t mipLevel);
		void validateDiffuseIrradianceSource(Texture const* source, std::string const& generatedName, uint32_t faceSize, uint32_t sampleCount) const;
		void renderDiffuseIrradianceFace(Texture* source, RenderTargetPtr const& destination, uint32_t face, uint32_t sampleCount);
		// Not const: a single-mip source is reported through the renderer log.
		void validatePrefilteredSpecularSource(Texture const* source, std::string const& generatedName, uint32_t faceSize, uint32_t mipLevels, uint32_t sampleCount);
		void renderPrefilteredSpecularFace(Texture* source, RenderTargetPtr const& destination, uint32_t face, uint32_t mipLevel, float roughness, uint32_t sampleCount);
		void renderSsaaLanczos(RenderTexture* source, RenderTargetPtr const& destination, glm::vec2 const& direction);
		void renderTaa(RenderTexture* currentColour, RenderTexture* currentDepth, RenderTexture* historyColour, RenderTexture* historyDepth, RenderTargetPtr const& destination, glm::mat4 const& inverseCurrentViewProjection, glm::mat4 const& previousViewProjection);
		void renderFxaa(RenderTexture* source, RenderTargetPtr const& destination);

	public:

		class _MPPAPI CubemapFaceRenderScope
		{
			RenderSystem* mSystem{}; RenderTexture* mTarget{};
			std::unique_ptr<GpuDebugScope> mGpuScope;
			int mViewport[4]{}; int mScissor[4]{}; int mDrawBuffer{}; int mReadBuffer{}; bool mScissorEnabled{};
			bool mFinished{};
		public:
			CubemapFaceRenderScope(RenderSystem& system, RenderTargetPtr const& target, uint32_t face, uint32_t mipLevel);
			CubemapFaceRenderScope(CubemapFaceRenderScope const&) = delete;
			CubemapFaceRenderScope& operator=(CubemapFaceRenderScope const&) = delete;
			~CubemapFaceRenderScope();
			void finish();
		};

		RenderSystem(size_t windowWidth, size_t windowHeight, Logger* logger, RenderSystemOptions options = {});

		virtual ~RenderSystem();

		size_t getWindowWidth() const;

		size_t getWindowHeight() const;

		float getAspectRatio() const;

		Caps const& getCaps() const;
		RenderSystemOptions const& getOptions() const { return mOptions; }
		ResourceManager* getResourceManager() const { return mResourceMgr; }
		IblEnvironmentCache& getIblEnvironmentCache() { return mIblEnvironmentCache; }

		void initialise();

		void createCoreResources(ResourceManager* resourceMgr);

		void destroyCoreResources();

		void showDebugPanel(bool show, TimeUnit timeUnit = TimeUnit::Milliseconds, SizeUnit sizeUnit = SizeUnit::Megabytes);

		bool isDebugPanelShown() const;

		void setDebugPreMessages(std::vector<std::string> const& messages);

		void setDebugPostMessages(std::vector<std::string> const& messages);

		void setDefaultState();

		struct RasterStateCacheStats
		{
			uint64_t applied{ 0 };
			uint64_t skipped{ 0 };
		};
		GraphRasterState captureRasterState(size_t colourOutputs) const;
		void applyRasterState(GraphRasterState const& state, size_t colourOutputs, size_t width, size_t height, bool force = false);
		void forceRenderWriteMasks(bool depth, GraphColourWriteMask const& colour);
		void debugVerifyRasterStateCache();
		RasterStateCacheStats getRasterStateCacheStats() const;

		void setDisplay(int width, int height);

		void debugMessage(std::string const& message);

		void infoMessage(std::string const& message);

		void warnMessage(std::string const& message);

		void errorMessage(std::string const& message);

		// Render targets and textures
		void setRenderTarget(RenderTargetPtr renderTarget);

		void pushRenderTarget(RenderTargetPtr renderTarget);

		void popRenderTarget();

		void renderToScreen();

		// External/presentation target used by explicit render-graph pipelines.
		RenderTargetPtr getScreenRenderTarget() const;

		RenderTargetPtr createRenderTexture(std::string const& name, size_t width, size_t height, size_t numAttachments, bool depthBuffer);

		RenderTargetPtr createRenderTexture(std::string const& name, size_t width, size_t height, RenderTextureOptions const& options);

		// Creates a renderer-owned, colour-only floating-point cubemap suitable
		// for cached environment, irradiance, or prefiltered-specular outputs.
		// 0x881B is the backend-neutral wire value for a 16-bit floating RGB image.
		RenderTargetPtr createIblCubemap(std::string const& name, size_t faceSize, uint32_t mipLevels, uint32_t internalFormat = 0x881B);
		// Converts a linear floating-point equirectangular Texture2D into a
		// generated cubemap. Cache publication is deliberately caller-owned.
		RenderTargetPtr convertEquirectangularToCubemap(Texture* hdrEquirectangular, std::string const& generatedName, uint32_t faceSize, uint32_t mipLevels = 1);
		// Generates an unpublished single-mip diffuse irradiance cubemap. Cache
		// lookup/publication remains the responsibility of pipeline runtime code.
		RenderTargetPtr generateDiffuseIrradiance(Texture* environmentCubemap, std::string const& generatedName, uint32_t faceSize, uint32_t sampleCount = 1024);
		// Generates an unpublished GGX-prefiltered specular cubemap. Mip zero is
		// roughness zero; final mip is roughness one.
		RenderTargetPtr generatePrefilteredSpecular(Texture* environmentCubemap, std::string const& generatedName, uint32_t faceSize, uint32_t mipLevels, uint32_t sampleCount = 1024);
		// Returns the renderer-owned split-sum BRDF integration LUT. Creation is
		// lazy and cached for this renderer/context.
		ResourcePtr getOrCreatePbrBrdfIntegrationLut();

		void flushVertexBuffers();

		// Executor contract used to validate MRT shader output locations whenever
		// a program is selected during a graph pass. Zero disables validation.
		void setExpectedGraphColourOutputs(size_t count);

		void beginRenderFlowCapture(RenderPipelineFlowSnapshot* snapshot) noexcept;
		bool endRenderFlowCapture() noexcept;
		bool isRenderFlowCaptureActive() const noexcept;
		void beginRenderFlowPass(GraphPassHandle pass, std::string const& name) noexcept;
		void endRenderFlowPass(GraphPassHandle pass, std::string const& name) noexcept;
		void abortRenderFlowPass() noexcept;
		void failRenderFlowCapture() noexcept;
		void recordRenderFlowBatch(RenderBatchSubmission submission) noexcept;
		void recordRenderFlowStateChanges(std::vector<std::string> changes) noexcept;
		void recordRenderFlowEvent(RenderFlowEventKind kind, std::string const& name,
			GraphImageHandle image = {}, bool enabled = true, std::string const& bypassReason = {},
			std::string const& outputName = {}, bool depth = false,
			std::vector<RenderFlowResourceDesc> inputs = {},
			std::vector<RenderFlowResourceDesc> outputs = {}) noexcept;

		// Clipping
		void pushClipRectangle(ClipRectangle const& clipRect);

		void popClipRectangle();

		void setViewport(int x, int y, size_t width, size_t height);

		void resetViewport();

		//
		// 3d operations
		//

		// Camera
		void pushCameraMatrix();

		void popCameraMatrix();

		glm::mat4 const& getCameraMatrix() const;

		void setCamera3d(glm::vec3 const& position, glm::vec3 const& target, glm::vec3 const& up);

		// Projection
		void pushProjectionMatrix();

		void popProjectionMatrix();

		glm::mat4 const& getProjectionMatrix() const;

		void setProjection3dPerspective(float fov, float nearDist, float farDist);

		glm::mat3 getNormalMatrix() const;

		glm::mat4 const& getModelCameraProjectionMatrix() const;

		// Transforms
		void pushModelMatrix();

		void popModelMatrix();

		glm::mat4 const& getModelMatrix() const;

		void resetTransform();

		// Lights
		void setLightCount(size_t count);

		void setAmbientColour(Colour const& colour);

		void setLight1Position(glm::vec3 const& pos);

		void setLight1Colour(Colour const& colour);

		void setLight2Position(glm::vec3 const& pos);

		void setLight2Colour(Colour const& colour);

		void setPbrAmbientColour(Colour const& colour);

		void setPbrLights(std::vector<PbrLight> const& lights);

		void setActivePbrEnvironment(PbrEnvironmentPtr environment);

		void setActivePipelineSamplerOverrides(std::map<std::string, ResourcePtr> const& overrides);

		std::map<std::string, ResourcePtr> const& getActivePipelineSamplerOverrides() const;

		void setActivePipelineUniformOverrides(UniformCollection const& overrides);

		UniformCollection const& getActivePipelineUniformOverrides() const;

		//
		// Particles
		//

		// Gameplay reaches the extracted CPU API through its renderer owner.
		ParticleSystem& getParticleSystem();
		ParticleSystem const& getParticleSystem() const;
		TrailSystem& getTrailSystem();
		TrailSystem const& getTrailSystem() const;

		// Simulation for this rendered frame, issued before graph execution by a
		// pipeline whose graph draws the corresponding primitive. Graph passes may
		// execute several times per frame and must never call these methods.
		void simulateParticles();
		void simulateTrails();

		// The particle draw, issued from inside an authored blend-class-specific
		// MPP.ParticleScene or MPP.ParticleWeightedOit pass. Scene depth is optional.
		void renderParticles(ParticleBlendClass blendClass, RenderTexture* sceneDepth = nullptr);
		void renderParticles(ParticleBlendClass blendClass, ResourcePtr const& sceneDepth);
		void renderParticleDistortion(RenderTexture* sceneDepth = nullptr);
		void renderParticleDistortion(ResourcePtr const& sceneDepth);
		// Emitter-level, depth-aware additive inscattering. The matching graph pass
		// writes HDR colour and, when present, the emissive/bloom attachment.
		void renderParticleVolumetricLighting(RenderTexture* sceneDepth = nullptr);
		void renderParticleVolumetricLighting(ResourcePtr const& sceneDepth);
		// Real mesh particles use their own material/vertex-attribute pass.
		void renderMeshParticles();
		// The separate ribbon draw issued by MPP.TrailScene.
		void renderTrails(ParticleBlendClass blendClass, RenderTexture* sceneDepth = nullptr);
		void renderTrails(ParticleBlendClass blendClass, ResourcePtr const& sceneDepth);

		bool particlesAvailable();

		// Publishes the camera state the CameraFrame UBO (binding 3) exposes to
		// scene material programs. `view` and `projection` must be exactly the
		// matrices the scene was rasterized with -- the water ray march unprojects
		// the depth buffer through them, so a jittered projection matters.
		void setCameraFrame(glm::mat4 const& view, glm::mat4 const& projection,
			glm::vec2 const& viewportSize, float nearDistance, float farDistance, float seconds);

		// Monotonic seconds since the first call, for shader animation that has no
		// other clock (water ripple scroll). Deliberately not frame-rate derived.
		float getElapsedSeconds() const;

		// A shadow domain is shared by all pipelines that name it in their
		// RenderPipelineOptions. No domain means no shadow allocations or binds.
		void configureShadowDomain(std::string const& name, ShadowOptions const& options);

		bool hasShadowDomain(std::string const& name) const;

		ShadowOptions const& getShadowDomainOptions(std::string const& name) const;

		RenderTargetPtr getShadowDomainDepthTarget(std::string const& name);

		void ensureShadowDomainResources(std::string const& name);

		// Captures automatic scene revisions and reports whether the shared point
		// cubemap needs its six face passes. A completed stable revision is reused
		// by every pipeline naming the domain.
		bool prepareShadowDomain(std::string const& name, std::vector<SceneModel3dPtr> const& models);

		void invalidateShadowDomain(std::string const& name);

		ShadowDomainDiagnostics getShadowDomainDiagnostics(std::string const& name) const;

		// face is ignored for directional domains. Point domains render all six
		// faces when omitted, or exactly the requested canonical cubemap face.
		void renderShadowDomain(std::string const& name, std::vector<SceneModel3dPtr> const& models, uint32_t face = UINT32_MAX);

		void setActiveShadowDomain(std::string const& name);

		// Renders visible opaque geometry with renderer-owned depth-only programs,
		// suppressing every colour attachment. On success the prior raster state is
		// restored except that depth comparison is set to LessEqual for the
		// immediately-following material pass.
		void renderDepthPrepass(
			std::vector<SceneModel3dPtr> const& models, CameraPtr camera,
			size_t colourOutputCount = 1);

		static constexpr size_t getMaxPbrLights() { return MaxPbrLights; }

		//
		// 3d operations
		//
		void translateTransform3d(glm::vec3 const& vec);

		void rotateTransform3d(float angle, glm::vec3 const& axis);

		void scaleTransform3d(glm::vec3 const& vec);

		//
		// 2d operations
		//

		// Projection
		void setProjection2dOrthographic();

		// Transforms
		void translateTransform2d(glm::vec2 const& vec);

		void rotateTransform2d(float angle);

		void scaleTransform2d(glm::vec2 const& vec);

		// Rendering
		void addSceneFactory(std::string const& type, SceneFactory factory);

		ScenePtr createScene(std::string const& type);

		void renderScene(ScenePtr scene, CameraPtr camera, glm::vec2 const& offset2d,
			std::string const& pipelineName, ScenePassOverrides const& overrides = {});

		// Renders one exact-matrix virtual view without executing or mutating the
		// host Camera. The pipeline owns stable HDR-colour/depth attachments keyed by
		// `view.slot`; all renderer state is restored before this call returns or throws.
		AuxiliarySceneOutputs renderAuxiliaryScene(ScenePtr scene, CameraPtr hostCamera,
			std::string const& pipelineName, AuxiliarySceneView const& view);
		AuxiliarySceneDiagnostics const& getAuxiliarySceneDiagnostics(
			std::string const& pipelineName, std::string const& slot) const;

		RenderPipelinePtr getOrCreateRenderPipeline(std::string const& name);

		RenderPipelinePtr getOrCreateRenderPipeline(std::string const& name, RenderPipelineOptions const& options);

		RenderPipelinePtr getRenderPipeline(std::string const& name);

		// Removes an obsolete named pipeline after callers have switched away from it.
		bool removeRenderPipeline(std::string const& name);

		RenderInfo const& getRenderInfo() const;

		void startStatsCollection();
		uint64_t getFrameSerial() const;

		RenderInfo const& finishStatsCollection();

		RenderInfo const& getCurrentRenderInfo() const;

		// Sorts every queued perspective render command by its model-space draw
		// position's distance from the active view. This is intentionally opt-in:
		// it trades material batching (and normal transparent ordering) for a
		// strict closest-first diagnostic path.
		void setSortGeometryFrontToBack(bool enabled);

		bool sortGeometryFrontToBack() const;

		void clearScreen(Colour const& colour);

		void setGamma(float gamma);

		float getGamma() const;
		 
		//
		// 3d rendering
		//
		ModelInstance* renderModelBatched(Model const& model, bool alphaBlend, glm::vec3 const& viewPos);

		ModelInstance* renderModelBatched(Model const& model, bool alphaBlend);

		ModelInstance* renderModelBatched(Model const& model, bool alphaBlend, CameraPtr camera);

		ModelInstance* renderModelBatched(Model const& model, glm::mat4 const& transform, CameraPtr camera);

		ModelInstance* renderModelBatched(Model const& model, bool alphaBlend, glm::vec3 const& viewPos, glm::mat4 const& transform, glm::mat4 const& mcp);

		void renderModelImmediate(Model const& model, bool alphaBlend, glm::vec3 const& viewPos, std::shared_ptr<ModelRenderParams> params);

		void renderModelImmediate(Model const& model, bool alphaBlend, std::shared_ptr<ModelRenderParams> params);

		void renderModelImmediate(Model const& model, bool alphaBlend, std::shared_ptr<ModelRenderParams> params, CameraPtr camera);

		//
		// 2d rendering
		// 
		void renderFullscreenQuad(Texture* texture, BlendMode srcBlend, BlendMode dstBlend, std::shared_ptr<UniformCollection> = nullptr);
		void renderGraphFullscreen(ResourcePtr program, std::vector<std::pair<std::string, Texture*>> const& samplers, UniformCollection const& parameters);
		void renderParticleWeightedOitResolve(Texture* scene, Texture* accumulation, Texture* opticalDepth, Texture* bloom = nullptr);
		void renderParticleDistortionComposite(Texture* scene, Texture* distortion);

		void renderToneMappedFullscreenQuad(Texture* texture, float exposure, bool useAcesToneMap);

		// Resolves an inspectable colour/depth mip into a display-safe target.
		void renderTextureDiagnostic(RenderTexture* source, RenderTargetPtr const& destination, TextureDiagnosticOptions const& options);

		void renderBloomExtract(Texture* source, float threshold);

		void renderBloomBlur(Texture* source, glm::vec2 const& direction);

		void renderBloomCombine(Texture* scene, Texture* bloom, float intensity);

		void renderSSAORaw(RenderTexture* depth, glm::mat4 const& projection, glm::mat4 const& inverseProjection, SSAOOptions const& options);
		void renderGTAORaw(RenderTexture* depth, glm::mat4 const& projection, glm::mat4 const& inverseProjection, GTAOOptions const& options);
		void renderGTAORaw(RenderTexture* depth, Texture* normals, glm::mat4 const& projection, glm::mat4 const& inverseProjection, GTAOOptions const& options);
		void renderSSAOBlur(Texture* ambientOcclusion, RenderTexture* depth, int blurRadius);
		void renderSSAOCombine(Texture* scene, Texture* ambientOcclusion, Texture* modulation = nullptr);
		void renderEnvironmentDebugCube(Texture* environment, Camera* camera);

		void renderQuad(int x, int y, int width, int height, Colour const& colour, bool alphaBlend, bool wireFrame, ResourcePtr texture);

		void renderQuad(int x, int y, int width, int height, Colour const& colour, bool alphaBlend, bool wireFrame);

		void renderText(std::string const& text, int x, int y, Colour const& colour);

		void renderText(std::vector<std::string> const& text, int x, int y, Colour const& colour);

		void renderTextFormatted(std::string const& text, int x, int y);

		void renderTextFormatted(std::vector<std::string> const& text, int x, int y);

		void renderBufferImmediate(int8_t const* vertexData, uint32_t vertexStride, uint32_t numVertices, int8_t* const indexData, uint32_t indexWidth, uint32_t numIndices, std::vector<VertexBufferRenderCommand> const& commands);

		// Override new/delete to force alignment on 16-byte boundary.
		/*
		void* operator new(size_t size)
		{
			return _aligned_malloc(size, 16);
		}

		void operator delete(void* p)
		{
			_aligned_free(p);
		}
		*/
	};

}