#pragma once

#include <map>
#include <string>

#include "mpp/PbrMaterialStream.h"
#include "mpp/ResourceManager.h"

#include "Config.h"
#include "FileStream.h"

namespace mpp
{
	namespace resource_parsers
	{

		class _MPPRESOURCEPARSERSAPI FilePbrMaterialStream : public mpp::PbrMaterialStream, public FileStream
		{
			bool mUseSpecifiedMeshSpec;

			mesh::MeshSpecification mMeshSpec;

			bool mRelativisePaths;

		private:

			void createChildResourceStreamsImpl();

			void parseForChildResourceStreams(mpp::data::StructuredData const& data, std::string const& filepath, bool useSpecifiedMesh, mesh::MeshSpecification const* meshSpec);

			static void parseUniform(mpp::data::StructuredData const& data, UniformCollection& uniforms, std::string const& filepath);

			static void parseUniformVectorType(std::string const& name, std::string const& type, size_t count, std::string const& value, UniformCollection &uniforms, std::string const& filepath);

			static void parseUniformMatrixType(std::string const& name, std::string const& type, size_t count, std::string const& value, UniformCollection &uniforms, std::string const& filepath);

		protected:

			void loadImpl();

		public:

			FilePbrMaterialStream(ResourceManager* resourceMgr, std::string const& filepath, bool relativisePaths = true);

			FilePbrMaterialStream(ResourceManager* resourceMgr, std::string const& filepath, mpp::data::StructuredData const& data, bool relativisePaths = true);

			FilePbrMaterialStream(ResourceManager* resourceMgr, std::string const& filepath, mesh::MeshSpecification const& meshSpec, bool relativisePaths = true);

			FilePbrMaterialStream(ResourceManager* resourceMgr, std::string const& filepath, mpp::data::StructuredData const& data, mesh::MeshSpecification const& meshSpec, bool relativisePaths = true);

			static std::pair<std::string, PbrMaterialSpecification> parseDefinition(mpp::data::StructuredData const& data, ResourceManager* resourceMgr, std::string const& filepath);
		};

	}
}