#pragma once

#pragma warning(push)
#pragma warning(disable : 4201)
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#pragma warning(pop)

#include "mpp/PbrMaterialStream.h"
#include "mpp/mesh/Vertex.h"
#include "mpp/mesh/MeshSpecification.h"

namespace mpp
{
	class _MPPAPI ProgrammaticPbrMaterialStream : public PbrMaterialStream
	{

		void loadImpl() {}

		void createChildResourceStreamsImpl();

	public:

		explicit ProgrammaticPbrMaterialStream(ResourceManager* resourceMgr);

		void setSpecification(PbrMaterialSpecification const& matSpec);

		void setProgram(PbrMaterialSpecification::ProgramOptions progOptions);

		void setProgram(std::string const& program);

		void setMeshSpecification(mesh::MeshSpecification const& spec);

		void setProgram2d(bool is2d);

		void setProgramVertexShaderFile(std::string const& file);

		void setProgramVertexShaderResource(std::string const& resource);

		void setProgramGeometryShaderFile(std::string const& file);

		void setProgramGeometryShaderResource(std::string const& resource);

		void setProgramFragmentShaderFile(std::string const& file);

		void setProgramFragmentShaderResource(std::string const& resource);

	private:
		void setTextureChild(std::string const& sampler, std::string const& resource);
		void setTexture(std::string const& sampler, std::string const& texture);
		void setDefaultTexture(std::string const& sampler);
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

	public:
		void setBaseColourMap(std::string const& texture);
		void setMetallicRoughnessMap(std::string const& texture);
		void setNormalMap(std::string const& texture);
		void setOcclusionMap(std::string const& texture);
		void setEmissiveMap(std::string const& texture);
		void setExtensionTexture(std::string const& name, std::string const& texture, TextureTarget target = TextureTarget::Texture2D);
		void setExtensionUniform(std::string const& name, int32_t value);
		void setExtensionUniform(std::string const& name, float value);
		void setExtensionUniform(std::string const& name, glm::vec2 const& value);
		void setExtensionUniform(std::string const& name, glm::vec3 const& value);
		void setExtensionUniform(std::string const& name, glm::vec4 const& value);

		void setSurface(PbrMaterialSpecification::PbrSurface const& surface);
		void setPbrSurface(PbrMaterialSpecification::PbrSurface const& surface);
	};
}