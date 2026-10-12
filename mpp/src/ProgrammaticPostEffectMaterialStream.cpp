#include "mpp/DefaultShaders.h"
#include "mpp/ProgrammaticPostEffectMaterialStream.h"
#include "mpp/ProgrammaticProgramStream.h"

using namespace std;

namespace mpp
{
	ProgrammaticPostEffectMaterialStream::ProgrammaticPostEffectMaterialStream(ResourceManager* resourceMgr)
		: PostEffectMaterialStream(resourceMgr)
	{
	}

	void ProgrammaticPostEffectMaterialStream::createChildResourceStreamsImpl()
	{
		if (mSpecification.program.resourceExists && mSpecification.program.isChild)
		{
			auto programStream = new ProgrammaticProgramStream(getResourceMgr());

			auto parser = make_shared<program::Parser>();
			parser->setMeshSpecification(mSpecification.program.spec);

			if (mSpecification.program.vertexShader.type == PostEffectMaterialSpecification::ProgramOptions::Shader::Type::Default)
				parser->setVertexSource(VertexShaderFullscreenTemplate);
			else
				parser->setVertexSource(mSpecification.program.vertexShader.data);

			// A post effect has no universally meaningful default fragment shader
			// -- each effect's shading is what makes it that effect -- so
			// PostEffectMaterial::createImpl() requires an explicit source here
			// rather than falling back to a built-in.
			parser->setFragmentSource(mSpecification.program.fragmentShader.data);

			programStream->setParser(parser);
			addChild("Program", ResourceStreamPtr(programStream));
		}
	}

	void ProgrammaticPostEffectMaterialStream::setSpecification(PostEffectMaterialSpecification const& matSpec)
	{
		mSpecification = matSpec;
	}

	void ProgrammaticPostEffectMaterialStream::setProgram(PostEffectMaterialSpecification::ProgramOptions progOptions)
	{
		mSpecification.program = progOptions;
	}

	void ProgrammaticPostEffectMaterialStream::setProgram(string const& program)
	{
		mSpecification.program.isChild = false;
		mSpecification.program.resourceExists = true;
		mSpecification.program.existingResource = program;
	}

	void ProgrammaticPostEffectMaterialStream::setMeshSpecification(mesh::MeshSpecification const& meshSpec)
	{
		mSpecification.program.spec = meshSpec;
	}

	void ProgrammaticPostEffectMaterialStream::setProgramVertexShaderFile(string const& file)
	{
		mSpecification.program.vertexShader.type = PostEffectMaterialSpecification::ProgramOptions::Shader::Type::File;
		mSpecification.program.vertexShader.data = file;
	}

	void ProgrammaticPostEffectMaterialStream::setProgramVertexShaderResource(string const& resource)
	{
		mSpecification.program.vertexShader.type = PostEffectMaterialSpecification::ProgramOptions::Shader::Type::Resource;
		mSpecification.program.vertexShader.data = resource;
	}

	void ProgrammaticPostEffectMaterialStream::setProgramFragmentShaderFile(string const& file)
	{
		mSpecification.program.fragmentShader.type = PostEffectMaterialSpecification::ProgramOptions::Shader::Type::File;
		mSpecification.program.fragmentShader.data = file;
	}

	void ProgrammaticPostEffectMaterialStream::setProgramFragmentShaderResource(string const& resource)
	{
		mSpecification.program.fragmentShader.type = PostEffectMaterialSpecification::ProgramOptions::Shader::Type::Resource;
		mSpecification.program.fragmentShader.data = resource;
	}

	void ProgrammaticPostEffectMaterialStream::addSamplerSlot(string const& sampler)
	{
		mSpecification.samplerSlots.push_back(sampler);
	}

	void ProgrammaticPostEffectMaterialStream::setUniforms(UniformCollection const& uniforms)
	{
		mSpecification.uniforms = uniforms;
	}

	void ProgrammaticPostEffectMaterialStream::setUniform(string const& name, int32_t value)
	{
		mSpecification.uniforms.setUniform(name, value);
	}

	void ProgrammaticPostEffectMaterialStream::setUniform(string const& name, uint32_t value)
	{
		mSpecification.uniforms.setUniform(name, value);
	}

	void ProgrammaticPostEffectMaterialStream::setUniform(string const& name, float value)
	{
		mSpecification.uniforms.setUniform(name, value);
	}

	void ProgrammaticPostEffectMaterialStream::setUniform(string const& name, glm::vec2 const& value)
	{
		mSpecification.uniforms.setUniform(name, value);
	}

	void ProgrammaticPostEffectMaterialStream::setUniform(string const& name, glm::vec3 const& value)
	{
		mSpecification.uniforms.setUniform(name, value);
	}

	void ProgrammaticPostEffectMaterialStream::setUniform(string const& name, glm::vec4 const& value)
	{
		mSpecification.uniforms.setUniform(name, value);
	}
}
