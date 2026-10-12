#pragma once

#include <map>
#include <string>

#include "mpp/ProgramStream.h"
#include "mpp/ResourceManager.h"

#include "Config.h"
#include "FileStream.h"

namespace mpp
{
	namespace resource_parsers
	{

		class _MPPRESOURCEPARSERSAPI FileProgramStream : public mpp::ProgramStream, public FileStream
		{
			std::map<std::string, mesh::VertexBufferStorageType> mMeshSpecificationStorage;
			std::map<std::string, mesh::Primitive::Type>  mMeshSpecificationPrimitive;
			std::map<std::string, mesh::Vertex::Component> mComponentTypes;
			std::map<std::string, mesh::Vertex::DataType> mDataTypes;

			bool mMeshSpecRequired;

			mesh::MeshSpecification mMeshSpecification;

			bool mRelativisePaths;

		private:
			struct Definition
			{
				std::shared_ptr<program::Parser> parser;
				std::set<std::string> attribs;
			};

			void setup();

			void loadImpl();

			static Shader parseShader(mpp::data::StructuredData const& data, ResourceManager* resourceMgr, std::string const& filepath);

		public:

			FileProgramStream(ResourceManager* resourceMgr, std::string const& filepath, bool relativisePaths = true);

			FileProgramStream(ResourceManager* resourceMgr, std::string const& filepath, mpp::data::StructuredData const& data, bool relativisePaths = true);

			FileProgramStream(ResourceManager* resourceMgr, std::string const& filepath, mesh::MeshSpecification const& meshSpec, bool relativisePaths = true);

			FileProgramStream(ResourceManager* resourceMgr, std::string const& filepath, mpp::data::StructuredData const& data, mesh::MeshSpecification const& meshSpec, bool relativisePaths = true);

			static std::pair<std::string, Definition> parseDefinition(mpp::data::StructuredData const& data, ResourceManager* resourceMgr, std::string const& filepath, bool meshSpecRequired, mesh::MeshSpecification const* mainMeshSpec, bool relativisePaths = true);
		};

	}
}