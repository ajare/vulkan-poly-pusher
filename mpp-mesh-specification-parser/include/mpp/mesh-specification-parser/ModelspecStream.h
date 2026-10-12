#pragma once

#include "mpp/UniformCollection.h"

#include "mpp/resource-parsers/FileStream.h"

#include "mpp/mesh/MeshSpecification.h"

#include "Config.h"

namespace mpp
{
	namespace mesh_specification_parser
	{

		class _MPPMESHSPECIFICATIONPARSERAPI ModelspecStream : public resource_parsers::FileStream
		{

			mesh::MeshSpecification mMeshSpec;

			std::map<std::string, ResourceStreamPtr> mMaterials;

		public:

			explicit ModelspecStream(std::string const& filepath);

			void load();

			mesh::MeshSpecification const& getMeshSpecification() const;

			std::map<std::string, ResourceStreamPtr> const& getMaterials() const;

			void serialize(std::ofstream& fp);
		};

	}
}