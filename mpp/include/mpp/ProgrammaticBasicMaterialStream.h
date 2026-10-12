#pragma once

#pragma warning(push)
#pragma warning(disable : 4201)
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#pragma warning(pop)

#include "mpp/BasicMaterialStream.h"
#include "mpp/mesh/Vertex.h"
#include "mpp/mesh/MeshSpecification.h"

namespace mpp
{
	class _MPPAPI ProgrammaticBasicMaterialStream : public BasicMaterialStream
	{

		void loadImpl() {}

		void createChildResourceStreamsImpl();

	public:

		explicit ProgrammaticBasicMaterialStream(ResourceManager* resourceMgr);

		void setSpecification(BasicMaterialSpecification const& matSpec);

		void setProgram(BasicMaterialSpecification::ProgramOptions progOptions);

		void setProgram(std::string const& program);

		void setMeshSpecification(mesh::MeshSpecification const& spec);

		void setProgram2d(bool is2d);

		void setProgramVertexShaderFile(std::string const& file);

		void setProgramVertexShaderResource(std::string const& resource);

		void setProgramGeometryShaderFile(std::string const& file);

		void setProgramGeometryShaderResource(std::string const& resource);

		void setProgramFragmentShaderFile(std::string const& file);

		void setProgramFragmentShaderResource(std::string const& resource);

		void setTextureChild(std::string const& sampler, std::string const& resource);

		void setTexture(std::string const& sampler, std::string const& texture);

		void setDefaultTexture(std::string const& sampler);

		void setShadowCasterContract(ShadowCasterContract const& contract);

		void setUniforms(UniformCollection const& uniforms);

		void setUniform(std::string const& name, int32_t value);

		void setUniform(std::string const& name, uint32_t value);

		void setUniform(std::string const& name, float value);

		void setUniform(std::string const& name, glm::vec2 const& value);

		void setUniform(std::string const& name, glm::vec3 const& value);

		void setUniform(std::string const& name, glm::vec4 const& value);

		void setUniform(std::string const& name, size_t count, int32_t const* values);

		void setUniform(std::string const& name, size_t count, uint32_t const* values);

		void setUniform(std::string const& name, size_t count, float const* values);
	};
}