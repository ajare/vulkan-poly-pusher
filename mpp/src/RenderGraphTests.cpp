#include <cmath>

#include "mpp/Caps.h"
#include "mpp/RenderGraph.h"
#include "mpp/RenderGraphBuiltInPasses.h"
#include "mpp/RenderGraphPassFactoryRegistry.h"
#include "mpp/RenderGraphTests.h"
#include "mpp/RenderPipeline.h"
#include "mpp/SceneDocument.h"
#include "mpp/DefaultShaders.h"
#include "mpp/PbrShaders.h"
#include "mpp/ParticleData.h"
#include "mpp/ParticleShaders.h"
#include "mpp/ModelRenderParams.h"
#include "mpp/mesh/MeshSpecification.h"

namespace mpp
{
	bool runRenderGraphTopologyTests(std::string* failure)
	{
		auto fail = [&](std::string const& message) { if (failure) *failure = message; return false; };

		auto validatesBuiltInNormalContract = [](std::string const& shader)
		{
			auto colour = shader.find("@Out(vec4 COLOUR)");
			auto bloom = shader.find("@Out(vec4 BLOOM_MASK)");
			auto normals = shader.find("@Out(vec2 SHADING_NORMAL)");
			return colour != std::string::npos && bloom > colour && normals > bloom &&
				shader.find("encodeOctahedralNormal") != std::string::npos && shader.find("mat3(VIEW_MATRIX)") != std::string::npos;
		};
		if (!validatesBuiltInNormalContract(FragmentShader3dTemplate)) return fail("built-in legacy shader lost the location-2 view-space octahedral shading-normal contract");
		auto const particleVertex = std::string(ParticleDrawVertexShader);
		for (auto const* mode : { "BILLBOARD_CAMERA_FACING", "BILLBOARD_SCREEN_ALIGNED", "BILLBOARD_CYLINDRICAL", "BILLBOARD_AXIS_LOCKED", "BILLBOARD_VELOCITY_ALIGNED", "BILLBOARD_VELOCITY_STRETCHED" })
			if (particleVertex.find(mode) == std::string::npos) return fail("particle shader lost a required billboard mode");
		auto const particleFragment = std::string(ParticleDrawFragmentShader);
		auto const radixScatter = std::string(ParticleRadixScatterComputeShader);
		auto const oitResolve = std::string(ParticleWeightedOitResolveFragmentShader);
		auto const distortionComposite = std::string(ParticleDistortionCompositeFragmentShader);
		if (particleVertex.find("particleHalfExtents") == std::string::npos ||
			particleVertex.find("length(projectedVelocity)") == std::string::npos ||
			particleVertex.find("expandParticleQuad") == std::string::npos ||
			particleVertex.find("ANIMATION_FRAME_OVER_LIFE") == std::string::npos ||
			particleVertex.find("ANIMATION_FIXED_RATE") == std::string::npos ||
			particleVertex.find("ANIMATION_RANDOM_START") == std::string::npos ||
			particleFragment.find("linearViewDepth(sceneDepth) - PARTICLE_VIEW_DEPTH") == std::string::npos ||
			particleFragment.find("MPP_PARTICLE_WEIGHTED_OIT") == std::string::npos ||
			particleFragment.find("MPP_PARTICLE_DISTORTION") == std::string::npos ||
			particleFragment.find("PARTICLE_DISTORTION_STRENGTH") == std::string::npos ||
			particleFragment.find("-log(max(1.0 - alpha") == std::string::npos ||
			std::string(ParticleSortKeyComputeShader).find("descendingFloatKey") == std::string::npos ||
			radixScatter.find("LOCAL_DIGITS[earlier] == digit") == std::string::npos ||
			oitResolve.find("exp(-opticalDepth)") == std::string::npos ||
			oitResolve.find("accumulation.rgb / max(accumulation.a") == std::string::npos ||
			distortionComposite.find("uv + offset") == std::string::npos)
			return fail("particle shader lost billboard expansion, animation, soft depth, distortion, radix sorting, or weighted blended OIT");
		if (!validatesBuiltInNormalContract(BuiltInPbrFragmentShader)) return fail("built-in PBR shader lost the location-2 view-space octahedral shading-normal contract");
		auto pbrFinalNormal = std::string(BuiltInPbrFragmentShader).find("@Out(vec2 SHADING_NORMAL)");
		if (pbrFinalNormal < std::string(BuiltInPbrFragmentShader).find("PBR_WATER_DISTORTION_STRENGTH", std::string(BuiltInPbrFragmentShader).find("void main()")))
			return fail("built-in PBR shader writes MRT normals before material normal processing");
		auto validatesPointShadowContract = [](std::string const& shader)
		{
			return shader.find("sampler2DShadow SHADOW_MAP") != std::string::npos &&
				shader.find("samplerCubeShadow POINT_SHADOW_MAP") != std::string::npos &&
				shader.find("SHADOW_TYPE_AND_LIGHT_INDEX.y") != std::string::npos &&
				shader.find("pointShadowVisibility") != std::string::npos;
		};
		if (!validatesPointShadowContract(FragmentShader3dTemplate) || !validatesPointShadowContract(BuiltInPbrFragmentShader))
			return fail("built-in receiver lost directional/point shadow sampling or explicit light association");
		if (VertexShaderPointShadowDepthTemplate.find("SHADOW_WORLD_POSITION") == std::string::npos ||
			FragmentShaderPointShadowDepthTemplate.find("gl_FragDepth = length") == std::string::npos)
			return fail("point caster no longer writes radial cubemap depth");
		if (VertexShaderAlphaShadowDepthTemplate.find("SHADOW_TEXCOORDS") == std::string::npos ||
			FragmentShaderAlphaShadowDepthTemplate.find("SHADOW_ALPHA_CUTOFF") == std::string::npos ||
			FragmentShaderAlphaShadowDepthTemplate.find("discard") == std::string::npos ||
			VertexShaderPointAlphaShadowDepthTemplate.find("SHADOW_WORLD_POSITION") == std::string::npos ||
			FragmentShaderPointAlphaShadowDepthTemplate.find("discard") == std::string::npos ||
			FragmentShaderPointAlphaShadowDepthTemplate.find("gl_FragDepth = length") == std::string::npos)
			return fail("masked caster lost its directional or point-depth silhouette path");
		auto validatesFilteredPointShadow = [](std::string const& shader)
		{
			return shader.find("for (int y = -1; y <= 1; ++y)") != std::string::npos &&
				shader.find("for (int x = -1; x <= 1; ++x)") != std::string::npos &&
				shader.find("tapDirection") != std::string::npos &&
				shader.find("visibility /= 9.0") != std::string::npos &&
				shader.find("BIAS_AND_ENABLED.w") != std::string::npos &&
				shader.find("mix(visibility, 1.0, fade)") != std::string::npos;
		};
		if (!validatesFilteredPointShadow(FragmentShader3dTemplate) || !validatesFilteredPointShadow(BuiltInPbrFragmentShader))
			return fail("built-in point receivers lost tangent-space 3x3 PCF or range fade");
		RenderPipelineOptions pipelineDefaults;
		if (!pipelineDefaults.depthPrepass)
			return fail("depth prepass is not enabled by default");
		if (pipelineDefaults.waterReflections.technique != WaterReflectionTechnique::ScreenSpace ||
			pipelineDefaults.waterReflections.planarResolution != PlanarReflectionResolution::Half ||
			!pipelineDefaults.waterReflections.planarPlanes.empty() ||
			WaterReflectionOptions::MaxPlanarPlanes != 4)
			return fail("generated Water reflection-source defaults changed unexpectedly");
		pipelineDefaults.waterReflections.technique = WaterReflectionTechnique::Planar;
		pipelineDefaults.waterReflections.planarResolution = PlanarReflectionResolution::Quarter;
		pipelineDefaults.waterReflections.planarPlanes.push_back(
			{ 12.5f, ReflectionPlaneSide::Below, 12.49f, 12.51f });
		if (pipelineDefaults.waterReflections.technique != WaterReflectionTechnique::Planar ||
			pipelineDefaults.waterReflections.planarResolution != PlanarReflectionResolution::Quarter ||
			pipelineDefaults.waterReflections.planarPlanes.front().elevation != 12.5f ||
			pipelineDefaults.waterReflections.planarPlanes.front().viewerSide != ReflectionPlaneSide::Below ||
			pipelineDefaults.waterReflections.planarPlanes.front().minimumMatchingElevation != 12.49f ||
			pipelineDefaults.waterReflections.planarPlanes.front().maximumMatchingElevation != 12.51f)
			return fail("generated Water pipeline did not retain its generic Planar source contract");
		pipelineDefaults.depthPrepass = false;
		if (pipelineDefaults.depthPrepass)
			return fail("disabled depth prepass pipeline option was not retained");

		auto nearVector = [](glm::vec3 const& left, glm::vec3 const& right)
		{
			return glm::length(left - right) < 0.0001f;
		};
		auto nearMatrix = [](glm::mat4 const& left, glm::mat4 const& right)
		{
			for (int column = 0; column < 4; ++column)
				for (int row = 0; row < 4; ++row)
					if (std::abs(left[column][row] - right[column][row]) >= 0.0001f) return false;
			return true;
		};
		Camera reflectionSource(glm::vec3(2.0f, 6.0f, 4.0f), 0.0f, 0.0f, 0.0f, 60.0f, 1.5f);
		reflectionSource.setLookAt(reflectionSource.getPosition(), glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
		auto const sourceDirection = reflectionSource.getDirection();
		auto const sourceUp = reflectionSource.getUp();
		auto const reflected = buildPlanarReflectionView(reflectionSource,
			{ 1.0f, ReflectionPlaneSide::Above }, 1.5f);
		if (!nearVector(reflected.position, glm::vec3(2.0f, -4.0f, 4.0f)) ||
			!nearVector(reflected.direction, glm::vec3(sourceDirection.x, -sourceDirection.y, sourceDirection.z)) ||
			!nearVector(reflected.up, glm::vec3(sourceUp.x, -sourceUp.y, sourceUp.z)))
			return fail("Planar reflection did not mirror camera position, direction, and up around the horizontal plane");
		auto insideClip = [&](glm::vec3 const& point)
		{
			auto clip = reflected.projection * reflected.view * glm::vec4(point, 1.0f);
			return clip.w > 0.0f && clip.z >= -clip.w && clip.z <= clip.w;
		};
		if (!insideClip(glm::vec3(0.0f, 2.0f, 0.0f)) ||
			!insideClip(glm::vec3(0.0f, 0.96f, 0.0f)) ||
			insideClip(glm::vec3(0.0f, 0.94f, 0.0f)) ||
			insideClip(glm::vec3(0.0f, 0.0f, 0.0f)))
			return fail("viewer-above Planar clipping did not retain above-plane geometry with a 0.05-unit bias");
		Camera reflectionSourceBelow(glm::vec3(2.0f, -4.0f, 4.0f), 0.0f, 0.0f, 0.0f, 60.0f, 1.5f);
		reflectionSourceBelow.setLookAt(reflectionSourceBelow.getPosition(),
			glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
		auto const reflectedBelow = buildPlanarReflectionView(reflectionSourceBelow,
			{ 1.0f, ReflectionPlaneSide::Below }, 1.5f);
		auto insideBelowClip = [&](glm::vec3 const& point)
		{
			auto clip = reflectedBelow.projection * reflectedBelow.view * glm::vec4(point, 1.0f);
			return clip.w > 0.0f && clip.z >= -clip.w && clip.z <= clip.w;
		};
		if (!insideBelowClip(glm::vec3(0.0f, 0.0f, 0.0f)) ||
			!insideBelowClip(glm::vec3(0.0f, 1.04f, 0.0f)) ||
			insideBelowClip(glm::vec3(0.0f, 1.06f, 0.0f)) ||
			insideBelowClip(glm::vec3(0.0f, 2.0f, 0.0f)))
			return fail("viewer-below Planar clipping did not retain below-plane geometry with a 0.05-unit bias");

		// The public generic seam accepts exact matrices and any world plane. A
		// vertical x=0 plane retains x>=0 and applies the same normalized 0.05-unit
		// expansion even when the caller scales the plane equation.
		auto const verticalView = glm::lookAt(glm::vec3(4.0f, 0.0f, 6.0f),
			glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
		auto const verticalProjection = glm::perspective(glm::radians(60.0f), 1.5f, 0.1f, 100.0f);
		auto const vertical = buildObliquelyClippedVirtualCamera(
			verticalView, verticalProjection, glm::vec4(1.0f, 0.0f, 0.0f, 0.0f));
		auto const scaledVertical = buildObliquelyClippedVirtualCamera(
			verticalView, verticalProjection, glm::vec4(7.0f, 0.0f, 0.0f, 0.0f));
		auto insideVerticalClip = [&](glm::vec3 const& point)
		{
			auto const clip = vertical.projection * vertical.view * glm::vec4(point, 1.0f);
			return clip.w > 0.0f && clip.z >= -clip.w && clip.z <= clip.w;
		};
		if (!nearMatrix(vertical.view, verticalView) ||
			!nearMatrix(vertical.projection, scaledVertical.projection) ||
			!insideVerticalClip(glm::vec3(1.0f, 0.0f, 0.0f)) ||
			!insideVerticalClip(glm::vec3(-0.04f, 0.0f, 0.0f)) ||
			insideVerticalClip(glm::vec3(-0.06f, 0.0f, 0.0f)) ||
			insideVerticalClip(glm::vec3(-1.0f, 0.0f, 0.0f)))
			return fail("generic vertical oblique clipping lost exact matrices, normalized plane scale, or the documented 0.05-unit seam bias");
		VirtualCamera exactCamera(verticalView, verticalProjection, 0.1f, 100.0f);
		if (!nearMatrix(exactCamera.getViewTransform(), verticalView) ||
			!nearMatrix(exactCamera.getProjectionTransform(), verticalProjection))
			return fail("matrix-backed virtual camera did not preserve exact application transforms");
		auto const sharedPlanarProjection = buildObliquelyClippedVirtualCamera(
			reflected.view,
			glm::perspective(glm::radians(reflectionSource.getFov()), 1.5f,
				reflectionSource.getNearClipDistance(), reflectionSource.getFarClipDistance()),
			glm::vec4(0.0f, 1.0f, 0.0f, -1.0f)).projection;
		if (!nearMatrix(reflected.projection, sharedPlanarProjection))
			return fail("Planar reflection no longer uses the generic virtual-camera oblique-clipping machinery");

		ShadowOptions pointDefaults;
		if (pointDefaults.nearPlane != 0.25f || pointDefaults.light.range != 192.0f ||
			pointDefaults.filterRadiusTexels != 1.0f || pointDefaults.fadeStartNormalized != 0.9f ||
			pointDefaults.filterMode != ShadowFilterMode::Pcf3x3)
			return fail("point-shadow quality defaults are not the Player Torch contract");

		SceneDocument pointScene;
		pointScene.name = "Authored point shadow";
		SceneLightDocument fillLight; fillLight.id = "Fill";
		SceneLightDocument pointLight; pointLight.id = "PointShadow"; pointLight.type = SceneLightType::Point;
		pointLight.position = { 3.0f, 4.0f, 5.0f }; pointLight.range = 24.0f; pointLight.castsShadows = true;
		SceneLightDocument rimLight; rimLight.id = "Rim";
		pointScene.lights = { fillLight, pointLight, rimLight };
		if (pointScene.validate().hasErrors() || pointScene.getShadowLightIndex() != 1)
			return fail("an authored point shadow was rejected or lost its independent light index");
		auto invalidPointScene = pointScene;
		invalidPointScene.lights[1].range = 0.0f;
		if (!invalidPointScene.validate().hasErrors())
			return fail("a shadow-casting point light accepted a non-positive range");
		invalidPointScene = pointScene;
		SceneLightDocument secondShadow; secondShadow.id = "SecondShadow"; secondShadow.castsShadows = true;
		invalidPointScene.lights.push_back(secondShadow);
		if (!invalidPointScene.validate().hasErrors())
			return fail("multiple authored shadow lights were accepted");

		ModelRenderParams modelParams;
		auto const initialProgramSetRevision = modelParams.getProgramSetRevision();
		auto const initialShadowRevision = modelParams.getShadowRevision();
		modelParams.setModelInstanceCount(2);
		if (modelParams.getShadowRevision() <= initialShadowRevision) return fail("model instance change did not invalidate shadow output");
		modelParams.setMeshUniforms("Mesh", {});
		modelParams.setModelBlend(true);
		modelParams.setMeshBlend("Mesh", false);
		auto const& blendParams = modelParams.getMeshParams();
		if (!blendParams.at("").blend.has_value() || !*blendParams.at("").blend ||
			!blendParams.at("Mesh").blend.has_value() || *blendParams.at("Mesh").blend)
			return fail("model or mesh blend override was not retained");
		if (modelParams.getProgramSetRevision() != initialProgramSetRevision) return fail("non-program model parameters invalidated the visible program set");
		modelParams.setModelFlags(ModelRenderParams::Flag_Visible);
		auto const modelFlagsRevision = modelParams.getProgramSetRevision();
		modelParams.setMeshFlags("Mesh", 0);
		if (modelFlagsRevision <= initialProgramSetRevision || modelParams.getProgramSetRevision() <= modelFlagsRevision)
			return fail("visibility/material model parameters did not invalidate the visible program set");
		auto const casterPolicyRevision = modelParams.getShadowRevision();
		modelParams.setModelFlags(ModelRenderParams::Flag_Visible | ModelRenderParams::Flag_CastShadows);
		if (modelParams.getShadowRevision() <= casterPolicyRevision) return fail("caster-policy change did not invalidate shadow output");
		auto const materialContractRevision = modelParams.getShadowRevision();
		modelParams.setMeshMaterial("ShadowContractMesh", {});
		if (modelParams.getShadowRevision() <= materialContractRevision) return fail("material shadow-contract override did not invalidate shadow output");
		auto const unchangedFlagsRevision = modelParams.getProgramSetRevision();
		modelParams.setMeshFlags("Mesh", 0);
		if (modelParams.getProgramSetRevision() != unchangedFlagsRevision) return fail("unchanged visibility invalidated the visible program set");

		// Mesh layout identity feeds program caching. Every equality field must be
		// represented by an unambiguous canonical key, while the compact hash and
		// generated descriptor must distinguish common formerly-colliding layouts.
		auto makeMeshSpecification = [](mesh::Vertex::DataType type, bool normalised = false, size_t boundary = 0, std::string identifier = "POSITION")
		{
			mesh::MeshSpecification specification;
			auto* layout = specification.createVertexBufferAttributeLayout(false);
			layout->createAttribute(mesh::Vertex::Component::Position2, identifier, type, normalised, boundary);
			return specification;
		};
		auto floatLayout = makeMeshSpecification(mesh::Vertex::DataType::Float);
		auto identicalFloatLayout = floatLayout;
		auto integerLayout = makeMeshSpecification(mesh::Vertex::DataType::Int);
		auto normalisedLayout = makeMeshSpecification(mesh::Vertex::DataType::Float, true);
		auto paddedLayout = makeMeshSpecification(mesh::Vertex::DataType::Float, false, 16);
		auto identifiedLayout = makeMeshSpecification(mesh::Vertex::DataType::Float, false, 0, "CUSTOM_POSITION");
		auto offsetLayout = floatLayout; offsetLayout.getVertexBufferAttributeLayout(0).getAttribute(0).offsetInBytes = 4;
		if (floatLayout != identicalFloatLayout || floatLayout.getHashString() != identicalFloatLayout.getHashString() || floatLayout.getHashCode() != identicalFloatLayout.getHashCode())
			return fail("equal mesh layouts do not have stable canonical identities");
		for (auto const* different : { &integerLayout, &normalisedLayout, &paddedLayout, &identifiedLayout, &offsetLayout })
		{
			if (floatLayout == *different || floatLayout.getHashString() == different->getHashString() || floatLayout.getHashCode() == different->getHashCode() || floatLayout.getDescriptor("mesh_") == different->getDescriptor("mesh_"))
				return fail("mesh identity collapsed a differing attribute type, normalization, offset, padding, or identifier");
		}
		auto indexedLayout = floatLayout; indexedLayout.setIndexedVertices(true);
		auto dynamicLayout = floatLayout; dynamicLayout.setStorageType(mesh::VertexBufferStorageType::Dynamic);
		auto lineLayout = floatLayout; lineLayout.setPrimitiveType(mesh::Primitive::Type::Lines);
		auto staticLayout = mesh::MeshSpecification();
		staticLayout.createVertexBufferAttributeLayout(true)->createAttribute(mesh::Vertex::Component::Position2, mesh::Vertex::DataType::Float, false);
		for (auto const* different : { &indexedLayout, &dynamicLayout, &lineLayout, &staticLayout })
			if (floatLayout.getHashString() == different->getHashString()) return fail("mesh identity omitted primitive, storage, indexing, or buffer-static state");
		mesh::MeshSpecification groupedLayout;
		auto* groupedFirst = groupedLayout.createVertexBufferAttributeLayout(false);
		groupedFirst->createAttribute(mesh::Vertex::Component::Position2, mesh::Vertex::DataType::Float, false);
		groupedFirst->createAttribute(mesh::Vertex::Component::UserDefined2, "USER_A", mesh::Vertex::DataType::Float, false);
		mesh::MeshSpecification splitLayout;
		auto* splitFirst = splitLayout.createVertexBufferAttributeLayout(false);
		splitFirst->createAttribute(mesh::Vertex::Component::Position2, mesh::Vertex::DataType::Float, false);
		auto* splitSecond = splitLayout.createVertexBufferAttributeLayout(false);
		splitSecond->createAttribute(mesh::Vertex::Component::UserDefined2, "USER_A", mesh::Vertex::DataType::Float, false);
		if (groupedLayout.getHashString() == splitLayout.getHashString()) return fail("mesh identity omitted vertex-buffer layout grouping");

		GraphImageDesc colour;
		colour.format = GraphImageFormat::Rgba16f;
		colour.usage = GraphImageUsage::ColourAttachment | GraphImageUsage::Sampled;
		if (colour.shape != GraphImageShape::Texture2D) return fail("existing graph image descriptors no longer default to 2D");

		GraphImageDesc cubeDepth;
		cubeDepth.format = GraphImageFormat::Depth24;
		cubeDepth.shape = GraphImageShape::CubeMap;
		cubeDepth.absoluteSize = { 64, 64 };
		cubeDepth.usage = GraphImageUsage::DepthAttachment | GraphImageUsage::Sampled | GraphImageUsage::Exported;
		cubeDepth.depthCompare = true;
		RenderGraph cubeGraph;
		auto cube = cubeGraph.createImage("PointShadow", cubeDepth);
		for (uint32_t face = 0; face < 6; ++face) { auto pass = cubeGraph.addPass("Face" + std::to_string(face)); cube = cubeGraph.writeDepth(pass, cube, GraphLoadOp::Clear, GraphStoreOp::Store, 0.1f * (face + 1), 0, face); }
		if (!cubeGraph.compile().valid || cubeGraph.getPassInfo({ 5 }).depthOutputs.front().cubeFace != 5 || cubeGraph.describe().find("face 5") == std::string::npos) return fail("depth cubemap face writes were not compiled or described");
		bool rejectedFace = false; try { auto pass = cubeGraph.addPass("BadFace"); cubeGraph.writeDepth(pass, cube, GraphLoadOp::Clear, GraphStoreOp::Store, 1.0f, 0, 6); } catch (...) { rejectedFace = true; }
		if (!rejectedFace) return fail("out-of-range cubemap face was accepted");
		bool rejectedMissingFace = false; try { RenderGraph graph; auto image = graph.createImage("Cube", cubeDepth); graph.writeDepth(graph.addPass("MissingFace"), image); } catch (...) { rejectedMissingFace = true; }
		if (!rejectedMissingFace) return fail("cubemap attachment without a selected face was accepted");
		bool rejected2dFace = false; try { RenderGraph graph; GraphImageDesc depth2d = cubeDepth; depth2d.shape = GraphImageShape::Texture2D; auto image = graph.createImage("Depth2D", depth2d); graph.writeDepth(graph.addPass("2DFace"), image, GraphLoadOp::Clear, GraphStoreOp::Store, 1.0f, 0, 0); } catch (...) { rejected2dFace = true; }
		if (!rejected2dFace) return fail("2D attachment accepted a cubemap face");
		bool rejectedShape = false; try { RenderGraph graph; auto invalid = cubeDepth; invalid.absoluteSize = { 64, 32 }; graph.createImage("NonSquareCube", invalid); } catch (...) { rejectedShape = true; }
		if (!rejectedShape) return fail("non-square cubemap descriptor was accepted");

		RenderGraph valid;
		auto scene = valid.createImage("Scene", colour);
		auto bloom = valid.createImage("Bloom", colour);
		auto scenePass = valid.addPass("Scene", GraphPassType::Scene);
		scene = valid.writeColour(scenePass, scene, GraphLoadOp::Clear);
		auto bloomPass = valid.addPass("Bloom", GraphPassType::Fullscreen);
		valid.readSampled(bloomPass, scene);
		valid.writeColour(bloomPass, bloom);
		auto result = valid.compile();
		if (!result.valid || result.passOrder.size() != 2) return fail("valid two-pass graph was rejected");if(valid.getImageVersionCount(scene.id)!=2)return fail("render graph image version inventory is incorrect");
		auto cacheAfterFirstCompile = valid.getPlanCacheStats();
		if (cacheAfterFirstCompile.compileMisses != 1) return fail("first graph compilation was not recorded as a cache miss");
		if (!valid.compile().valid || valid.getPlanCacheStats().compileHits <= cacheAfterFirstCompile.compileHits) return fail("unchanged graph compilation did not hit the plan cache");
		auto firstAllocation = valid.buildAllocationPlan({ 320, 180 });
		auto cacheAfterFirstAllocation = valid.getPlanCacheStats();
		auto repeatedAllocation = valid.buildAllocationPlan({ 320, 180 });
		if (!firstAllocation.valid || repeatedAllocation.allocatedImages.size() != firstAllocation.allocatedImages.size() || valid.getPlanCacheStats().allocationHits <= cacheAfterFirstAllocation.allocationHits)
			return fail("unchanged graph allocation did not hit the viewport plan cache");
		Caps artifactCaps; artifactCaps.maxTextureSize = 4096; artifactCaps.maxColourAttachments = 8; artifactCaps.maxDrawBuffers = 8;
		auto compiledArtifact = valid.buildCompiledPlan(artifactCaps, { 320, 180 });
		if (!compiledArtifact.valid || !compiledArtifact.compilation.valid || !compiledArtifact.allocation.valid || compiledArtifact.compilation.passOrder.size() != 2)
			return fail("combined immutable compilation/allocation artifact is incomplete");
		auto cacheBeforeEdit = valid.getPlanCacheStats();
		auto editedDesc=valid.getImageInfo({0,0}).desc;editedDesc.mipLevels=2;valid.setImageDesc({0,0},editedDesc);if(valid.getImageInfo({0,0}).desc.mipLevels!=2)return fail("graph image descriptor edit was not retained");
		if (!valid.compile().valid || valid.getPlanCacheStats().compileMisses <= cacheBeforeEdit.compileMisses || valid.getPlanCacheStats().invalidations <= cacheBeforeEdit.invalidations)
			return fail("graph edit did not invalidate the cached compilation");
		auto rebuiltAllocation = valid.buildAllocationPlan({ 320, 180 });
		if (!rebuiltAllocation.valid || rebuiltAllocation.allocatedImages.empty() || rebuiltAllocation.allocatedImages.front().desc.mipLevels != 2 || valid.getPlanCacheStats().allocationMisses <= cacheBeforeEdit.allocationMisses)
			return fail("graph edit did not invalidate the cached allocation plan");
		auto cacheBeforeResize = valid.getPlanCacheStats();
		auto resizedAllocation = valid.buildAllocationPlan({ 321, 180 });
		if (!resizedAllocation.valid || valid.getPlanCacheStats().allocationMisses <= cacheBeforeResize.allocationMisses) return fail("new graph viewport reused an allocation plan for different dimensions");
		RenderGraph cacheCopy(valid);
		if (cacheCopy.getPlanCacheStats().compileHits || cacheCopy.getPlanCacheStats().compileMisses || !cacheCopy.compile().valid || cacheCopy.getPlanCacheStats().compileMisses != 1)
			return fail("RenderGraph copy inherited another graph's plan cache");
		RenderGraph copied(valid);copied.setPassEnabled(scenePass,false);if(valid.getPassInfo(scenePass).enabled==copied.getPassInfo(scenePass).enabled||copied.getPassCount()!=valid.getPassCount())return fail("deep RenderGraph copy is not independent");RenderGraph assigned;assigned=valid;if(assigned.getPassCount()!=valid.getPassCount()||!assigned.compile().valid)return fail("RenderGraph copy assignment lost topology");
		RenderGraph structural(valid);structural.setPassName({0},"SceneRenamed");structural.setImageName({0,0},"SceneTarget");auto duplicate=structural.duplicatePass({1},"BloomCopy");if(structural.getPassCount()!=3||structural.getPassInfo(duplicate).colourOutputs.empty())return fail("render graph pass duplication failed");structural.movePass(duplicate,1);if(structural.getPassInfo({1}).name!="BloomCopy")return fail("render graph pass move failed");structural.removePass({1});if(structural.getPassCount()!=2||structural.getImageVersionCount(1)!=2)return fail("render graph pass removal did not clean produced values");structural.removeImage({1,0});if(structural.getImageCount()!=1||!structural.getPassInfo({1}).colourOutputs.empty())return fail("render graph image removal did not clean pass references");
		RenderGraph attachments;auto attachmentA=attachments.createImage("A",colour),attachmentB=attachments.createImage("B",colour),attachmentOut=attachments.createImage("Out",colour);auto attachmentWriter=attachments.addPass("Writer");attachmentA=attachments.writeColour(attachmentWriter,attachmentA);attachments.setValueId(attachmentA,"Stable.Attachment");auto attachmentReader=attachments.addPass("Reader");attachments.bindSampler(attachmentReader,"TEX",attachmentA);attachments.writeColour(attachmentReader,attachmentOut);auto replacement=attachments.retargetColourOutput(attachmentWriter,0,attachmentB);if(attachments.getValueId(replacement)!="Stable.Attachment"||attachments.getPassInfo(attachmentReader).samplerBindings[0].image.id!=attachmentB.id||!attachments.compile().valid)return fail("attachment retargeting did not preserve stable dependent references");attachments.removeColourOutput(attachmentWriter,0);if(!attachments.getPassInfo(attachmentReader).sampledInputs.empty())return fail("attachment removal did not clean sampled references");

		RenderGraph missingProducer;
		auto unwritten = missingProducer.createImage("Unwritten", colour);
		auto reader = missingProducer.addPass("Reader");
		missingProducer.readSampled(reader, unwritten);
		if (missingProducer.compile().valid) return fail("unwritten sampled image was accepted");

		RenderGraph feedback;
		auto feedbackImage = feedback.createImage("Feedback", colour);
		auto feedbackPass = feedback.addPass("FeedbackPass");
		auto written = feedback.writeColour(feedbackPass, feedbackImage);
		feedback.readSampled(feedbackPass, written);
		if (feedback.compile().valid) return fail("same-pass image feedback was accepted");

		// Loading a transient image that nothing produced earlier reads whatever the
		// allocator's aliasing left behind, so the result changes with an allocation
		// decision rather than with the graph.
		RenderGraph loadTransient;
		auto scratch = loadTransient.createImage("Scratch", colour);
		auto loader = loadTransient.addPass("Loader", GraphPassType::Fullscreen);
		loadTransient.writeColour(loader, scratch, GraphLoadOp::Load);
		if (loadTransient.compile().valid) return fail("loading an unproduced transient image was accepted");
		// The same load is well defined once an earlier pass has produced it.
		RenderGraph loadProduced;
		auto produced = loadProduced.createImage("Produced", colour);
		auto firstWriter = loadProduced.addPass("FirstWriter", GraphPassType::Fullscreen);
		produced = loadProduced.writeColour(firstWriter, produced, GraphLoadOp::Clear);
		auto secondWriter = loadProduced.addPass("SecondWriter", GraphPassType::Fullscreen);
		loadProduced.writeColour(secondWriter, produced, GraphLoadOp::Load);
		if (!loadProduced.compile().valid) return fail("loading a transient image produced by an earlier pass was rejected");
		// And on a non-transient image, whose contents the allocator must preserve.
		GraphImageDesc persistentColour = colour; persistentColour.transient = false;
		RenderGraph loadPersistent;
		auto kept = loadPersistent.createImage("Kept", persistentColour);
		auto keptLoader = loadPersistent.addPass("KeptLoader", GraphPassType::Fullscreen);
		loadPersistent.writeColour(keptLoader, kept, GraphLoadOp::Load);
		if (!loadPersistent.compile().valid) return fail("loading a non-transient image was rejected");
		// DontCare says the contents do not matter, which is exactly the honest
		// declaration for an unproduced transient image.
		RenderGraph dontCareTransient;
		auto ignored = dontCareTransient.createImage("Ignored", colour);
		auto ignoringPass = dontCareTransient.addPass("Ignoring", GraphPassType::Fullscreen);
		dontCareTransient.writeColour(ignoringPass, ignored, GraphLoadOp::DontCare);
		if (!dontCareTransient.compile().valid) return fail("a DontCare write to an unproduced transient image was rejected");

		valid.setValueId(scene, "Scene.AfterOpaque");
		if (valid.getValueId(scene) != "Scene.AfterOpaque" || valid.findValue("Scene.AfterOpaque").version != scene.version)
			return fail("stable graph value ID did not round-trip");

		RenderGraph outOfOrder;
		auto orderedImage = outOfOrder.createImage("Ordered", colour);
		auto orderedOutput = outOfOrder.createImage("OrderedOutput", colour);
		auto consumer = outOfOrder.addPass("Consumer", GraphPassType::Fullscreen);
		auto producer = outOfOrder.addPass("Producer", GraphPassType::Scene);
		orderedImage = outOfOrder.writeColour(producer, orderedImage);
		outOfOrder.setValueId(orderedImage, "Ordered.Produced");
		outOfOrder.readSampled(consumer, orderedImage);
		outOfOrder.writeColour(consumer, orderedOutput);
		auto automaticallyOrdered = outOfOrder.compile();
		if (!automaticallyOrdered.valid || automaticallyOrdered.passOrder.size() != 2 ||
			automaticallyOrdered.passOrder[0].id != producer.id || automaticallyOrdered.passOrder[1].id != consumer.id)
			return fail("normal compilation did not derive dependency order");
		if (std::none_of(automaticallyOrdered.messages.begin(), automaticallyOrdered.messages.end(), [&](auto const& message)
			{ return message.code == "RG-COMPILER-REORDERED-PASS" && message.pass.id == producer.id; }))
			return fail("dependency reordering did not emit compiler information");
		auto dependencyOrder = outOfOrder.buildDependencyOrder();
		if (!dependencyOrder.valid || dependencyOrder.passOrder.size() != 2 ||
			dependencyOrder.passOrder[0].id != producer.id || dependencyOrder.passOrder[1].id != consumer.id)
			return fail("stable dependency auto-order is incorrect");
		RenderGraph reordered(outOfOrder);reordered.reorderPasses(dependencyOrder.passOrder);if(!reordered.compile().valid||reordered.getPassInfo({0}).name!="Producer")return fail("explicit dependency pass reorder failed");

		outOfOrder.setPassEnabled(producer, false);
		if (outOfOrder.compile().valid) return fail("value produced by a disabled pass was accepted");
		if (outOfOrder.getPassInfo(producer).enabled) return fail("disabled pass state was not retained");

		RenderGraph loadOrdered;
		auto loadImage = loadOrdered.createImage("LoadOrdered", colour);
		auto loadConsumer = loadOrdered.addPass("LoadConsumer");
		auto loadProducer = loadOrdered.addPass("LoadProducer");
		loadImage = loadOrdered.writeColour(loadProducer, loadImage, GraphLoadOp::Clear);
		loadOrdered.writeColour(loadConsumer, loadImage, GraphLoadOp::Load);
		auto loadOrder = loadOrdered.compile();
		if (!loadOrder.valid || loadOrder.passOrder[0].id != loadProducer.id || loadOrder.passOrder[1].id != loadConsumer.id)
			return fail("attachment load dependency was not automatically ordered");

		RenderGraph compilerInformation;
		auto discardedImage = compilerInformation.createImage("Discarded", colour);
		auto discardedPass = compilerInformation.addPass("DiscardedPass");
		compilerInformation.writeColour(discardedPass, discardedImage, GraphLoadOp::Clear, GraphStoreOp::DontCare);
		auto retainedImage = compilerInformation.createImage("RetainedButUnused", colour);
		auto retainedPass = compilerInformation.addPass("RetainedPass");
		compilerInformation.writeColour(retainedPass, retainedImage, GraphLoadOp::Clear, GraphStoreOp::Store);
		auto compilerResult = compilerInformation.compile();
		if (!compilerResult.valid || compilerResult.passOrder.size() != 1 || compilerResult.passOrder.front().id != retainedPass.id ||
			compilerResult.culledPasses.size() != 1 || compilerResult.culledPasses.front().id != discardedPass.id)
			return fail("dead discard-only pass was not conservatively culled");
		if (compilerResult.unusedOutputs.size() != 2 || std::none_of(compilerResult.messages.begin(), compilerResult.messages.end(), [](auto const& message)
			{ return message.code == "RG-COMPILER-UNUSED-OUTPUT" && message.severity == GraphCompileMessageSeverity::Warning; }))
			return fail("unused output compiler diagnostics are incomplete");

		RenderGraph rootedGraph;
		auto liveImage = rootedGraph.createImage("Live", colour);
		auto deadImage = rootedGraph.createImage("Dead", colour);
		auto liveProducer = rootedGraph.addPass("LiveProducer");
		liveImage = rootedGraph.writeColour(liveProducer, liveImage);
		auto deadProducer = rootedGraph.addPass("DeadProducer");
		rootedGraph.writeColour(deadProducer, deadImage);
		auto presentationDesc = colour; presentationDesc.external = true; presentationDesc.transient = false;
		presentationDesc.usage = presentationDesc.usage | GraphImageUsage::Presentation;
		auto presentation = rootedGraph.createImage("Presentation", presentationDesc);
		auto presentationPass = rootedGraph.addPass("Presentation");
		rootedGraph.readSampled(presentationPass, liveImage);
		rootedGraph.writeColour(presentationPass, presentation);
		auto rootedResult = rootedGraph.compile();
		if (!rootedResult.valid || rootedResult.passOrder.size() != 2 || rootedResult.passOrder[0].id != liveProducer.id ||
			rootedResult.passOrder[1].id != presentationPass.id || rootedResult.culledPasses.size() != 1 || rootedResult.culledPasses.front().id != deadProducer.id)
			return fail("presentation-root dead-pass elimination did not retain exactly the contributing chain");
		RenderGraph importedInputGraph;
		auto importedDesc = colour; importedDesc.external = true; importedDesc.transient = false;
		auto importedInput = importedInputGraph.createImage("ImportedInput", importedDesc);
		auto importedOutput = importedInputGraph.createImage("ImportedOutput", colour);
		auto importedPass = importedInputGraph.addPass("ImportedConsumer");
		importedInputGraph.readSampled(importedPass, importedInput);
		importedInputGraph.writeColour(importedPass, importedOutput);
		auto importedResult = importedInputGraph.compile();
		if (!importedResult.valid || importedResult.passOrder.size() != 1 || importedResult.passOrder.front().id != importedPass.id)
			return fail("read-only external import incorrectly activated presentation-root culling");

		RenderGraphPassFactoryRegistry registry;
		registerBuiltInRenderGraphPasses(registry);
		if (!registry.findMetadata("MPP.PbrScene") || !registry.findMetadata("MPP.CustomFullscreen"))
			return fail("built-in pass authoring metadata was not registered");
		RenderGraph metadataGraph;
		auto metadataInput = metadataGraph.createImage("MetadataInput", colour);
		auto metadataProducer = metadataGraph.addPass("MetadataProducer");
		metadataInput = metadataGraph.writeColour(metadataProducer, metadataInput);
		metadataGraph.setPassEnabled(metadataProducer, false);
		auto metadataOutput = metadataGraph.createImage("MetadataOutput", colour);
		auto metadataPass = metadataGraph.addPass("MetadataBloom", GraphPassType::Fullscreen);
		metadataGraph.setPassCallbackFactory(metadataPass, "MPP.FullscreenEffect");
		metadataGraph.bindSampler(metadataPass, "TEX1", metadataInput);
		metadataGraph.writeColour(metadataPass, metadataOutput);
		if (registry.validate(metadataGraph).hasErrors()) return fail("valid pass authoring metadata contract was rejected");
		metadataGraph.setPassCallbackFactory(metadataPass, "Unknown.Factory");
		if (!registry.validate(metadataGraph).hasErrors()) return fail("unknown pass factory metadata was accepted");

		auto const* particleMetadata = registry.findMetadata("MPP.ParticleScene");
		if (!particleMetadata || particleMetadata->inputs.size() != 1 || particleMetadata->inputs.front().required ||
			particleMetadata->inputs.front().sampler != "DEPTH" || particleMetadata->inputs.front().fallbackId != "HardParticles")
			return fail("particle scene metadata lost its optional named depth fallback");
		auto const* meshParticleMetadata = registry.findMetadata("MPP.ParticleMeshScene");
		if (!meshParticleMetadata || meshParticleMetadata->inputs.size() != 1u ||
			meshParticleMetadata->inputs[0].sampler != "SHADOW_MAP" || meshParticleMetadata->inputs[0].required ||
			!meshParticleMetadata->allowAdditionalInputs || meshParticleMetadata->outputs.size() != 3u ||
			!meshParticleMetadata->outputs[2].depth || meshParticleMetadata->materialSlots.size() != 1u)
			return fail("mesh particle scene metadata lost its dedicated material/geometry pass contract");
		auto const* trailMetadata = registry.findMetadata("MPP.TrailScene");
		if (!trailMetadata || trailMetadata->inputs.size() != 1 || trailMetadata->inputs.front().required ||
			trailMetadata->inputs.front().sampler != "DEPTH" || trailMetadata->inputs.front().fallbackId != "HardTrails" ||
			trailMetadata->parameters.size() != 1)
			return fail("trail scene metadata lost its separate draw contract or optional depth fallback");
		auto const* volumetricLightingMetadata = registry.findMetadata("MPP.ParticleVolumetricLighting");
		if (!volumetricLightingMetadata || volumetricLightingMetadata->inputs.size() != 1u ||
			volumetricLightingMetadata->inputs[0].sampler != "DEPTH" || volumetricLightingMetadata->inputs[0].required ||
			volumetricLightingMetadata->inputs[0].fallbackId != "UnoccludedParticleVolumes" ||
			volumetricLightingMetadata->outputs.size() != 2u || !volumetricLightingMetadata->outputs[0].required ||
			volumetricLightingMetadata->outputs[1].required)
			return fail("particle volumetric-lighting metadata lost its optional depth and emissive-output contract");
		auto const* distortionMetadata = registry.findMetadata("MPP.ParticleDistortion");
		auto const* distortionCompositeMetadata = registry.findMetadata("MPP.ParticleDistortionComposite");
		if (!distortionMetadata || distortionMetadata->inputs.size() != 1 || distortionMetadata->inputs.front().required ||
			distortionMetadata->outputs.size() != 1 ||
			distortionMetadata->outputs.front().acceptedFormats != std::vector<GraphImageFormat>{ GraphImageFormat::Rg16f, GraphImageFormat::Rg32f } ||
			!distortionCompositeMetadata || distortionCompositeMetadata->inputs.size() != 2 || distortionCompositeMetadata->outputs.size() != 1)
			return fail("particle distortion draw/composite authoring metadata was not registered");
		RenderGraph distortionGraph;
		GraphImageDesc distortionSceneDesc = colour; distortionSceneDesc.external = true; distortionSceneDesc.transient = false;
		auto distortionScene = distortionGraph.createImage("DistortionScene", distortionSceneDesc);
		GraphImageDesc distortionDesc = colour; distortionDesc.format = GraphImageFormat::Rg16f;
		auto distortionVectors = distortionGraph.createImage("DistortionVectors", distortionDesc);
		auto distortionOutput = distortionGraph.createImage("DistortionOutput", colour);
		auto distortionPass = distortionGraph.addPass("ParticleDistortion", GraphPassType::Scene);
		distortionGraph.setPassCallbackFactory(distortionPass, "MPP.ParticleDistortion");
		distortionVectors = distortionGraph.writeColour(distortionPass, distortionVectors, GraphLoadOp::Clear);
		auto distortionCompositePass = distortionGraph.addPass("ParticleDistortionComposite", GraphPassType::Fullscreen);
		distortionGraph.setPassCallbackFactory(distortionCompositePass, "MPP.ParticleDistortionComposite");
		distortionGraph.bindSampler(distortionCompositePass, "SCENE", distortionScene);
		distortionGraph.bindSampler(distortionCompositePass, "DISTORTION", distortionVectors);
		distortionGraph.writeColour(distortionCompositePass, distortionOutput);
		if (registry.validate(distortionGraph).hasErrors() || !distortionGraph.compile().valid)
			return fail("valid particle distortion draw/composite graph was rejected");

		auto const* oitMetadata = registry.findMetadata("MPP.ParticleWeightedOit");
		auto const* oitResolveMetadata = registry.findMetadata("MPP.ParticleWeightedOitResolve");
		if (!oitMetadata || oitMetadata->inputs.size() != 1 || oitMetadata->outputs.size() != 2 ||
			oitMetadata->outputs[0].acceptedFormats != std::vector<GraphImageFormat>{ GraphImageFormat::Rgba16f, GraphImageFormat::Rgba32f } ||
			oitMetadata->outputs[1].acceptedFormats != std::vector<GraphImageFormat>{ GraphImageFormat::R16f, GraphImageFormat::R32f } ||
			!oitResolveMetadata || oitResolveMetadata->inputs.size() != 4 || oitResolveMetadata->outputs.size() != 2)
			return fail("weighted blended OIT accumulation/resolve authoring metadata was not registered");
		RenderGraph oitGraph;
		GraphImageDesc oitSceneDesc = colour; oitSceneDesc.external = true; oitSceneDesc.transient = false;
		auto oitScene = oitGraph.createImage("OitScene", oitSceneDesc);
		GraphImageDesc accumulationDesc = colour; accumulationDesc.format = GraphImageFormat::Rgba16f;
		GraphImageDesc opticalDepthDesc = colour; opticalDepthDesc.format = GraphImageFormat::R16f;
		auto oitAccumulation = oitGraph.createImage("OitAccumulation", accumulationDesc);
		auto oitOpticalDepth = oitGraph.createImage("OitOpticalDepth", opticalDepthDesc);
		auto oitComposite = oitGraph.createImage("OitComposite", colour);
		auto oitPass = oitGraph.addPass("ParticleWeightedOit", GraphPassType::Scene);
		oitGraph.setPassCallbackFactory(oitPass, "MPP.ParticleWeightedOit");
		oitAccumulation = oitGraph.writeColour(oitPass, oitAccumulation, GraphLoadOp::Clear);
		oitOpticalDepth = oitGraph.writeColour(oitPass, oitOpticalDepth, GraphLoadOp::Clear);
		auto oitResolvePass = oitGraph.addPass("ParticleWeightedOitResolve", GraphPassType::Fullscreen);
		oitGraph.setPassCallbackFactory(oitResolvePass, "MPP.ParticleWeightedOitResolve");
		oitGraph.bindSampler(oitResolvePass, "SCENE", oitScene);
		oitGraph.bindSampler(oitResolvePass, "ACCUMULATION", oitAccumulation);
		oitGraph.bindSampler(oitResolvePass, "OPTICAL_DEPTH", oitOpticalDepth);
		oitGraph.writeColour(oitResolvePass, oitComposite);
		if (registry.validate(oitGraph).hasErrors() || !oitGraph.compile().valid)
			return fail("valid weighted blended OIT accumulation/resolve graph was rejected");

		RenderGraph particleGraph;
		auto particleColour = particleGraph.createImage("ParticleColour", colour);
		auto particlePass = particleGraph.addPass("ParticleAlpha", GraphPassType::Scene);
		particleGraph.setPassCallbackFactory(particlePass, "MPP.ParticleScene");
		UniformCollection particleParameters;
		particleParameters.setUniform("BLEND_MODE", int32_t(ParticleBlendClass::Alpha));
		particleGraph.setPassParameters(particlePass, particleParameters);
		particleGraph.writeColour(particlePass, particleColour);
		GraphRasterState invalidParticleRaster;
		invalidParticleRaster.explicitState = true;
		invalidParticleRaster.depthWrite = true;
		particleGraph.setPassRasterState(particlePass, invalidParticleRaster);
		auto particleDiagnostics = registry.validate(particleGraph);
		if (particleDiagnostics.hasErrors() || particleDiagnostics.count(DiagnosticSeverity::Warning) != 1 ||
			particleDiagnostics.getDiagnostics().front().code != "MPP-PASS-014")
			return fail("particle depth-write request did not produce exactly the override warning");
		invalidParticleRaster.depthWrite = false;
		particleGraph.setPassRasterState(particlePass, invalidParticleRaster);
		if (registry.validate(particleGraph).hasErrors())
			return fail("particle pass without optional depth did not select its hard-particle fallback");
		particleGraph.setPassCallbackFactory(particlePass, "MPP.TrailScene");
		if (registry.validate(particleGraph).hasErrors())
			return fail("trail pass without optional depth did not select its hard-trail fallback");
		invalidParticleRaster.depthWrite = true;
		particleGraph.setPassRasterState(particlePass, invalidParticleRaster);
		auto trailDiagnostics = registry.validate(particleGraph);
		if (trailDiagnostics.hasErrors() || trailDiagnostics.count(DiagnosticSeverity::Warning) != 1 ||
			trailDiagnostics.getDiagnostics().front().code != "MPP-PASS-014")
			return fail("trail depth-write request did not produce exactly the transparent-primitive override warning");

		// A non-transient image holds contents that outlive the frame, so nothing may
		// be planned on top of it. The plan used to test only the incoming image for
		// transience and not the allocation it was joining, so a transient image with
		// a compatible descriptor and a disjoint lifetime landed on one. The real
		// allocator refuses this, so the damage was confined to the plan -- but
		// PipelineEditor reports physicalAllocation and estimatedPhysicalBytes
		// straight from it, and any future consumer inherits a corruption bug.
		GraphImageDesc persistent = colour; persistent.transient = false;
		RenderGraph aliasing;
		auto keep = aliasing.createImage("Keep", persistent);
		auto scratchA = aliasing.createImage("ScratchA", colour);
		auto scratchB = aliasing.createImage("ScratchB", colour);
		auto scratchC = aliasing.createImage("ScratchC", colour);
		auto aliasStep0 = aliasing.addPass("Alias0", GraphPassType::Fullscreen);
		keep = aliasing.writeColour(aliasStep0, keep);
		auto aliasStep1 = aliasing.addPass("Alias1", GraphPassType::Fullscreen);
		aliasing.readSampled(aliasStep1, keep); scratchA = aliasing.writeColour(aliasStep1, scratchA);
		auto aliasStep2 = aliasing.addPass("Alias2", GraphPassType::Fullscreen);
		aliasing.readSampled(aliasStep2, scratchA); scratchB = aliasing.writeColour(aliasStep2, scratchB);
		auto aliasStep3 = aliasing.addPass("Alias3", GraphPassType::Fullscreen);
		aliasing.readSampled(aliasStep3, scratchB); aliasing.writeColour(aliasStep3, scratchC);
		auto aliasingPlan = aliasing.buildAllocationPlan({ 32, 32 });
		if (!aliasingPlan.valid || aliasingPlan.allocatedImages.size() != 4) return fail("transient aliasing plan did not compile");
		auto allocationOf = [&](GraphImageHandle const& image)
		{
			for (auto const& lifetime : aliasingPlan.allocatedImages) if (lifetime.image.id == image.id) return lifetime.physicalAllocation;
			return UINT32_MAX;
		};
		// Keep lives over passes 0..1 and ScratchB over 2..3, so their lifetimes are
		// disjoint and only transience keeps them apart.
		if (allocationOf(keep) == allocationOf(scratchB))
			return fail("a transient graph image was planned on top of a non-transient allocation");
		// ScratchA (1..2) and ScratchC (3..3) are disjoint and both transient, so the
		// fix must not have simply stopped aliasing altogether.
		if (allocationOf(scratchA) != allocationOf(scratchC))
			return fail("disjoint transient graph images were not aliased onto one allocation");
		uint64_t distinctBytes = 0;
		for (auto const& lifetime : aliasingPlan.allocatedImages) if (lifetime.image.id != scratchC.id) distinctBytes += lifetime.estimatedBytes;
		if (aliasingPlan.estimatedPhysicalBytes != distinctBytes)
			return fail("planned physical byte estimate does not match its own allocation groups");

		// The plan and RenderGraphTargets used to carry separate compatibility
		// predicates, and the plan's ignored six sampler fields plus usage. Both now
		// call graphImagesCanAlias, so a descriptor difference the real allocator
		// respects must stop the plan grouping too. Three ends of the same graph:
		// identical descriptors alias, a sampler difference does not, a usage
		// difference does not.
		auto planEndImagesTogether = [&](GraphImageDesc const& tailDesc)
		{
			RenderGraph graph;
			auto head = graph.createImage("Head", colour);
			auto middle = graph.createImage("Middle", colour);
			auto tail = graph.createImage("Tail", tailDesc);
			auto step0 = graph.addPass("Step0", GraphPassType::Fullscreen);
			head = graph.writeColour(step0, head);
			auto step1 = graph.addPass("Step1", GraphPassType::Fullscreen);
			graph.readSampled(step1, head); middle = graph.writeColour(step1, middle);
			auto step2 = graph.addPass("Step2", GraphPassType::Fullscreen);
			graph.readSampled(step2, middle); graph.writeColour(step2, tail);
			auto plan = graph.buildAllocationPlan({ 32, 32 });
			if (!plan.valid) return false;
			uint32_t headAllocation = UINT32_MAX, tailAllocation = UINT32_MAX;
			for (auto const& lifetime : plan.allocatedImages)
			{
				if (lifetime.image.id == head.id) headAllocation = lifetime.physicalAllocation;
				if (lifetime.image.id == tail.id) tailAllocation = lifetime.physicalAllocation;
			}
			return headAllocation != UINT32_MAX && headAllocation == tailAllocation;
		};
		if (!planEndImagesTogether(colour))
			return fail("identical disjoint graph images were not planned onto one allocation");
		GraphImageDesc biased = colour; biased.params.lodBias = 1.5f;
		if (planEndImagesTogether(biased))
			return fail("graph images differing only in sampler LOD bias were planned onto one allocation");
		GraphImageDesc unsampled = colour; unsampled.usage = GraphImageUsage::ColourAttachment;
		if (planEndImagesTogether(unsampled))
			return fail("graph images differing in declared usage were planned onto one allocation");

		// compile(Caps) used to contain an empty per-image loop, so no image was
		// checked against device limits and PbrPipelineDocument::validate reported a
		// pipeline as valid that then threw at allocatePhysical on the device.
		Caps limitedCaps{}; limitedCaps.maxTextureSize = 256; limitedCaps.maxColourAttachments = 4; limitedCaps.maxDrawBuffers = 4;
		auto compileWithImage = [&](GraphImageDesc const& desc, glm::uvec2 const& viewport)
		{
			RenderGraph graph;
			auto image = graph.createImage("Limited", desc);
			auto pass = graph.addPass("LimitedPass", GraphPassType::Fullscreen);
			graph.writeColour(pass, image);
			return viewport.x ? graph.compile(limitedCaps, viewport) : graph.compile(limitedCaps);
		};
		GraphImageDesc oversized = colour; oversized.absoluteSize = { 512, 512 }; oversized.relativeSize = { 0.0f, 0.0f };
		if (compileWithImage(oversized, {}).valid)
			return fail("an image larger than the maximum texture size compiled against caps");
		GraphImageDesc sized = colour; sized.absoluteSize = { 64, 64 }; sized.relativeSize = { 0.0f, 0.0f };
		if (!compileWithImage(sized, {}).valid)
			return fail("an image within the maximum texture size was rejected");
		GraphImageDesc overMipped = sized; overMipped.mipLevels = 12;
		if (compileWithImage(overMipped, {}).valid)
			return fail("an image declaring more mip levels than its size supports compiled against caps");
		// The whole point of the viewport overload: a relative image is unresolvable
		// without one, so the no-viewport call must accept it and the viewport call
		// must catch it.
		GraphImageDesc relative = colour; relative.absoluteSize = { 0, 0 }; relative.relativeSize = { 1.0f, 1.0f };
		if (!compileWithImage(relative, {}).valid)
			return fail("a viewport-relative image was rejected without a viewport to resolve it");
		if (compileWithImage(relative, { 512, 512 }).valid)
			return fail("a viewport-relative image exceeding the maximum texture size compiled against caps");
		if (!compileWithImage(relative, { 128, 128 }).valid)
			return fail("a viewport-relative image within the maximum texture size was rejected");

		RenderGraph capabilityCached;
		auto capabilityImage = capabilityCached.createImage("CapabilityCached", relative);
		auto capabilityPass = capabilityCached.addPass("CapabilityPass", GraphPassType::Fullscreen);
		capabilityCached.writeColour(capabilityPass, capabilityImage);
		if (capabilityCached.compile(limitedCaps, { 512, 512 }).valid) return fail("capability cache test did not reject the limited device");
		auto cacheAfterLimitedCompile = capabilityCached.getPlanCacheStats();
		if (capabilityCached.compile(limitedCaps, { 512, 512 }).valid || capabilityCached.getPlanCacheStats().compileHits <= cacheAfterLimitedCompile.compileHits)
			return fail("unchanged capability compilation did not hit the plan cache");
		auto largerCaps = limitedCaps; largerCaps.maxTextureSize = 1024;
		auto cacheBeforeLargerDevice = capabilityCached.getPlanCacheStats();
		if (!capabilityCached.compile(largerCaps, { 512, 512 }).valid || capabilityCached.getPlanCacheStats().compileMisses <= cacheBeforeLargerDevice.compileMisses)
			return fail("capability compilation reused a plan for a different device signature");

		return true;
	}
}
