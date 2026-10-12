#include "mpp/DefaultShaders.h"
#include "mpp/Program.h"
#include "mpp/ResourceManager.h"
#include "mpp/ProgrammaticBasicMaterialStream.h"
#include "mpp/ProgrammaticProgramStream.h"
#include "mpp/ProgrammaticTextureStream.h"

using namespace std;

namespace mpp
{

	ProgrammaticBasicMaterialStream::ProgrammaticBasicMaterialStream(ResourceManager* resourceMgr)
		: BasicMaterialStream(resourceMgr)
	{
	}

	void ProgrammaticBasicMaterialStream::createChildResourceStreamsImpl()
	{

		// Program
		if (mSpecification.program.resourceExists && mSpecification.program.isChild)
		{
			auto programStream = new ProgrammaticProgramStream(getResourceMgr());
			
			auto parser = make_shared<program::Parser>();

			parser->setMeshSpecification(mSpecification.program.spec);

			// Load source into parser
			if (mSpecification.program.vertexShader.type == BasicMaterialSpecification::ProgramOptions::Shader::Type::Default)
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

			if (mSpecification.program.fragmentShader.type == BasicMaterialSpecification::ProgramOptions::Shader::Type::Default)
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

	void ProgrammaticBasicMaterialStream::setSpecification(BasicMaterialSpecification const& matSpec)
	{

		mSpecification = matSpec;
	}

	/*
	 * Set program
	 *
	 */
	void ProgrammaticBasicMaterialStream::setProgram(BasicMaterialSpecification::ProgramOptions progOptions)
	{

		mSpecification.program = progOptions;
	}

	void ProgrammaticBasicMaterialStream::setProgram(string const& program)
	{

		mSpecification.program.isChild = false;
		mSpecification.program.resourceExists = true;
		mSpecification.program.existingResource = program;
	}

	void ProgrammaticBasicMaterialStream::setMeshSpecification(mesh::MeshSpecification const& meshSpec)
	{

		mSpecification.program.spec = meshSpec;
	}

	void ProgrammaticBasicMaterialStream::setProgram2d(bool is2d)
	{

		mSpecification.program.is2d = is2d;
	}

	void ProgrammaticBasicMaterialStream::setProgramVertexShaderFile(string const& file)
	{

		mSpecification.program.vertexShader.type = BasicMaterialSpecification::ProgramOptions::Shader::Type::File;
		mSpecification.program.vertexShader.data = file;
	}

	void ProgrammaticBasicMaterialStream::setProgramVertexShaderResource(string const& resource)
	{

		mSpecification.program.vertexShader.type = BasicMaterialSpecification::ProgramOptions::Shader::Type::Resource;
		mSpecification.program.vertexShader.data = resource;
	}

	void ProgrammaticBasicMaterialStream::setProgramGeometryShaderFile(string const& file)
	{

		mSpecification.program.geometryShader.type = BasicMaterialSpecification::ProgramOptions::Shader::Type::File;
		mSpecification.program.geometryShader.data = file;
	}

	void ProgrammaticBasicMaterialStream::setProgramGeometryShaderResource(string const& resource)
	{

		mSpecification.program.geometryShader.type = BasicMaterialSpecification::ProgramOptions::Shader::Type::Resource;
		mSpecification.program.geometryShader.data = resource;
	}

	void ProgrammaticBasicMaterialStream::setProgramFragmentShaderFile(string const& file)
	{

		mSpecification.program.fragmentShader.type = BasicMaterialSpecification::ProgramOptions::Shader::Type::File;
		mSpecification.program.fragmentShader.data = file;
	}

	void ProgrammaticBasicMaterialStream::setProgramFragmentShaderResource(string const& resource)
	{

		mSpecification.program.fragmentShader.type = BasicMaterialSpecification::ProgramOptions::Shader::Type::Resource;
		mSpecification.program.fragmentShader.data = resource;
	}

	void ProgrammaticBasicMaterialStream::setTextureChild(string const& sampler, string const& resource)
	{

		BasicMaterialSpecification::TextureOptions textureOptions;
		
		textureOptions.resourceExists = true;
		textureOptions.sampler = sampler;

		textureOptions.isChild = true;
		textureOptions.existingResource = resource;

		mSpecification.textures.push_back(textureOptions);
	}

	void ProgrammaticBasicMaterialStream::setTexture(string const& sampler, string const& texture)
	{

		BasicMaterialSpecification::TextureOptions textureOptions;

		textureOptions.resourceExists = true;
		textureOptions.sampler = sampler;

		textureOptions.isChild = false;
		textureOptions.existingResource = texture;

		mSpecification.textures.push_back(textureOptions);
	}

	void ProgrammaticBasicMaterialStream::setDefaultTexture(string const& sampler)
	{

		BasicMaterialSpecification::TextureOptions textureOptions;

		textureOptions.existingResource = true;
		textureOptions.sampler = sampler;

		textureOptions.isChild = false;
		textureOptions.existingResource = "__mpp_tex_none__";

		mSpecification.textures.push_back(textureOptions);
	}

	void ProgrammaticBasicMaterialStream::setShadowCasterContract(ShadowCasterContract const& contract)
	{
		mSpecification.shadowCaster = contract;
	}

	void ProgrammaticBasicMaterialStream::setUniforms(UniformCollection const& uniforms)
	{

		mSpecification.uniforms = uniforms;
	}

	void ProgrammaticBasicMaterialStream::setUniform(string const& name, int32_t value)
	{

		mSpecification.uniforms.setUniform(name, value);
	}

	void ProgrammaticBasicMaterialStream::setUniform(string const& name, uint32_t value)
	{

		mSpecification.uniforms.setUniform(name, value);
	}

	void ProgrammaticBasicMaterialStream::setUniform(string const& name, float value)
	{

		mSpecification.uniforms.setUniform(name, value);
	}

	void ProgrammaticBasicMaterialStream::setUniform(string const& name, glm::vec3 const& value)
	{

		mSpecification.uniforms.setUniform(name, value);
	}

	void ProgrammaticBasicMaterialStream::setUniform(string const& name, glm::vec4 const& value)
	{

		mSpecification.uniforms.setUniform(name, value);
	}

	void ProgrammaticBasicMaterialStream::setUniform(string const& name, size_t count, int32_t const* values)
	{

		mSpecification.uniforms.setUniform(name, count, 1, values);
	}

	void ProgrammaticBasicMaterialStream::setUniform(string const& name, size_t count, uint32_t const* values)
	{

		mSpecification.uniforms.setUniform(name, count, 1, values);
	}

	void ProgrammaticBasicMaterialStream::setUniform(string const& name, size_t count, float const* values)
	{

		mSpecification.uniforms.setUniform(name, count, 1, values);
	}
}