// Native Vulkan implementation of the frozen BasicMaterial public API (#95).
// CPU-side behaviour (program resolution through the resource manager,
// uniform collection, texture option mapping) is retained from the
// reference. Uniform values reach the shader through the program's native
// per-frame-context uniform buffers behind descriptor sets — no GL uniform
// state. Texture realization is not delivered: a program declaring samplers
// fails its own load before texture resolution runs.
#include <cassert>
#include <cstring>
#include <format>

#include "utils/FileSystem.h"

#include "mpp/BasicMaterial.h"
#include "mpp/BasicMaterialStream.h"
#include "mpp/RenderSystem.h"
#include "mpp/ResourceManager.h"
#include "mpp/String.h"
#include "mpp/MppException.h"

using namespace std;

namespace mpp
{
	/*
	* Constructor.
	*
	*/
	BasicMaterial::BasicMaterial(string const& name, RenderSystem* renderSystem, ResourceManager* resourceMgr, ResourceStreamPtr resourceStream)
		: Material(name, "BasicMaterial", renderSystem, resourceMgr, resourceStream)
	{
	}

	/*
	 * Destructor
	 *
	 */
	BasicMaterial::~BasicMaterial()
	{
		destroy();
	}

	/*
	 * Create material.
	 *
	 */
	void BasicMaterial::createImpl()
	{
		BasicMaterialStream* mStr = dynamic_cast<BasicMaterialStream*>(getResourceStream().get());
		if (!mStr)
		{
			THROW_MPP("Could not cast to type 'BasicMaterialStream'", __LINE__, __FILE__, __func__);
		}

		auto resourceMgr = getResourceManager();

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
			case BasicMaterialSpecification::ProgramOptions::Shader::Type::Default:
				vertexShaderSrc = "";
				break;

			case BasicMaterialSpecification::ProgramOptions::Shader::Type::File:
				vertexShaderSrc = utils::FileSystem::readTextFile(progOpts.vertexShader.data);
				break;

			case BasicMaterialSpecification::ProgramOptions::Shader::Type::Resource:
				vertexShaderSrc = dynamic_cast<String*>(getResourceManager()->getResource(progOpts.vertexShader.data).get())->getData();
				break;

			default:
				THROW_MPP("Unknown BasicMaterialStream::ProgramOptions::Shader::Type", __LINE__, __FILE__, __func__);
			}

			string geometryShaderSrc;
			switch (progOpts.geometryShader.type)
			{
			case BasicMaterialSpecification::ProgramOptions::Shader::Type::Default:
				geometryShaderSrc = "";
				break;

			case BasicMaterialSpecification::ProgramOptions::Shader::Type::File:
				geometryShaderSrc = utils::FileSystem::readTextFile(progOpts.geometryShader.data);
				break;

			case BasicMaterialSpecification::ProgramOptions::Shader::Type::Resource:
				geometryShaderSrc = dynamic_cast<String*>(getResourceManager()->getResource(progOpts.geometryShader.data).get())->getData();
				break;

			default:
				THROW_MPP("Unknown BasicMaterialStream::ProgramOptions::Shader::Type", __LINE__, __FILE__, __func__);
			}

			string fragmentShaderSrc;
			switch (progOpts.fragmentShader.type)
			{
			case BasicMaterialSpecification::ProgramOptions::Shader::Type::Default:
				fragmentShaderSrc = "";
				break;

			case BasicMaterialSpecification::ProgramOptions::Shader::Type::File:
				fragmentShaderSrc = utils::FileSystem::readTextFile(progOpts.fragmentShader.data);
				break;

			case BasicMaterialSpecification::ProgramOptions::Shader::Type::Resource:
				fragmentShaderSrc = dynamic_cast<String*>(getResourceManager()->getResource(progOpts.fragmentShader.data).get())->getData();
				break;

			default:
				THROW_MPP("Unknown BasicMaterialStream::ProgramOptions::Shader::Type", __LINE__, __FILE__, __func__);
			}

			// Get or create program, either with default shaders or loaded strings in ProgOpts.
			// The retained default-shader templates compile through the same
			// Vulkan toolchain; templates using undelivered features fail there
			// with named diagnostics.
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

		// Set uniforms and the explicitly authored generic shadow-caster policy.
		mUniforms = mStr->getUniforms();
		mShadowCaster = mStr->getShadowCasterContract();
		// Set textures
		Program* program = (Program*)(mProgram.get());
		auto const& materialTextures = mStr->getTextures();

		// Go through each texture, get the binding location.
		for (int i = 0; i < program->getNumSamplers(); ++i)
		{
			string const& samplerName = program->getSamplerName(i);

			auto it = find_if(materialTextures.begin(), materialTextures.end(),
				[samplerName](BasicMaterialSpecification::TextureOptions const& textureOptions)
			{
				return textureOptions.sampler == samplerName;
			});

			string textureName;
			if (it == materialTextures.end())
			{
				// The neutral/fallback texture resources referenced here are
				// core resources; the core-resource set is not delivered in
				// this subset, so these lookups fail actionably by name.
				if (samplerName == "SHADOW_MAP" || samplerName == "POINT_SHADOW_MAP" ||
					samplerName.rfind("PASS_POINT_SHADOW_MAP_", 0) == 0)
				{
					textureName = "__mpp_tex_none__";
				}
				else if (samplerName == "PBR_BASE_COLOUR_MAP" || samplerName == "PBR_OCCLUSION_MAP") textureName = "__mpp_tex_pbr_white__";
				else if (samplerName == "PBR_METALLIC_ROUGHNESS_MAP") textureName = "__mpp_tex_pbr_metallic_roughness__";
				else if (samplerName == "PBR_NORMAL_MAP") textureName = "__mpp_tex_pbr_normal__";
				else if (samplerName == "PBR_EMISSIVE_MAP") textureName = "__mpp_tex_pbr_black__";
				else if (samplerName == "PBR_IRRADIANCE_MAP" || samplerName == "PBR_PREFILTERED_SPECULAR_MAP") textureName = "__mpp_tex_pbr_ibl_cube__";
				else if (samplerName == "PBR_BRDF_LUT") textureName = "__mpp_tex_pbr_brdf_lut__";
				else if (samplerName == "PBR_SCENE_COLOUR_RESOLVED" ||
					samplerName == "PBR_PLANAR_REFLECTION_0" ||
					samplerName == "PBR_PLANAR_REFLECTION_1" ||
					samplerName == "PBR_PLANAR_REFLECTION_2" ||
					samplerName == "PBR_PLANAR_REFLECTION_3") textureName = "__mpp_tex_pbr_black__";
				else if (samplerName == "PBR_SCENE_DEPTH") textureName = "__mpp_tex_pbr_white__";
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
	void BasicMaterial::destroyImpl()
	{
	}

	/*
	 * Load material: realize the program's native pipeline and dependent
	 * textures.
	 *
	 */
	void BasicMaterial::loadImpl()
	{
		mProgram->load();

		for (auto texture: mTextures)
		{
			texture->load();
		}
	}

	/*
	 * Unload material. Retained reference behaviour: the dependent program is
	 * released at destroy time, not here.
	 *
	 */
	void BasicMaterial::unloadImpl()
	{
	}

	ResourcePtr BasicMaterial::getProgram()
	{
		return mProgram;
	}

	int BasicMaterial::getNumTextures() const
	{
		return (int)mTextures.size();
	}

	void BasicMaterial::setTexture(int i, ResourcePtr texture)
	{
		assert(i >= 0 && i < getNumTextures());
		mTextures[i] = texture;
	}

	ResourcePtr BasicMaterial::getTexture(int i) const
	{
		return mTextures[i];
	}

	/*
	 * Write the material's uniform values into the program's native uniform
	 * storage (staged into every frame context's descriptor-backed buffer).
	 *
	 */
	void BasicMaterial::setUniforms()
	{
		mUniforms.bindUniforms(mProgram);
	}
}
