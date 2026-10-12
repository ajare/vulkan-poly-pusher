#pragma once

#pragma warning(push)
#pragma warning(disable : 4201)
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#pragma warning(pop)

#include "mpp/PostEffectMaterialStream.h"
#include "mpp/mesh/MeshSpecification.h"

namespace mpp
{
	class _MPPAPI ProgrammaticPostEffectMaterialStream : public PostEffectMaterialStream
	{
		void loadImpl() {}

		void createChildResourceStreamsImpl();

	public:

		explicit ProgrammaticPostEffectMaterialStream(ResourceManager* resourceMgr);

		void setSpecification(PostEffectMaterialSpecification const& matSpec);

		void setProgram(PostEffectMaterialSpecification::ProgramOptions progOptions);

		void setProgram(std::string const& program);

		void setMeshSpecification(mesh::MeshSpecification const& spec);

		void setProgramVertexShaderFile(std::string const& file);

		void setProgramVertexShaderResource(std::string const& resource);

		void setProgramFragmentShaderFile(std::string const& file);

		void setProgramFragmentShaderResource(std::string const& resource);

		void addSamplerSlot(std::string const& sampler);

		void setUniforms(UniformCollection const& uniforms);

		void setUniform(std::string const& name, int32_t value);

		void setUniform(std::string const& name, uint32_t value);

		void setUniform(std::string const& name, float value);

		void setUniform(std::string const& name, glm::vec2 const& value);

		void setUniform(std::string const& name, glm::vec3 const& value);

		void setUniform(std::string const& name, glm::vec4 const& value);
	};
}
