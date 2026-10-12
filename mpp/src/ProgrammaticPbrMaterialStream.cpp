#include "mpp/DefaultShaders.h"
#include "mpp/Program.h"
#include "mpp/ResourceManager.h"
#include "mpp/ProgrammaticPbrMaterialStream.h"
#include "mpp/PbrMaterialFeatures.h"
#include "mpp/ProgrammaticProgramStream.h"
#include "mpp/ProgrammaticTextureStream.h"
#include "mpp/MppException.h"

using namespace std;

namespace mpp
{
	namespace { void requireExtensionName(string const& name) { if (name.rfind("PBR_EXT_", 0) != 0) THROW_MPP("PBR extension names must use the PBR_EXT_ namespace.", __LINE__, __FILE__, __func__); } }

	ProgrammaticPbrMaterialStream::ProgrammaticPbrMaterialStream(ResourceManager* resourceMgr)
		: PbrMaterialStream(resourceMgr)
	{
	}

	void ProgrammaticPbrMaterialStream::createChildResourceStreamsImpl()
	{

		// Program
		if (mSpecification.program.resourceExists && mSpecification.program.isChild)
		{
			auto programStream = new ProgrammaticProgramStream(getResourceMgr());
			if (!mSpecification.legacyFullContract)
				programStream->setFragmentPreamble(makePbrSpecializationDefines(derivePbrMaterialFeatures(mSpecification.pbr, mSpecification.textures)));

			auto parser = make_shared<program::Parser>();

			parser->setMeshSpecification(mSpecification.program.spec);

			// Load source into parser
			if (mSpecification.program.vertexShader.type == PbrMaterialSpecification::ProgramOptions::Shader::Type::Default)
			{
				if (mSpecification.program.is2d)
				{
					parser->setVertexSource(VertexShader2dTemplate);
				}
				else
				{
					parser->setVertexSource(VertexShader3dTemplate);
				}
			}
			else
			{
				parser->setVertexSource(mSpecification.program.vertexShader.data);
			}

			if (mSpecification.program.fragmentShader.type == PbrMaterialSpecification::ProgramOptions::Shader::Type::Default)
			{
				if (mSpecification.program.is2d)
				{
					parser->setFragmentSource(FragmentShader2dTemplate);
				}
				else
				{
					parser->setFragmentSource(FragmentShader3dTemplate);
				}
			}
			else
			{
				parser->setFragmentSource(mSpecification.program.fragmentShader.data);
			}

			programStream->setParser(parser);
			addChild("Program", ResourceStreamPtr(programStream));
		}

		// Textures
		for (auto const& texture: mSpecification.textures)
		{
			if (texture.resourceExists && texture.isChild)
			{
				auto resMgr = getResourceMgr();
				auto textureStream = new ProgrammaticTextureStream(resMgr);

				textureStream->setParams(texture.params);
				textureStream->setSampler(texture.sampler);
				textureStream->setTarget(texture.target);
				textureStream->setFile(texture.source, resMgr->getImageLoadFunction());

				addChild(texture.existingResource, ResourceStreamPtr(textureStream));
			}
		}
	}

	void ProgrammaticPbrMaterialStream::setSpecification(PbrMaterialSpecification const& matSpec)
	{

		mSpecification = matSpec;
	}

	/*
	 * Set program
	 *
	 */
	void ProgrammaticPbrMaterialStream::setProgram(PbrMaterialSpecification::ProgramOptions progOptions)
	{

		mSpecification.program = progOptions;
	}

	void ProgrammaticPbrMaterialStream::setProgram(string const& program)
	{

		mSpecification.program.isChild = false;
		mSpecification.program.resourceExists = true;
		mSpecification.program.existingResource = program;
	}

	void ProgrammaticPbrMaterialStream::setMeshSpecification(mesh::MeshSpecification const& meshSpec)
	{

		mSpecification.program.spec = meshSpec;
	}

	void ProgrammaticPbrMaterialStream::setProgram2d(bool is2d)
	{

		mSpecification.program.is2d = is2d;
	}

	void ProgrammaticPbrMaterialStream::setProgramVertexShaderFile(string const& file)
	{

		mSpecification.program.vertexShader.type = PbrMaterialSpecification::ProgramOptions::Shader::Type::File;
		mSpecification.program.vertexShader.data = file;
	}

	void ProgrammaticPbrMaterialStream::setProgramVertexShaderResource(string const& resource)
	{

		mSpecification.program.vertexShader.type = PbrMaterialSpecification::ProgramOptions::Shader::Type::Resource;
		mSpecification.program.vertexShader.data = resource;
	}

	void ProgrammaticPbrMaterialStream::setProgramGeometryShaderFile(string const& file)
	{

		mSpecification.program.geometryShader.type = PbrMaterialSpecification::ProgramOptions::Shader::Type::File;
		mSpecification.program.geometryShader.data = file;
	}

	void ProgrammaticPbrMaterialStream::setProgramGeometryShaderResource(string const& resource)
	{

		mSpecification.program.geometryShader.type = PbrMaterialSpecification::ProgramOptions::Shader::Type::Resource;
		mSpecification.program.geometryShader.data = resource;
	}

	void ProgrammaticPbrMaterialStream::setProgramFragmentShaderFile(string const& file)
	{

		mSpecification.program.fragmentShader.type = PbrMaterialSpecification::ProgramOptions::Shader::Type::File;
		mSpecification.program.fragmentShader.data = file;
	}

	void ProgrammaticPbrMaterialStream::setProgramFragmentShaderResource(string const& resource)
	{

		mSpecification.program.fragmentShader.type = PbrMaterialSpecification::ProgramOptions::Shader::Type::Resource;
		mSpecification.program.fragmentShader.data = resource;
	}

	void ProgrammaticPbrMaterialStream::setTextureChild(string const& sampler, string const& resource)
	{

		PbrMaterialSpecification::TextureOptions textureOptions;

		textureOptions.resourceExists = true;
		textureOptions.sampler = sampler;

		textureOptions.isChild = true;
		textureOptions.existingResource = resource;

		mSpecification.textures.push_back(textureOptions);
	}

	void ProgrammaticPbrMaterialStream::setTexture(string const& sampler, string const& texture)
	{

		PbrMaterialSpecification::TextureOptions textureOptions;

		textureOptions.resourceExists = true;
		textureOptions.sampler = sampler;

		textureOptions.isChild = false;
		textureOptions.existingResource = texture;

		mSpecification.textures.push_back(textureOptions);
	}

	void ProgrammaticPbrMaterialStream::setBaseColourMap(string const& texture) { setTexture("PBR_BASE_COLOUR_MAP", texture); }
	void ProgrammaticPbrMaterialStream::setMetallicRoughnessMap(string const& texture) { setTexture("PBR_METALLIC_ROUGHNESS_MAP", texture); }
	void ProgrammaticPbrMaterialStream::setNormalMap(string const& texture) { setTexture("PBR_NORMAL_MAP", texture); }
	void ProgrammaticPbrMaterialStream::setOcclusionMap(string const& texture) { setTexture("PBR_OCCLUSION_MAP", texture); }
	void ProgrammaticPbrMaterialStream::setEmissiveMap(string const& texture) { setTexture("PBR_EMISSIVE_MAP", texture); }

	void ProgrammaticPbrMaterialStream::setExtensionTexture(string const& name, string const& texture, TextureTarget target) { requireExtensionName(name); setTexture(name, texture); mSpecification.textures.back().target = target; }
	void ProgrammaticPbrMaterialStream::setExtensionUniform(string const& name, int32_t value) { requireExtensionName(name); setUniform(name, value); }
	void ProgrammaticPbrMaterialStream::setExtensionUniform(string const& name, float value) { requireExtensionName(name); setUniform(name, value); }
	void ProgrammaticPbrMaterialStream::setExtensionUniform(string const& name, glm::vec2 const& value) { requireExtensionName(name); setUniform(name, value); }
	void ProgrammaticPbrMaterialStream::setExtensionUniform(string const& name, glm::vec3 const& value) { requireExtensionName(name); setUniform(name, value); }
	void ProgrammaticPbrMaterialStream::setExtensionUniform(string const& name, glm::vec4 const& value) { requireExtensionName(name); setUniform(name, value); }

	void ProgrammaticPbrMaterialStream::setDefaultTexture(string const& sampler)
	{

		PbrMaterialSpecification::TextureOptions textureOptions;

		textureOptions.existingResource = true;
		textureOptions.sampler = sampler;

		textureOptions.isChild = false;
		textureOptions.existingResource = "__mpp_tex_none__";

		mSpecification.textures.push_back(textureOptions);
	}

	void ProgrammaticPbrMaterialStream::setSurface(PbrMaterialSpecification::PbrSurface const& surface)
	{
		setPbrSurface(surface);
	}

	void ProgrammaticPbrMaterialStream::setPbrSurface(PbrMaterialSpecification::PbrSurface const& surface)
	{
		mSpecification.pbr = surface;
	}

	void ProgrammaticPbrMaterialStream::setUniforms(UniformCollection const& uniforms)
	{

		mSpecification.uniforms = uniforms;
	}

	void ProgrammaticPbrMaterialStream::setUniform(string const& name, int32_t value)
	{

		mSpecification.uniforms.setUniform(name, value);
	}

	void ProgrammaticPbrMaterialStream::setUniform(string const& name, uint32_t value)
	{

		mSpecification.uniforms.setUniform(name, value);
	}

	void ProgrammaticPbrMaterialStream::setUniform(string const& name, float value)
	{

		mSpecification.uniforms.setUniform(name, value);
	}

	void ProgrammaticPbrMaterialStream::setUniform(string const& name, glm::vec2 const& value)
	{
		mSpecification.uniforms.setUniform(name, value);
	}

	void ProgrammaticPbrMaterialStream::setUniform(string const& name, glm::vec3 const& value)
	{

		mSpecification.uniforms.setUniform(name, value);
	}

	void ProgrammaticPbrMaterialStream::setUniform(string const& name, glm::vec4 const& value)
	{

		mSpecification.uniforms.setUniform(name, value);
	}

	void ProgrammaticPbrMaterialStream::setUniform(string const& name, size_t count, int32_t const* values)
	{

		mSpecification.uniforms.setUniform(name, count, 1, values);
	}

	void ProgrammaticPbrMaterialStream::setUniform(string const& name, size_t count, uint32_t const* values)
	{

		mSpecification.uniforms.setUniform(name, count, 1, values);
	}

	void ProgrammaticPbrMaterialStream::setUniform(string const& name, size_t count, float const* values)
	{

		mSpecification.uniforms.setUniform(name, count, 1, values);
	}
}