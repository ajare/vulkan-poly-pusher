#include "mpp/RenderSystem.h"
#include "WindowBridge.h"
#include <stdexcept>
#include <cstdio>
namespace mpp {
namespace {
[[noreturn]] void unavailableNoexcept(char const* method) noexcept {
    std::fprintf(stderr, "RenderSystem::%s is not delivered in the Vulkan clear/present subset (noexcept API)\n", method);
    std::terminate();
}
}
RenderSystem::RenderSystem(size_t width, size_t height, Logger* logger, RenderSystemOptions options)
    : ResourceWrangler("RenderSystem"), mLogger(logger), mOptions(std::move(options)),
      mWindowWidth(width), mWindowHeight(height), mResourceMgr(nullptr) {
    initialise();
}
RenderSystem::~RenderSystem() = default;
void RenderSystem::initialise() {
    auto const& aa = mOptions.antiAliasing;
    if (aa.msaa != AntiAliasingSamples::Off || aa.ssaa != AntiAliasingSamples::Off || aa.taa || aa.fxaa)
        throw std::runtime_error("Anti-aliasing is not delivered in the Vulkan clear/present subset");
    if (!mVulkanWindow) mVulkanWindow = detail::associatedWindow();
    mVulkanWindow->initialise();
    mCaps = {}; // No GL context; unavailable rendering features stay false/zero.
    // Truthful native vertex-input limits for the delivered geometry-upload
    // subset; every other rendering capability remains unavailable.
    mCaps.maxVertexAttributes = mVulkanWindow->maxVertexInputAttributes();
    mCaps.maxVertexAttributeStride = mVulkanWindow->maxVertexInputBindingStride();
}
size_t RenderSystem::getWindowWidth() const { return mWindowWidth; }
size_t RenderSystem::getWindowHeight() const { return mWindowHeight; }
float RenderSystem::getAspectRatio() const { return mWindowHeight ? float(mWindowWidth)/mWindowHeight : 0; }
Caps const& RenderSystem::getCaps() const { return mCaps; }
void RenderSystem::setDisplay(int w, int h) {
    if (w < 0 || h < 0) throw std::runtime_error("Negative display dimensions");
    mWindowWidth = w; mWindowHeight = h;
}
void RenderSystem::clearScreen(Colour const& colour) { mVulkanWindow->clear(colour); }
uint64_t RenderSystem::getFrameSerial() const { return mFrameSerial; }
void RenderSystem::startStatsCollection() { ++mFrameSerial; mRenderInfo = {}; }
RenderInfo const& RenderSystem::finishStatsCollection() { return mRenderInfo; }
RenderInfo const& RenderSystem::getCurrentRenderInfo() const { return mRenderInfo; }
RenderInfo const& RenderSystem::getRenderInfo() const { return mRenderInfo; }
BackendCapabilities getBackendCapabilities(RenderSystem const& system) {
    return system.mVulkanWindow->capabilities();
}
void captureNextPresent(RenderSystem& system, std::string const& path) {
    system.mVulkanWindow->capture(path);
}
void RenderSystem::createCoreResources(ResourceManager* resourceMgr) { throw std::runtime_error("RenderSystem::createCoreResources is not delivered in the Vulkan clear/present subset"); }
void RenderSystem::destroyCoreResources() { throw std::runtime_error("RenderSystem::destroyCoreResources is not delivered in the Vulkan clear/present subset"); }
void RenderSystem::showDebugPanel(bool show, TimeUnit timeUnit, SizeUnit sizeUnit) { throw std::runtime_error("RenderSystem::showDebugPanel is not delivered in the Vulkan clear/present subset"); }
bool RenderSystem::isDebugPanelShown() const { throw std::runtime_error("RenderSystem::isDebugPanelShown is not delivered in the Vulkan clear/present subset"); }
void RenderSystem::setDebugPreMessages(std::vector<std::string> const& messages) { throw std::runtime_error("RenderSystem::setDebugPreMessages is not delivered in the Vulkan clear/present subset"); }
void RenderSystem::setDebugPostMessages(std::vector<std::string> const& messages) { throw std::runtime_error("RenderSystem::setDebugPostMessages is not delivered in the Vulkan clear/present subset"); }
void RenderSystem::setDefaultState() { throw std::runtime_error("RenderSystem::setDefaultState is not delivered in the Vulkan clear/present subset"); }
GraphRasterState RenderSystem::captureRasterState(size_t colourOutputs) const { throw std::runtime_error("RenderSystem::captureRasterState is not delivered in the Vulkan clear/present subset"); }
void RenderSystem::applyRasterState(GraphRasterState const& state, size_t colourOutputs, size_t width, size_t height, bool force) { throw std::runtime_error("RenderSystem::applyRasterState is not delivered in the Vulkan clear/present subset"); }
void RenderSystem::forceRenderWriteMasks(bool depth, GraphColourWriteMask const& colour) { throw std::runtime_error("RenderSystem::forceRenderWriteMasks is not delivered in the Vulkan clear/present subset"); }
void RenderSystem::debugVerifyRasterStateCache() { throw std::runtime_error("RenderSystem::debugVerifyRasterStateCache is not delivered in the Vulkan clear/present subset"); }
RenderSystem::RasterStateCacheStats RenderSystem::getRasterStateCacheStats() const { throw std::runtime_error("RenderSystem::getRasterStateCacheStats is not delivered in the Vulkan clear/present subset"); }
// Diagnostic logging is delivered: messages route to the client's logger.
void RenderSystem::debugMessage(std::string const& message) { if (mLogger) mLogger->debug(message); }
void RenderSystem::infoMessage(std::string const& message) { if (mLogger) mLogger->info(message); }
void RenderSystem::warnMessage(std::string const& message) { if (mLogger) mLogger->warn(message); }
void RenderSystem::errorMessage(std::string const& message) { if (mLogger) mLogger->error(message); }
void RenderSystem::setRenderTarget(RenderTargetPtr renderTarget) { throw std::runtime_error("RenderSystem::setRenderTarget is not delivered in the Vulkan clear/present subset"); }
void RenderSystem::pushRenderTarget(RenderTargetPtr renderTarget) { throw std::runtime_error("RenderSystem::pushRenderTarget is not delivered in the Vulkan clear/present subset"); }
void RenderSystem::popRenderTarget() { throw std::runtime_error("RenderSystem::popRenderTarget is not delivered in the Vulkan clear/present subset"); }
void RenderSystem::renderToScreen() { throw std::runtime_error("RenderSystem::renderToScreen is not delivered in the Vulkan clear/present subset"); }
RenderTargetPtr RenderSystem::getScreenRenderTarget() const { throw std::runtime_error("RenderSystem::getScreenRenderTarget is not delivered in the Vulkan clear/present subset"); }
RenderTargetPtr RenderSystem::createRenderTexture(std::string const& name, size_t width, size_t height, size_t numAttachments, bool depthBuffer) { throw std::runtime_error("RenderSystem::createRenderTexture is not delivered in the Vulkan clear/present subset"); }
RenderTargetPtr RenderSystem::createRenderTexture(std::string const& name, size_t width, size_t height, RenderTextureOptions const& options) { throw std::runtime_error("RenderSystem::createRenderTexture is not delivered in the Vulkan clear/present subset"); }
RenderTargetPtr RenderSystem::createIblCubemap(std::string const& name, size_t faceSize, uint32_t mipLevels, uint32_t internalFormat) { throw std::runtime_error("RenderSystem::createIblCubemap is not delivered in the Vulkan clear/present subset"); }
RenderTargetPtr RenderSystem::convertEquirectangularToCubemap(Texture* hdrEquirectangular, std::string const& generatedName, uint32_t faceSize, uint32_t mipLevels) { throw std::runtime_error("RenderSystem::convertEquirectangularToCubemap is not delivered in the Vulkan clear/present subset"); }
RenderTargetPtr RenderSystem::generateDiffuseIrradiance(Texture* environmentCubemap, std::string const& generatedName, uint32_t faceSize, uint32_t sampleCount) { throw std::runtime_error("RenderSystem::generateDiffuseIrradiance is not delivered in the Vulkan clear/present subset"); }
RenderTargetPtr RenderSystem::generatePrefilteredSpecular(Texture* environmentCubemap, std::string const& generatedName, uint32_t faceSize, uint32_t mipLevels, uint32_t sampleCount) { throw std::runtime_error("RenderSystem::generatePrefilteredSpecular is not delivered in the Vulkan clear/present subset"); }
ResourcePtr RenderSystem::getOrCreatePbrBrdfIntegrationLut() { throw std::runtime_error("RenderSystem::getOrCreatePbrBrdfIntegrationLut is not delivered in the Vulkan clear/present subset"); }
void RenderSystem::flushVertexBuffers() { throw std::runtime_error("RenderSystem::flushVertexBuffers is not delivered in the Vulkan clear/present subset"); }
void RenderSystem::setExpectedGraphColourOutputs(size_t count) { throw std::runtime_error("RenderSystem::setExpectedGraphColourOutputs is not delivered in the Vulkan clear/present subset"); }
void RenderSystem::beginRenderFlowCapture(RenderPipelineFlowSnapshot* snapshot) noexcept { unavailableNoexcept(__func__); }
bool RenderSystem::endRenderFlowCapture() noexcept { unavailableNoexcept(__func__); }
bool RenderSystem::isRenderFlowCaptureActive() const noexcept { unavailableNoexcept(__func__); }
void RenderSystem::beginRenderFlowPass(GraphPassHandle pass, std::string const& name) noexcept { unavailableNoexcept(__func__); }
void RenderSystem::endRenderFlowPass(GraphPassHandle pass, std::string const& name) noexcept { unavailableNoexcept(__func__); }
void RenderSystem::abortRenderFlowPass() noexcept { unavailableNoexcept(__func__); }
void RenderSystem::failRenderFlowCapture() noexcept { unavailableNoexcept(__func__); }
void RenderSystem::recordRenderFlowBatch(RenderBatchSubmission submission) noexcept { unavailableNoexcept(__func__); }
void RenderSystem::recordRenderFlowStateChanges(std::vector<std::string> changes) noexcept { unavailableNoexcept(__func__); }
void RenderSystem::recordRenderFlowEvent(RenderFlowEventKind kind, std::string const& name, GraphImageHandle image, bool enabled, std::string const& bypassReason, std::string const& outputName, bool depth, std::vector<RenderFlowResourceDesc> inputs, std::vector<RenderFlowResourceDesc> outputs) noexcept { unavailableNoexcept(__func__); }
void RenderSystem::pushClipRectangle(ClipRectangle const& clipRect) { throw std::runtime_error("RenderSystem::pushClipRectangle is not delivered in the Vulkan clear/present subset"); }
void RenderSystem::popClipRectangle() { throw std::runtime_error("RenderSystem::popClipRectangle is not delivered in the Vulkan clear/present subset"); }
void RenderSystem::setViewport(int x, int y, size_t width, size_t height) {
    if (x || y || width != mWindowWidth || height != mWindowHeight)
        throw std::runtime_error("Only full-window viewports are delivered in the Vulkan clear/present subset");
    mViewportX = x; mViewportY = y; mViewportWidth = width; mViewportHeight = height;
}
void RenderSystem::resetViewport() { setViewport(0, 0, mWindowWidth, mWindowHeight); }
void RenderSystem::pushCameraMatrix() { throw std::runtime_error("RenderSystem::pushCameraMatrix is not delivered in the Vulkan clear/present subset"); }
void RenderSystem::popCameraMatrix() { throw std::runtime_error("RenderSystem::popCameraMatrix is not delivered in the Vulkan clear/present subset"); }
glm::mat4 const& RenderSystem::getCameraMatrix() const { throw std::runtime_error("RenderSystem::getCameraMatrix is not delivered in the Vulkan clear/present subset"); }
void RenderSystem::setCamera3d(glm::vec3 const& position, glm::vec3 const& target, glm::vec3 const& up) { throw std::runtime_error("RenderSystem::setCamera3d is not delivered in the Vulkan clear/present subset"); }
void RenderSystem::pushProjectionMatrix() { throw std::runtime_error("RenderSystem::pushProjectionMatrix is not delivered in the Vulkan clear/present subset"); }
void RenderSystem::popProjectionMatrix() { throw std::runtime_error("RenderSystem::popProjectionMatrix is not delivered in the Vulkan clear/present subset"); }
glm::mat4 const& RenderSystem::getProjectionMatrix() const { throw std::runtime_error("RenderSystem::getProjectionMatrix is not delivered in the Vulkan clear/present subset"); }
void RenderSystem::setProjection3dPerspective(float fov, float nearDist, float farDist) { throw std::runtime_error("RenderSystem::setProjection3dPerspective is not delivered in the Vulkan clear/present subset"); }
glm::mat3 RenderSystem::getNormalMatrix() const { throw std::runtime_error("RenderSystem::getNormalMatrix is not delivered in the Vulkan clear/present subset"); }
glm::mat4 const& RenderSystem::getModelCameraProjectionMatrix() const { throw std::runtime_error("RenderSystem::getModelCameraProjectionMatrix is not delivered in the Vulkan clear/present subset"); }
void RenderSystem::pushModelMatrix() { throw std::runtime_error("RenderSystem::pushModelMatrix is not delivered in the Vulkan clear/present subset"); }
void RenderSystem::popModelMatrix() { throw std::runtime_error("RenderSystem::popModelMatrix is not delivered in the Vulkan clear/present subset"); }
glm::mat4 const& RenderSystem::getModelMatrix() const { throw std::runtime_error("RenderSystem::getModelMatrix is not delivered in the Vulkan clear/present subset"); }
void RenderSystem::resetTransform() { throw std::runtime_error("RenderSystem::resetTransform is not delivered in the Vulkan clear/present subset"); }
void RenderSystem::setLightCount(size_t count) { throw std::runtime_error("RenderSystem::setLightCount is not delivered in the Vulkan clear/present subset"); }
void RenderSystem::setAmbientColour(Colour const& colour) { throw std::runtime_error("RenderSystem::setAmbientColour is not delivered in the Vulkan clear/present subset"); }
void RenderSystem::setLight1Position(glm::vec3 const& pos) { throw std::runtime_error("RenderSystem::setLight1Position is not delivered in the Vulkan clear/present subset"); }
void RenderSystem::setLight1Colour(Colour const& colour) { throw std::runtime_error("RenderSystem::setLight1Colour is not delivered in the Vulkan clear/present subset"); }
void RenderSystem::setLight2Position(glm::vec3 const& pos) { throw std::runtime_error("RenderSystem::setLight2Position is not delivered in the Vulkan clear/present subset"); }
void RenderSystem::setLight2Colour(Colour const& colour) { throw std::runtime_error("RenderSystem::setLight2Colour is not delivered in the Vulkan clear/present subset"); }
void RenderSystem::setPbrAmbientColour(Colour const& colour) { throw std::runtime_error("RenderSystem::setPbrAmbientColour is not delivered in the Vulkan clear/present subset"); }
void RenderSystem::setPbrLights(std::vector<PbrLight> const& lights) { throw std::runtime_error("RenderSystem::setPbrLights is not delivered in the Vulkan clear/present subset"); }
void RenderSystem::setActivePbrEnvironment(PbrEnvironmentPtr environment) { throw std::runtime_error("RenderSystem::setActivePbrEnvironment is not delivered in the Vulkan clear/present subset"); }
void RenderSystem::setActivePipelineSamplerOverrides(std::map<std::string, ResourcePtr> const& overrides) { throw std::runtime_error("RenderSystem::setActivePipelineSamplerOverrides is not delivered in the Vulkan clear/present subset"); }
std::map<std::string, ResourcePtr> const& RenderSystem::getActivePipelineSamplerOverrides() const { throw std::runtime_error("RenderSystem::getActivePipelineSamplerOverrides is not delivered in the Vulkan clear/present subset"); }
void RenderSystem::setActivePipelineUniformOverrides(UniformCollection const& overrides) { throw std::runtime_error("RenderSystem::setActivePipelineUniformOverrides is not delivered in the Vulkan clear/present subset"); }
UniformCollection const& RenderSystem::getActivePipelineUniformOverrides() const { throw std::runtime_error("RenderSystem::getActivePipelineUniformOverrides is not delivered in the Vulkan clear/present subset"); }
ParticleSystem& RenderSystem::getParticleSystem() { throw std::runtime_error("RenderSystem::getParticleSystem is not delivered in the Vulkan clear/present subset"); }
ParticleSystem const& RenderSystem::getParticleSystem() const { throw std::runtime_error("RenderSystem::getParticleSystem is not delivered in the Vulkan clear/present subset"); }
TrailSystem& RenderSystem::getTrailSystem() { throw std::runtime_error("RenderSystem::getTrailSystem is not delivered in the Vulkan clear/present subset"); }
TrailSystem const& RenderSystem::getTrailSystem() const { throw std::runtime_error("RenderSystem::getTrailSystem is not delivered in the Vulkan clear/present subset"); }
void RenderSystem::simulateParticles() { throw std::runtime_error("RenderSystem::simulateParticles is not delivered in the Vulkan clear/present subset"); }
void RenderSystem::simulateTrails() { throw std::runtime_error("RenderSystem::simulateTrails is not delivered in the Vulkan clear/present subset"); }
void RenderSystem::renderParticles(ParticleBlendClass blendClass, RenderTexture* sceneDepth) { throw std::runtime_error("RenderSystem::renderParticles is not delivered in the Vulkan clear/present subset"); }
void RenderSystem::renderParticles(ParticleBlendClass blendClass, ResourcePtr const& sceneDepth) { throw std::runtime_error("RenderSystem::renderParticles is not delivered in the Vulkan clear/present subset"); }
void RenderSystem::renderParticleDistortion(RenderTexture* sceneDepth) { throw std::runtime_error("RenderSystem::renderParticleDistortion is not delivered in the Vulkan clear/present subset"); }
void RenderSystem::renderParticleDistortion(ResourcePtr const& sceneDepth) { throw std::runtime_error("RenderSystem::renderParticleDistortion is not delivered in the Vulkan clear/present subset"); }
void RenderSystem::renderParticleVolumetricLighting(RenderTexture* sceneDepth) { throw std::runtime_error("RenderSystem::renderParticleVolumetricLighting is not delivered in the Vulkan clear/present subset"); }
void RenderSystem::renderParticleVolumetricLighting(ResourcePtr const& sceneDepth) { throw std::runtime_error("RenderSystem::renderParticleVolumetricLighting is not delivered in the Vulkan clear/present subset"); }
void RenderSystem::renderMeshParticles() { throw std::runtime_error("RenderSystem::renderMeshParticles is not delivered in the Vulkan clear/present subset"); }
void RenderSystem::renderTrails(ParticleBlendClass blendClass, RenderTexture* sceneDepth) { throw std::runtime_error("RenderSystem::renderTrails is not delivered in the Vulkan clear/present subset"); }
void RenderSystem::renderTrails(ParticleBlendClass blendClass, ResourcePtr const& sceneDepth) { throw std::runtime_error("RenderSystem::renderTrails is not delivered in the Vulkan clear/present subset"); }
bool RenderSystem::particlesAvailable() { throw std::runtime_error("RenderSystem::particlesAvailable is not delivered in the Vulkan clear/present subset"); }
void RenderSystem::setCameraFrame(glm::mat4 const& view, glm::mat4 const& projection, glm::vec2 const& viewportSize, float nearDistance, float farDistance, float seconds) { throw std::runtime_error("RenderSystem::setCameraFrame is not delivered in the Vulkan clear/present subset"); }
float RenderSystem::getElapsedSeconds() const { throw std::runtime_error("RenderSystem::getElapsedSeconds is not delivered in the Vulkan clear/present subset"); }
void RenderSystem::configureShadowDomain(std::string const& name, ShadowOptions const& options) { throw std::runtime_error("RenderSystem::configureShadowDomain is not delivered in the Vulkan clear/present subset"); }
bool RenderSystem::hasShadowDomain(std::string const& name) const { throw std::runtime_error("RenderSystem::hasShadowDomain is not delivered in the Vulkan clear/present subset"); }
ShadowOptions const& RenderSystem::getShadowDomainOptions(std::string const& name) const { throw std::runtime_error("RenderSystem::getShadowDomainOptions is not delivered in the Vulkan clear/present subset"); }
RenderTargetPtr RenderSystem::getShadowDomainDepthTarget(std::string const& name) { throw std::runtime_error("RenderSystem::getShadowDomainDepthTarget is not delivered in the Vulkan clear/present subset"); }
void RenderSystem::ensureShadowDomainResources(std::string const& name) { throw std::runtime_error("RenderSystem::ensureShadowDomainResources is not delivered in the Vulkan clear/present subset"); }
bool RenderSystem::prepareShadowDomain(std::string const& name, std::vector<SceneModel3dPtr> const& models) { throw std::runtime_error("RenderSystem::prepareShadowDomain is not delivered in the Vulkan clear/present subset"); }
void RenderSystem::invalidateShadowDomain(std::string const& name) { throw std::runtime_error("RenderSystem::invalidateShadowDomain is not delivered in the Vulkan clear/present subset"); }
ShadowDomainDiagnostics RenderSystem::getShadowDomainDiagnostics(std::string const& name) const { throw std::runtime_error("RenderSystem::getShadowDomainDiagnostics is not delivered in the Vulkan clear/present subset"); }
void RenderSystem::renderShadowDomain(std::string const& name, std::vector<SceneModel3dPtr> const& models, uint32_t face) { throw std::runtime_error("RenderSystem::renderShadowDomain is not delivered in the Vulkan clear/present subset"); }
void RenderSystem::setActiveShadowDomain(std::string const& name) { throw std::runtime_error("RenderSystem::setActiveShadowDomain is not delivered in the Vulkan clear/present subset"); }
void RenderSystem::renderDepthPrepass(std::vector<SceneModel3dPtr> const& models, CameraPtr camera, size_t colourOutputCount) { throw std::runtime_error("RenderSystem::renderDepthPrepass is not delivered in the Vulkan clear/present subset"); }
void RenderSystem::translateTransform3d(glm::vec3 const& vec) { throw std::runtime_error("RenderSystem::translateTransform3d is not delivered in the Vulkan clear/present subset"); }
void RenderSystem::rotateTransform3d(float angle, glm::vec3 const& axis) { throw std::runtime_error("RenderSystem::rotateTransform3d is not delivered in the Vulkan clear/present subset"); }
void RenderSystem::scaleTransform3d(glm::vec3 const& vec) { throw std::runtime_error("RenderSystem::scaleTransform3d is not delivered in the Vulkan clear/present subset"); }
void RenderSystem::setProjection2dOrthographic() { throw std::runtime_error("RenderSystem::setProjection2dOrthographic is not delivered in the Vulkan clear/present subset"); }
void RenderSystem::translateTransform2d(glm::vec2 const& vec) { throw std::runtime_error("RenderSystem::translateTransform2d is not delivered in the Vulkan clear/present subset"); }
void RenderSystem::rotateTransform2d(float angle) { throw std::runtime_error("RenderSystem::rotateTransform2d is not delivered in the Vulkan clear/present subset"); }
void RenderSystem::scaleTransform2d(glm::vec2 const& vec) { throw std::runtime_error("RenderSystem::scaleTransform2d is not delivered in the Vulkan clear/present subset"); }
void RenderSystem::addSceneFactory(std::string const& type, SceneFactory factory) { throw std::runtime_error("RenderSystem::addSceneFactory is not delivered in the Vulkan clear/present subset"); }
ScenePtr RenderSystem::createScene(std::string const& type) { throw std::runtime_error("RenderSystem::createScene is not delivered in the Vulkan clear/present subset"); }
void RenderSystem::renderScene(ScenePtr scene, CameraPtr camera, glm::vec2 const& offset2d, std::string const& pipelineName, ScenePassOverrides const& overrides) { throw std::runtime_error("RenderSystem::renderScene is not delivered in the Vulkan clear/present subset"); }
AuxiliarySceneOutputs RenderSystem::renderAuxiliaryScene(ScenePtr scene, CameraPtr hostCamera, std::string const& pipelineName, AuxiliarySceneView const& view) { throw std::runtime_error("RenderSystem::renderAuxiliaryScene is not delivered in the Vulkan clear/present subset"); }
AuxiliarySceneDiagnostics const& RenderSystem::getAuxiliarySceneDiagnostics(std::string const& pipelineName, std::string const& slot) const { throw std::runtime_error("RenderSystem::getAuxiliarySceneDiagnostics is not delivered in the Vulkan clear/present subset"); }
RenderPipelinePtr RenderSystem::getOrCreateRenderPipeline(std::string const& name) { throw std::runtime_error("RenderSystem::getOrCreateRenderPipeline is not delivered in the Vulkan clear/present subset"); }
RenderPipelinePtr RenderSystem::getOrCreateRenderPipeline(std::string const& name, RenderPipelineOptions const& options) { throw std::runtime_error("RenderSystem::getOrCreateRenderPipeline is not delivered in the Vulkan clear/present subset"); }
RenderPipelinePtr RenderSystem::getRenderPipeline(std::string const& name) { throw std::runtime_error("RenderSystem::getRenderPipeline is not delivered in the Vulkan clear/present subset"); }
bool RenderSystem::removeRenderPipeline(std::string const& name) { throw std::runtime_error("RenderSystem::removeRenderPipeline is not delivered in the Vulkan clear/present subset"); }
void RenderSystem::setSortGeometryFrontToBack(bool enabled) { throw std::runtime_error("RenderSystem::setSortGeometryFrontToBack is not delivered in the Vulkan clear/present subset"); }
bool RenderSystem::sortGeometryFrontToBack() const { throw std::runtime_error("RenderSystem::sortGeometryFrontToBack is not delivered in the Vulkan clear/present subset"); }
void RenderSystem::setGamma(float gamma) { throw std::runtime_error("RenderSystem::setGamma is not delivered in the Vulkan clear/present subset"); }
float RenderSystem::getGamma() const { throw std::runtime_error("RenderSystem::getGamma is not delivered in the Vulkan clear/present subset"); }
ModelInstance* RenderSystem::renderModelBatched(Model const& model, bool alphaBlend, glm::vec3 const& viewPos) { throw std::runtime_error("RenderSystem::renderModelBatched is not delivered in the Vulkan clear/present subset"); }
ModelInstance* RenderSystem::renderModelBatched(Model const& model, bool alphaBlend) { throw std::runtime_error("RenderSystem::renderModelBatched is not delivered in the Vulkan clear/present subset"); }
ModelInstance* RenderSystem::renderModelBatched(Model const& model, bool alphaBlend, CameraPtr camera) { throw std::runtime_error("RenderSystem::renderModelBatched is not delivered in the Vulkan clear/present subset"); }
ModelInstance* RenderSystem::renderModelBatched(Model const& model, glm::mat4 const& transform, CameraPtr camera) { throw std::runtime_error("RenderSystem::renderModelBatched is not delivered in the Vulkan clear/present subset"); }
ModelInstance* RenderSystem::renderModelBatched(Model const& model, bool alphaBlend, glm::vec3 const& viewPos, glm::mat4 const& transform, glm::mat4 const& mcp) { throw std::runtime_error("RenderSystem::renderModelBatched is not delivered in the Vulkan clear/present subset"); }
void RenderSystem::renderModelImmediate(Model const& model, bool alphaBlend, glm::vec3 const& viewPos, std::shared_ptr<ModelRenderParams> params) { throw std::runtime_error("RenderSystem::renderModelImmediate is not delivered in the Vulkan clear/present subset"); }
void RenderSystem::renderModelImmediate(Model const& model, bool alphaBlend, std::shared_ptr<ModelRenderParams> params) { throw std::runtime_error("RenderSystem::renderModelImmediate is not delivered in the Vulkan clear/present subset"); }
void RenderSystem::renderModelImmediate(Model const& model, bool alphaBlend, std::shared_ptr<ModelRenderParams> params, CameraPtr camera) { throw std::runtime_error("RenderSystem::renderModelImmediate is not delivered in the Vulkan clear/present subset"); }
void RenderSystem::renderFullscreenQuad(Texture* texture, BlendMode srcBlend, BlendMode dstBlend, std::shared_ptr<UniformCollection>) { throw std::runtime_error("RenderSystem::renderFullscreenQuad is not delivered in the Vulkan clear/present subset"); }
void RenderSystem::renderGraphFullscreen(ResourcePtr program, std::vector<std::pair<std::string, Texture*>> const& samplers, UniformCollection const& parameters) { throw std::runtime_error("RenderSystem::renderGraphFullscreen is not delivered in the Vulkan clear/present subset"); }
void RenderSystem::renderParticleWeightedOitResolve(Texture* scene, Texture* accumulation, Texture* opticalDepth, Texture* bloom) { throw std::runtime_error("RenderSystem::renderParticleWeightedOitResolve is not delivered in the Vulkan clear/present subset"); }
void RenderSystem::renderParticleDistortionComposite(Texture* scene, Texture* distortion) { throw std::runtime_error("RenderSystem::renderParticleDistortionComposite is not delivered in the Vulkan clear/present subset"); }
void RenderSystem::renderToneMappedFullscreenQuad(Texture* texture, float exposure, bool useAcesToneMap) { throw std::runtime_error("RenderSystem::renderToneMappedFullscreenQuad is not delivered in the Vulkan clear/present subset"); }
void RenderSystem::renderTextureDiagnostic(RenderTexture* source, RenderTargetPtr const& destination, TextureDiagnosticOptions const& options) { throw std::runtime_error("RenderSystem::renderTextureDiagnostic is not delivered in the Vulkan clear/present subset"); }
void RenderSystem::renderBloomExtract(Texture* source, float threshold) { throw std::runtime_error("RenderSystem::renderBloomExtract is not delivered in the Vulkan clear/present subset"); }
void RenderSystem::renderBloomBlur(Texture* source, glm::vec2 const& direction) { throw std::runtime_error("RenderSystem::renderBloomBlur is not delivered in the Vulkan clear/present subset"); }
void RenderSystem::renderBloomCombine(Texture* scene, Texture* bloom, float intensity) { throw std::runtime_error("RenderSystem::renderBloomCombine is not delivered in the Vulkan clear/present subset"); }
void RenderSystem::renderSSAORaw(RenderTexture* depth, glm::mat4 const& projection, glm::mat4 const& inverseProjection, SSAOOptions const& options) { throw std::runtime_error("RenderSystem::renderSSAORaw is not delivered in the Vulkan clear/present subset"); }
void RenderSystem::renderGTAORaw(RenderTexture* depth, glm::mat4 const& projection, glm::mat4 const& inverseProjection, GTAOOptions const& options) { throw std::runtime_error("RenderSystem::renderGTAORaw is not delivered in the Vulkan clear/present subset"); }
void RenderSystem::renderGTAORaw(RenderTexture* depth, Texture* normals, glm::mat4 const& projection, glm::mat4 const& inverseProjection, GTAOOptions const& options) { throw std::runtime_error("RenderSystem::renderGTAORaw is not delivered in the Vulkan clear/present subset"); }
void RenderSystem::renderSSAOBlur(Texture* ambientOcclusion, RenderTexture* depth, int blurRadius) { throw std::runtime_error("RenderSystem::renderSSAOBlur is not delivered in the Vulkan clear/present subset"); }
void RenderSystem::renderSSAOCombine(Texture* scene, Texture* ambientOcclusion, Texture* modulation) { throw std::runtime_error("RenderSystem::renderSSAOCombine is not delivered in the Vulkan clear/present subset"); }
void RenderSystem::renderEnvironmentDebugCube(Texture* environment, Camera* camera) { throw std::runtime_error("RenderSystem::renderEnvironmentDebugCube is not delivered in the Vulkan clear/present subset"); }
void RenderSystem::renderQuad(int x, int y, int width, int height, Colour const& colour, bool alphaBlend, bool wireFrame, ResourcePtr texture) { throw std::runtime_error("RenderSystem::renderQuad is not delivered in the Vulkan clear/present subset"); }
void RenderSystem::renderQuad(int x, int y, int width, int height, Colour const& colour, bool alphaBlend, bool wireFrame) { throw std::runtime_error("RenderSystem::renderQuad is not delivered in the Vulkan clear/present subset"); }
void RenderSystem::renderText(std::string const& text, int x, int y, Colour const& colour) { throw std::runtime_error("RenderSystem::renderText is not delivered in the Vulkan clear/present subset"); }
void RenderSystem::renderText(std::vector<std::string> const& text, int x, int y, Colour const& colour) { throw std::runtime_error("RenderSystem::renderText is not delivered in the Vulkan clear/present subset"); }
void RenderSystem::renderTextFormatted(std::string const& text, int x, int y) { throw std::runtime_error("RenderSystem::renderTextFormatted is not delivered in the Vulkan clear/present subset"); }
void RenderSystem::renderTextFormatted(std::vector<std::string> const& text, int x, int y) { throw std::runtime_error("RenderSystem::renderTextFormatted is not delivered in the Vulkan clear/present subset"); }
void RenderSystem::renderBufferImmediate(int8_t const* vertexData, uint32_t vertexStride, uint32_t numVertices, int8_t* const indexData, uint32_t indexWidth, uint32_t numIndices, std::vector<VertexBufferRenderCommand> const& commands) { throw std::runtime_error("RenderSystem::renderBufferImmediate is not delivered in the Vulkan clear/present subset"); }
}
