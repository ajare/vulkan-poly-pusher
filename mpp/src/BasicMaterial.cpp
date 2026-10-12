#include <format>
#include <cstring>

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
				if (samplerName == "SHADOW_MAP" || samplerName == "POINT_SHADOW_MAP" ||
					samplerName.rfind("PASS_POINT_SHADOW_MAP_", 0) == 0)
				{
					// Shadow domains and explicit ScenePassOverrides replace these bindings
					// during an opted-in scene flush. The normal no-texture fallback keeps
					// materials valid when that pass-scoped state is absent.
					textureName = "__mpp_tex_none__";
				}
				// A BasicMaterial stays generic, but a custom basic shader may opt into
				// individual canonical PBR inputs. Supply neutral resources so it can
				// execute in a PBR pipeline without becoming a PbrMaterial.
				else if (samplerName == "PBR_BASE_COLOUR_MAP" || samplerName == "PBR_OCCLUSION_MAP") textureName = "__mpp_tex_pbr_white__";
				else if (samplerName == "PBR_METALLIC_ROUGHNESS_MAP") textureName = "__mpp_tex_pbr_metallic_roughness__";
				else if (samplerName == "PBR_NORMAL_MAP") textureName = "__mpp_tex_pbr_normal__";
				else if (samplerName == "PBR_EMISSIVE_MAP") textureName = "__mpp_tex_pbr_black__";
				else if (samplerName == "PBR_IRRADIANCE_MAP" || samplerName == "PBR_PREFILTERED_SPECULAR_MAP") textureName = "__mpp_tex_pbr_ibl_cube__";
				else if (samplerName == "PBR_BRDF_LUT") textureName = "__mpp_tex_pbr_brdf_lut__";
				// Render-graph water inputs are pipeline-owned just like IBL and
				// shadows. Neutral material resources keep a generic shader valid in
				// a pipeline without a water pass; MPP.WaterScene replaces them.
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
	 * Load material.
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
	 * Unload material.
	 *
	 */
	void BasicMaterial::unloadImpl()
	{
	}

	/*
	 * Get the program this material uses.
	 *
	 */
	ResourcePtr BasicMaterial::getProgram()
	{
		return mProgram;
	}

	/*
	 * Get number of textures.
	 *
	 */
	int BasicMaterial::getNumTextures() const
	{
		return (int)mTextures.size();
	}

	void BasicMaterial::setTexture(int i, ResourcePtr texture)
	{
		assert(i >= 0 && i < getNumTextures());
		mTextures[i] = texture;
	}

	/*
	 * Get specified texture.
	 *
	 */
	ResourcePtr BasicMaterial::getTexture(int i) const
	{
		return mTextures[i];
	}
	/*
	 * Set uniforms.
	 *
	 */
	void BasicMaterial::setUniforms()
	{
		mUniforms.bindUniforms(mProgram);
	}
}