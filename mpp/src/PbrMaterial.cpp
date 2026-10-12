#include <format>
#include <cfloat>
#include <cstring>
#include <set>

#include "utils/FileSystem.h"

#include <GL/glew.h>

#include "mpp/PbrMaterial.h"
#include "mpp/PbrMaterialStream.h"
#include "mpp/PbrShaders.h"
#include "mpp/RenderSystem.h"
#include "mpp/ResourceManager.h"
#include "mpp/String.h"
#include "mpp/MppException.h"
#include "mpp/GLErrorCheck.h"

using namespace std;

namespace mpp
{
	/*
	* Constructor.
	*
	*/
	PbrMaterial::PbrMaterial(string const& name, RenderSystem* renderSystem, ResourceManager* resourceMgr, ResourceStreamPtr resourceStream)
		: Material(name, "PbrMaterial", renderSystem, resourceMgr, resourceStream)
	{
	}

	/*
	 * Destructor
	 *
	 */
	PbrMaterial::~PbrMaterial()
	{
		destroy();
	}

	/*
	 * Create material.
	 *
	 */
	void PbrMaterial::createImpl()
	{
		PbrMaterialStream* mStr = dynamic_cast<PbrMaterialStream*>(getResourceStream().get());
		if (!mStr)
		{
			THROW_MPP("Could not cast to type 'PbrMaterialStream'", __LINE__, __FILE__, __func__);
		}

		auto resourceMgr = getResourceManager();

		// Validate and specialize from the material definition before
		// choosing or compiling a program.
		mUniforms = mStr->getUniforms();
		mPbrSurface = mStr->getPbrSurface();
		mPbrSurface.enabled = true;
		auto requireRange = [&](char const* field, float value, float minimum, float maximum)
		{
			if (value < minimum || value > maximum)
				THROW_MPP("PbrMaterial '" + getName() + "' has " + field + " outside its supported range.", __LINE__, __FILE__, __func__);
		};
		requireRange("baseColourFactor alpha", mPbrSurface.baseColourFactor.a, 0.0f, 1.0f);
		requireRange("metallicFactor", mPbrSurface.metallicFactor, 0.0f, 1.0f);
		requireRange("roughnessFactor", mPbrSurface.roughnessFactor, 0.0f, 1.0f);
		requireRange("normalScale", mPbrSurface.normalScale, 0.0f, FLT_MAX);
		requireRange("occlusionStrength", mPbrSurface.occlusionStrength, 0.0f, 1.0f);
		requireRange("alphaCutoff", mPbrSurface.alphaCutoff, 0.0f, 1.0f);
		if (mPbrSurface.baseColourFactor.r < 0.0f || mPbrSurface.baseColourFactor.g < 0.0f || mPbrSurface.baseColourFactor.b < 0.0f ||
			mPbrSurface.emissiveFactor.r < 0.0f || mPbrSurface.emissiveFactor.g < 0.0f || mPbrSurface.emissiveFactor.b < 0.0f)
			THROW_MPP("PbrMaterial '" + getName() + "' has negative colour or emissive factors.", __LINE__, __FILE__, __func__);
		mFeatures = derivePbrMaterialFeatures(mPbrSurface, mStr->getTextures(), mStr->usesLegacyFullContract());
		mFeatureSummary = describePbrMaterialFeatures(mFeatures);

		// Create program and build information about it.  Program is either a named resource, or a MeshSpecification with
		// optional shader strings.
		auto const& progOpts = mStr->getProgramOptions();

		if (progOpts.resourceExists)
		{
			if (progOpts.isChild)
			{
				mProgram = resourceMgr->getResource(getName() + "/Program");
			}
			else
			{
				mProgram = resourceMgr->getResource(progOpts.existingResource);
			}
		}
		else
		{
			// Get texture usage
			uint32_t programFlags{ 0 };

			if (!mStr->getTextures().empty())
			{
				programFlags |= MPP_PROGRAM_TAGS_TEXTURE;
			}

			// Load in shaders if required
			string vertexShaderSrc;
			switch (progOpts.vertexShader.type)
			{
			case PbrMaterialSpecification::ProgramOptions::Shader::Type::Default:
				vertexShaderSrc = "";
				break;

			case PbrMaterialSpecification::ProgramOptions::Shader::Type::File:
				vertexShaderSrc = utils::FileSystem::readTextFile(progOpts.vertexShader.data);
				break;

			case PbrMaterialSpecification::ProgramOptions::Shader::Type::Resource:
				vertexShaderSrc = dynamic_cast<String*>(getResourceManager()->getResource(progOpts.vertexShader.data).get())->getData();
				break;

			default:
				THROW_MPP("Unknown PbrMaterialStream::ProgramOptions::Shader::Type", __LINE__, __FILE__, __func__);
			}

			string geometryShaderSrc;
			switch (progOpts.geometryShader.type)
			{
			case PbrMaterialSpecification::ProgramOptions::Shader::Type::Default:
				geometryShaderSrc = "";
				break;

			case PbrMaterialSpecification::ProgramOptions::Shader::Type::File:
				geometryShaderSrc = utils::FileSystem::readTextFile(progOpts.geometryShader.data);
				break;

			case PbrMaterialSpecification::ProgramOptions::Shader::Type::Resource:
				geometryShaderSrc = dynamic_cast<String*>(getResourceManager()->getResource(progOpts.geometryShader.data).get())->getData();
				break;

			default:
				THROW_MPP("Unknown PbrMaterialStream::ProgramOptions::Shader::Type", __LINE__, __FILE__, __func__);
			}

			string fragmentShaderSrc;
			switch (progOpts.fragmentShader.type)
			{
			case PbrMaterialSpecification::ProgramOptions::Shader::Type::Default:
				fragmentShaderSrc = "";
				break;

			case PbrMaterialSpecification::ProgramOptions::Shader::Type::File:
				fragmentShaderSrc = utils::FileSystem::readTextFile(progOpts.fragmentShader.data);
				break;

			case PbrMaterialSpecification::ProgramOptions::Shader::Type::Resource:
				fragmentShaderSrc = dynamic_cast<String*>(getResourceManager()->getResource(progOpts.fragmentShader.data).get())->getData();
				break;

			default:
				THROW_MPP("Unknown PbrMaterialStream::ProgramOptions::Shader::Type", __LINE__, __FILE__, __func__);
			}

			// An omitted program selects the engine-owned metallic-roughness shader.
			if (progOpts.vertexShader.type == PbrMaterialSpecification::ProgramOptions::Shader::Type::Default) vertexShaderSrc = BuiltInPbrVertexShader;
			if (progOpts.fragmentShader.type == PbrMaterialSpecification::ProgramOptions::Shader::Type::Default) fragmentShaderSrc = BuiltInPbrFragmentShader;
			if (!hasPbrFeature(mFeatures, PbrMaterialFeature::LegacyFullContract))
				fragmentShaderSrc = injectPbrSpecializationDefines(fragmentShaderSrc, mFeatures);

			// Get or create program, either with built-in PBR shaders or loaded strings in ProgOpts.
			if (progOpts.is2d)
			{
				mProgram = resourceMgr->getDefault2dProgram(vertexShaderSrc, fragmentShaderSrc, progOpts.spec, programFlags, false);
			}
			else
			{
				mProgram = resourceMgr->getDefault3dProgram(vertexShaderSrc, fragmentShaderSrc, progOpts.spec, programFlags, false);
			}
		}

		acquireDependentResource(mProgram);
		mProgram->load();
		string const programLabel = "PBR [" + mFeatureSummary + "]: " + mProgram->getName();
		GL_CHECK(glObjectLabel(GL_PROGRAM, mProgram->getId(), -1, programLabel.c_str()));

		// The built-in and every custom PBR program share this stable material
		// contract. Optional maps use neutral textures, not optional interfaces.
		Program* program = static_cast<Program*>(mProgram.get());
		bool const legacyFullContract = hasPbrFeature(mFeatures, PbrMaterialFeature::LegacyFullContract);
		// Renderer-owned rather than material-owned: supplied from the bound
		// prefiltered cubemap, never authored, serialized, or overridable per
		// instance. It is deliberately *optional* in the contract. Shader source is
		// baked into binary .mppmodel assets, so requiring it would invalidate every
		// model built before it existed and turn a shading fix into an asset
		// migration. A program without it is reported below instead.
		static constexpr char const* prefilteredMaxLodUniform = "PBR_PREFILTERED_MAX_LOD";
		// PBR_SCENE_COLOUR_MAX_LOD is renderer-owned like PBR_PREFILTERED_MAX_LOD;
		// the rest are authored on PbrSurface::water.
		static vector<string> const waterUniformNames{ "PBR_SCENE_COLOUR_MAX_LOD",
			"PBR_WATER_DISTORTION_SCALE", "PBR_WATER_DISTORTION_STRENGTH", "PBR_WATER_SCROLL_SPEED",
			"PBR_WATER_MICRO_ROUGHNESS", "PBR_WATER_SSR_MAX_DISTANCE", "PBR_WATER_SSR_STEPS",
			"PBR_WATER_SSR_THICKNESS", "PBR_WATER_EDGE_FADE",
			"PBR_WATER_GRAZING_FALLBACK_START", "PBR_WATER_GRAZING_FALLBACK_END" };
		static vector<string> const waterSamplerNames{ "PBR_SCENE_COLOUR_RESOLVED", "PBR_SCENE_DEPTH",
			"PBR_WATER_NORMAL_MAP", "PBR_WATER_DETAIL_NORMAL_MAP" };
		vector<string> requiredUniforms{ "PBR_BASE_COLOUR_FACTOR" };
		if (legacyFullContract)
		{
			requiredUniforms.insert(requiredUniforms.end(), { "PBR_METALLIC_FACTOR", "PBR_ROUGHNESS_FACTOR", "PBR_EMISSIVE_FACTOR",
				"PBR_NORMAL_SCALE", "PBR_OCCLUSION_STRENGTH", "PBR_ALPHA_MODE", "PBR_ALPHA_CUTOFF", "PBR_DOUBLE_SIDED" });
		}
		else
		{
			if (hasPbrFeature(mFeatures, PbrMaterialFeature::Metallic)) requiredUniforms.push_back("PBR_METALLIC_FACTOR");
			if (hasPbrFeature(mFeatures, PbrMaterialFeature::Roughness)) requiredUniforms.push_back("PBR_ROUGHNESS_FACTOR");
			if (hasPbrFeature(mFeatures, PbrMaterialFeature::MetallicMap)) requiredUniforms.push_back("PBR_METALLIC_CHANNEL");
			if (hasPbrFeature(mFeatures, PbrMaterialFeature::RoughnessMap)) requiredUniforms.push_back("PBR_ROUGHNESS_CHANNEL");
			if (hasPbrFeature(mFeatures, PbrMaterialFeature::Emissive)) requiredUniforms.push_back("PBR_EMISSIVE_FACTOR");
			if (hasPbrFeature(mFeatures, PbrMaterialFeature::NormalMap)) requiredUniforms.push_back("PBR_NORMAL_SCALE");
			if (hasPbrFeature(mFeatures, PbrMaterialFeature::Occlusion)) requiredUniforms.push_back("PBR_OCCLUSION_STRENGTH");
			if (hasPbrFeature(mFeatures, PbrMaterialFeature::AlphaMask)) requiredUniforms.push_back("PBR_ALPHA_CUTOFF");
			// Water has no legacy asset population to protect, so unlike
			// PBR_PREFILTERED_MAX_LOD its renderer-owned LOD uniform is required
			// rather than warned about: a water program without it would silently
			// sample the wrong mip of the resolved scene colour.
			if (hasPbrFeature(mFeatures, PbrMaterialFeature::Water))
				requiredUniforms.insert(requiredUniforms.end(), waterUniformNames.begin(), waterUniformNames.end());
		}
		for (auto const& uniform : requiredUniforms)
		{
			if (program->getUniformId(uniform) < 0)
				THROW_MPP("PbrMaterial '" + getName() + "' program is missing required uniform '" + uniform + "'.", __LINE__, __FILE__, __func__);
			uint32_t expectedType = GL_FLOAT;
			if (uniform == "PBR_BASE_COLOUR_FACTOR") expectedType = GL_FLOAT_VEC4;
			else if (uniform == "PBR_EMISSIVE_FACTOR") expectedType = GL_FLOAT_VEC3;
			else if (uniform == "PBR_WATER_SCROLL_SPEED") expectedType = GL_FLOAT_VEC2;
			else if (uniform == "PBR_ALPHA_MODE" || uniform == "PBR_DOUBLE_SIDED" || uniform == "PBR_METALLIC_CHANNEL" || uniform == "PBR_ROUGHNESS_CHANNEL" || uniform == "PBR_WATER_SSR_STEPS") expectedType = GL_INT;
			if (program->getUniformGlType(uniform) != expectedType)
				THROW_MPP("PbrMaterial '" + getName() + "' uniform '" + uniform + "' has the wrong GLSL type.", __LINE__, __FILE__, __func__);
		}
		if (program->getUniformId(prefilteredMaxLodUniform) < 0)
			getRenderSystem()->warnMessage("PbrMaterial '" + getName() + "' program does not declare '" + prefilteredMaxLodUniform +
				"'. Its prefiltered specular fetch assumes a fixed mip count, so roughness will not map onto the bound cubemap's chain. "
				"Add the uniform to the shader and rebuild any .mppmodel that embeds it.");
		else if (program->getUniformGlType(prefilteredMaxLodUniform) != GL_FLOAT)
			THROW_MPP("PbrMaterial '" + getName() + "' uniform '" + prefilteredMaxLodUniform + "' has the wrong GLSL type.", __LINE__, __FILE__, __func__);
		set<string> allCoreUniforms = { "PBR_BASE_COLOUR_FACTOR", "PBR_METALLIC_FACTOR", "PBR_ROUGHNESS_FACTOR", "PBR_EMISSIVE_FACTOR",
			"PBR_NORMAL_SCALE", "PBR_OCCLUSION_STRENGTH", "PBR_ALPHA_MODE", "PBR_ALPHA_CUTOFF", "PBR_DOUBLE_SIDED", "PBR_METALLIC_CHANNEL", "PBR_ROUGHNESS_CHANNEL",
			prefilteredMaxLodUniform };
		allCoreUniforms.insert(waterUniformNames.begin(), waterUniformNames.end());
		set<string> allowedCoreUniforms(requiredUniforms.begin(), requiredUniforms.end());
		// Renderer-owned, so always permitted even though it is never required.
		allowedCoreUniforms.insert(prefilteredMaxLodUniform);
		if (!legacyFullContract) for (auto const& name : program->getUniformNames())
			if (allCoreUniforms.contains(name) && !allowedCoreUniforms.contains(name))
				THROW_MPP("PbrMaterial '" + getName() + "' program exposes specialized-out uniform '" + name + "' for [" + mFeatureSummary + "].", __LINE__, __FILE__, __func__);

		vector<string> requiredSamplers{ "PBR_IRRADIANCE_MAP", "PBR_PREFILTERED_SPECULAR_MAP", "PBR_BRDF_LUT" };
		if (legacyFullContract || hasPbrFeature(mFeatures, PbrMaterialFeature::BaseColourMap)) requiredSamplers.push_back("PBR_BASE_COLOUR_MAP");
		if (legacyFullContract || hasPbrFeature(mFeatures, PbrMaterialFeature::MetallicRoughnessMap)) requiredSamplers.push_back("PBR_METALLIC_ROUGHNESS_MAP");
		if (hasPbrFeature(mFeatures, PbrMaterialFeature::MetallicMap)) requiredSamplers.push_back("PBR_METALLIC_MAP");
		if (hasPbrFeature(mFeatures, PbrMaterialFeature::RoughnessMap)) requiredSamplers.push_back("PBR_ROUGHNESS_MAP");
		if (legacyFullContract || hasPbrFeature(mFeatures, PbrMaterialFeature::NormalMap)) requiredSamplers.push_back("PBR_NORMAL_MAP");
		if (legacyFullContract || hasPbrFeature(mFeatures, PbrMaterialFeature::Occlusion)) requiredSamplers.push_back("PBR_OCCLUSION_MAP");
		if (legacyFullContract || hasPbrFeature(mFeatures, PbrMaterialFeature::EmissiveMap)) requiredSamplers.push_back("PBR_EMISSIVE_MAP");
		if (hasPbrFeature(mFeatures, PbrMaterialFeature::Water))
			requiredSamplers.insert(requiredSamplers.end(), waterSamplerNames.begin(), waterSamplerNames.end());
		for (auto const& sampler : requiredSamplers)
		{
			bool found = false;
			for (int index = 0; index < program->getNumSamplers(); ++index)
				if (program->getSamplerName(index) == sampler) { found = true; break; }
			if (!found)
				THROW_MPP("PbrMaterial '" + getName() + "' program is missing required sampler '" + sampler + "'.", __LINE__, __FILE__, __func__);
			uint32_t const expectedType = sampler == "PBR_IRRADIANCE_MAP" || sampler == "PBR_PREFILTERED_SPECULAR_MAP" ? GL_SAMPLER_CUBE : GL_SAMPLER_2D;
			if (program->getSamplerGlType(sampler) != expectedType)
				THROW_MPP("PbrMaterial '" + getName() + "' sampler '" + sampler + "' has the wrong sampler type.", __LINE__, __FILE__, __func__);
		}
		set<string> allCoreSamplers = { "PBR_BASE_COLOUR_MAP", "PBR_METALLIC_ROUGHNESS_MAP", "PBR_NORMAL_MAP", "PBR_OCCLUSION_MAP",
			"PBR_EMISSIVE_MAP", "PBR_METALLIC_MAP", "PBR_ROUGHNESS_MAP", "PBR_IRRADIANCE_MAP", "PBR_PREFILTERED_SPECULAR_MAP", "PBR_BRDF_LUT" };
		allCoreSamplers.insert(waterSamplerNames.begin(), waterSamplerNames.end());
		set<string> const allowedCoreSamplers(requiredSamplers.begin(), requiredSamplers.end());
		if (!legacyFullContract) for (int index = 0; index < program->getNumSamplers(); ++index)
		{
			auto const& name = program->getSamplerName(index);
			if (allCoreSamplers.contains(name) && !allowedCoreSamplers.contains(name))
				THROW_MPP("PbrMaterial '" + getName() + "' program exposes specialized-out sampler '" + name + "' for [" + mFeatureSummary + "].", __LINE__, __FILE__, __func__);
		}

		string fragmentOutputDiagnostic;
		if (!program->validateFragmentOutputLocations(1, fragmentOutputDiagnostic))
			THROW_MPP("PbrMaterial '" + getName() + "' program must write fragment location 0: " + fragmentOutputDiagnostic, __LINE__, __FILE__, __func__);
		// Rebuild canonical runtime values from the exact selected interface.
		// File streams mirror all factors for serialization, but specialized-out
		// values must not remain legal instance overrides.
		for (auto const& name : allCoreUniforms) mUniforms.mUniformData.erase(name);
		mUniforms.mUniformData.erase("PBR_ENABLED");
		mUniforms.setUniform("PBR_BASE_COLOUR_FACTOR", mPbrSurface.baseColourFactor);
		if (legacyFullContract || hasPbrFeature(mFeatures, PbrMaterialFeature::Metallic)) mUniforms.setUniform("PBR_METALLIC_FACTOR", mPbrSurface.metallicFactor);
		if (legacyFullContract || hasPbrFeature(mFeatures, PbrMaterialFeature::Roughness)) mUniforms.setUniform("PBR_ROUGHNESS_FACTOR", mPbrSurface.roughnessFactor);
		auto channel = [&](char const* sampler) { auto const& textures = mStr->getTextures(); auto found = find_if(textures.begin(), textures.end(), [&](auto const& texture) { return texture.sampler == sampler; }); return (int32_t)(found == textures.end() ? 0 : found->channel); };
		if (hasPbrFeature(mFeatures, PbrMaterialFeature::MetallicMap)) mUniforms.setUniform("PBR_METALLIC_CHANNEL", channel("PBR_METALLIC_MAP"));
		if (hasPbrFeature(mFeatures, PbrMaterialFeature::RoughnessMap)) mUniforms.setUniform("PBR_ROUGHNESS_CHANNEL", channel("PBR_ROUGHNESS_MAP"));
		if (legacyFullContract || hasPbrFeature(mFeatures, PbrMaterialFeature::Emissive)) mUniforms.setUniform("PBR_EMISSIVE_FACTOR", mPbrSurface.emissiveFactor);
		if (legacyFullContract || hasPbrFeature(mFeatures, PbrMaterialFeature::NormalMap)) mUniforms.setUniform("PBR_NORMAL_SCALE", mPbrSurface.normalScale);
		if (legacyFullContract || hasPbrFeature(mFeatures, PbrMaterialFeature::Occlusion)) mUniforms.setUniform("PBR_OCCLUSION_STRENGTH", mPbrSurface.occlusionStrength);
		if (legacyFullContract) mUniforms.setUniform("PBR_ALPHA_MODE", (int32_t)mPbrSurface.alphaMode);
		if (legacyFullContract || hasPbrFeature(mFeatures, PbrMaterialFeature::AlphaMask)) mUniforms.setUniform("PBR_ALPHA_CUTOFF", mPbrSurface.alphaCutoff);
		if (legacyFullContract) mUniforms.setUniform("PBR_DOUBLE_SIDED", (int32_t)(mPbrSurface.doubleSided ? 1 : 0));
		if (hasPbrFeature(mFeatures, PbrMaterialFeature::Water))
		{
			auto const& water = mPbrSurface.water;
			// PBR_SCENE_COLOUR_MAX_LOD is deliberately absent: it belongs to the
			// resolved-scene texture the renderer binds, not to the material, and is
			// uploaded next to PBR_PREFILTERED_MAX_LOD at bind time.
			mUniforms.setUniform("PBR_WATER_DISTORTION_SCALE", water.distortionScale);
			mUniforms.setUniform("PBR_WATER_DISTORTION_STRENGTH", water.distortionStrength);
			mUniforms.setUniform("PBR_WATER_SCROLL_SPEED", water.scrollSpeed);
			mUniforms.setUniform("PBR_WATER_MICRO_ROUGHNESS", water.microRoughness);
			mUniforms.setUniform("PBR_WATER_SSR_MAX_DISTANCE", water.ssrMaxDistance);
			mUniforms.setUniform("PBR_WATER_SSR_STEPS", water.ssrSteps);
			mUniforms.setUniform("PBR_WATER_SSR_THICKNESS", water.ssrThickness);
			mUniforms.setUniform("PBR_WATER_EDGE_FADE", water.edgeFade);
			mUniforms.setUniform("PBR_WATER_GRAZING_FALLBACK_START", water.grazingFallbackStart);
			mUniforms.setUniform("PBR_WATER_GRAZING_FALLBACK_END", water.grazingFallbackEnd);
		}

		set<string> const coreUniforms(requiredUniforms.begin(), requiredUniforms.end());
		for (auto const& [name, value] : mUniforms.getUniformData())
		{
			MPP_UNUSED(value);
			if (coreUniforms.contains(name) || name == "PBR_ENABLED") continue;
			if (name.rfind("PBR_EXT_", 0) != 0)
				THROW_MPP("PbrMaterial '" + getName() + "' custom uniform '" + name + "' must use the PBR_EXT_ namespace.", __LINE__, __FILE__, __func__);
			if (program->getUniformId(name, value.count > 1 ? 0 : -1) < 0)
				THROW_MPP("PbrMaterial '" + getName() + "' declares extension uniform '" + name + "' which is absent from its program.", __LINE__, __FILE__, __func__);
			uint32_t expectedType = 0;
			if (value.type == program::GLSLType::Int) expectedType = value.numElements == 1 ? GL_INT : value.numElements == 2 ? GL_INT_VEC2 : value.numElements == 3 ? GL_INT_VEC3 : GL_INT_VEC4;
			else if (value.type == program::GLSLType::Uint) expectedType = value.numElements == 1 ? GL_UNSIGNED_INT : value.numElements == 2 ? GL_UNSIGNED_INT_VEC2 : value.numElements == 3 ? GL_UNSIGNED_INT_VEC3 : GL_UNSIGNED_INT_VEC4;
			else if (value.type == program::GLSLType::Float) expectedType = value.numElements == 1 ? GL_FLOAT : value.numElements == 2 ? GL_FLOAT_VEC2 : value.numElements == 3 ? GL_FLOAT_VEC3 : GL_FLOAT_VEC4;
			if (expectedType == 0 || program->getUniformGlType(name) != expectedType)
				THROW_MPP("PbrMaterial '" + getName() + "' extension uniform '" + name + "' does not match its reflected GLSL type.", __LINE__, __FILE__, __func__);
		}
		for (auto const& name : program->getUniformNames())
			if (name.rfind("PBR_EXT_", 0) == 0 && mUniforms.getUniformData().find(name) == mUniforms.getUniformData().end())
				THROW_MPP("PbrMaterial '" + getName() + "' program requires undeclared extension uniform '" + name + "'.", __LINE__, __FILE__, __func__);

		// Set textures
		auto const& materialTextures = mStr->getTextures();

		for (auto const& texture : materialTextures)
		{
			if (allCoreSamplers.contains(texture.sampler) || texture.sampler == "SHADOW_MAP") continue;
			if (texture.sampler.rfind("PBR_EXT_", 0) != 0)
				THROW_MPP("PbrMaterial '" + getName() + "' custom sampler '" + texture.sampler + "' must use the PBR_EXT_ namespace.", __LINE__, __FILE__, __func__);
			bool found = false;
			for (int index = 0; index < program->getNumSamplers(); ++index) if (program->getSamplerName(index) == texture.sampler) { found = true; break; }
			if (!found) THROW_MPP("PbrMaterial '" + getName() + "' declares extension sampler absent from its program: '" + texture.sampler + "'.", __LINE__, __FILE__, __func__);
			uint32_t const reflectedType = program->getSamplerGlType(texture.sampler);
			uint32_t const expectedType = texture.target == TextureTarget::CubeMap ? GL_SAMPLER_CUBE : GL_SAMPLER_2D;
			if (reflectedType != expectedType) THROW_MPP("PbrMaterial '" + getName() + "' extension sampler '" + texture.sampler + "' target does not match its reflected sampler type.", __LINE__, __FILE__, __func__);
		}
		for (int index = 0; index < program->getNumSamplers(); ++index)
		{
			auto const& name = program->getSamplerName(index);
			if (name.rfind("PBR_EXT_", 0) != 0) continue;
			bool declared = any_of(materialTextures.begin(), materialTextures.end(), [&](auto const& texture) { return texture.sampler == name; });
			if (!declared) THROW_MPP("PbrMaterial '" + getName() + "' program requires undeclared extension sampler '" + name + "'.", __LINE__, __FILE__, __func__);
		}

		// A serialized legacy stream may predate PbrSurface itself. The standard
		// PBR sampler contract is also sufficient to select its neutral maps.
		for (int i = 0; i < program->getNumSamplers(); ++i)
		{
			if (program->getSamplerName(i).rfind("PBR_", 0) == 0)
			{
				mPbrSurface.enabled = true;
				break;
			}
		}

		// Go through each texture, get the binding location.
		for (int i = 0; i < program->getNumSamplers(); ++i)
		{
			string const& samplerName = program->getSamplerName(i);

			auto it = find_if(materialTextures.begin(), materialTextures.end(),
				[samplerName] (PbrMaterialSpecification::TextureOptions const& textureOptions)
			{
				return textureOptions.sampler == samplerName;
			});

			string textureName;
			if (it == materialTextures.end())
			{
				if (samplerName == "SHADOW_MAP" || samplerName == "POINT_SHADOW_MAP")
				{
					// Shadow domains replace this binding during an opted-in scene flush.
					// The normal no-texture fallback keeps non-shadow pipelines valid.
					textureName = "__mpp_tex_none__";
				}
				else if (mPbrSurface.enabled)
				{
					if (samplerName == "PBR_BASE_COLOUR_MAP" || samplerName == "PBR_OCCLUSION_MAP")
					{
						textureName = "__mpp_tex_pbr_white__";
					}
					else if (samplerName == "PBR_METALLIC_ROUGHNESS_MAP")
					{
						textureName = "__mpp_tex_pbr_metallic_roughness__";
					}
					else if (samplerName == "PBR_NORMAL_MAP" || samplerName == "PBR_WATER_NORMAL_MAP" || samplerName == "PBR_WATER_DETAIL_NORMAL_MAP")
					{
						textureName = "__mpp_tex_pbr_normal__";
					}
					else if (samplerName == "PBR_SCENE_COLOUR_RESOLVED" ||
						samplerName == "PBR_PLANAR_REFLECTION_0" ||
						samplerName == "PBR_PLANAR_REFLECTION_1" ||
						samplerName == "PBR_PLANAR_REFLECTION_2" ||
						samplerName == "PBR_PLANAR_REFLECTION_3")
					{
						// Replaced by the water graph pass's resolved scene colour.
						// Black keeps a water material valid in a pipeline without
						// that pass, where every march is a miss anyway.
						textureName = "__mpp_tex_pbr_black__";
					}
					else if (samplerName == "PBR_SCENE_DEPTH")
					{
						// Depth 1.0 is the far plane, so the unbound case marches into
						// empty space and falls back to the cubemap rather than hitting
						// whatever a neutral colour texture happened to contain.
						textureName = "__mpp_tex_pbr_white__";
					}
					else if (samplerName == "PBR_EMISSIVE_MAP")
					{
						textureName = "__mpp_tex_pbr_black__";
					}
					else if (samplerName == "PBR_IRRADIANCE_MAP" || samplerName == "PBR_PREFILTERED_SPECULAR_MAP")
					{
						textureName = "__mpp_tex_pbr_ibl_cube__";
					}
					else if (samplerName == "PBR_BRDF_LUT")
					{
						textureName = "__mpp_tex_pbr_brdf_lut__";
					}
				}

				if (textureName.empty())
				{
					string errMsg = std::format("Sampler '{}' declared in program '{}' is not bound by material '{}'.",
						samplerName, program->getName(), getName());
					THROW_MPP(errMsg, __LINE__, __FILE__, __func__);
				}
			}
			else
			{
				auto const& textureOptions = *it;
				textureName = textureOptions.isChild
					? getName() + "/" + textureOptions.existingResource
					: textureOptions.existingResource;
			}

			// Add as acquired resource
			auto texRes = resourceMgr->getResource(textureName);
			acquireDependentResource(texRes);
			mTextures.push_back(texRes);
		}
	}

	/*
	 * Destroy material.
	 *
	 */
	void PbrMaterial::destroyImpl()
	{
	}

	/*
	 * Load material.
	 *
	 */
	void PbrMaterial::loadImpl()
	{
		mProgram->load();

		for (auto texture: mTextures)
		{
			texture->load();
		}
	}

	/*
	 * Unload material.
	 *
	 */
	void PbrMaterial::unloadImpl()
	{
	}

	/*
	 * Get the program this material uses.
	 *
	 */
	ResourcePtr PbrMaterial::getProgram()
	{
		return mProgram;
	}

	PbrMaterialSpecification::PbrSurface const& PbrMaterial::getSurface() const
	{
		return mPbrSurface;
	}

	PbrMaterialFeatures PbrMaterial::getFeatures() const
	{
		return mFeatures;
	}

	ShadowCasterContract makePbrShadowCasterContract(PbrMaterialSpecification::PbrSurface const& surface)
	{
		ShadowCasterContract contract;
		if (surface.alphaMode == PbrMaterialSpecification::PbrAlphaMode::Blend)
		{
			contract.behaviour = ShadowCasterContract::Behaviour::Disabled;
			return contract;
		}
		if (surface.alphaMode == PbrMaterialSpecification::PbrAlphaMode::Mask)
		{
			contract.behaviour = ShadowCasterContract::Behaviour::AlphaMask;
			contract.alphaSampler = "PBR_BASE_COLOUR_MAP";
			contract.alphaCutoff = surface.alphaCutoff;
			contract.alphaFactor = surface.baseColourFactor.a;
		}
		return contract;
	}

	ShadowCasterContract PbrMaterial::getShadowCasterContract() const
	{
		return makePbrShadowCasterContract(mPbrSurface);
	}

	string const& PbrMaterial::getFeatureSummary() const
	{
		return mFeatureSummary;
	}

	void PbrMaterial::validateInstanceUniforms(UniformCollection const& uniforms) const
	{
		auto const& declared = mUniforms.getUniformData();
		for (auto const& [name, value] : uniforms.getUniformData())
		{
			auto expected = declared.find(name);
			if (expected == declared.end())
				THROW_MPP("PbrMaterial '" + getName() + "' does not declare instance uniform '" + name + "'.", __LINE__, __FILE__, __func__);
			if (name.rfind("PBR_", 0) != 0)
				THROW_MPP("PbrMaterial instance uniform '" + name + "' must be a canonical PBR value or use the PBR_EXT_ namespace.", __LINE__, __FILE__, __func__);
			auto const& contract = expected->second;
			if (value.type != contract.type || value.count != contract.count || value.numElements != contract.numElements)
				THROW_MPP("PbrMaterial instance uniform '" + name + "' has a type or shape mismatch.", __LINE__, __FILE__, __func__);
			if (static_cast<Program*>(mProgram.get())->getUniformId(name) < 0)
				THROW_MPP("PbrMaterial program does not expose instance uniform '" + name + "'.", __LINE__, __FILE__, __func__);
		}
	}

	/*
	 * Get number of textures.
	 *
	 */
	int PbrMaterial::getNumTextures() const
	{
		return (int)mTextures.size();
	}

	void PbrMaterial::setTexture(int i, ResourcePtr texture)
	{
		assert(i >= 0 && i < getNumTextures());
		mTextures[i] = texture;
	}

	/*
	 * Get specified texture.
	 *
	 */
	ResourcePtr PbrMaterial::getTexture(int i) const
	{
		return mTextures[i];
	}
	/*
	 * Set uniforms.
	 *
	 */
	void PbrMaterial::setUniforms()
	{
		mUniforms.bindUniforms(mProgram);
	}
}