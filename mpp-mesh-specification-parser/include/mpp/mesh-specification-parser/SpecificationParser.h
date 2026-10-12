#pragma once

#include <cstdint>
#include <map>
#include <string>

#include <mpp/mesh/MeshSpecification.h>
#include <mpp/mesh/MaterialInformation.h>

#include "mpp/mesh-specification-parser/Config.h"

#include "mpp/mesh-specification-parser/ProgramInformation.h"

namespace mpp
{
	namespace mesh_specification_parser
	{

		class _MPPMESHSPECIFICATIONPARSERAPI SpecificationParser
		{
			std::string mFilename;

			std::map<std::string, mpp::mesh::Vertex::Component> mComponentTypes;
			std::map<std::string, mpp::mesh::Vertex::DataType> mDataTypes;

		public:

			explicit SpecificationParser(std::string const& filename);

			std::map<std::string, mesh::MaterialInformation> parseMaterialInformation();

			mpp::mesh::MeshSpecification parseMeshSpecification(uint32_t& maxVerticesPerMesh);
		};

	}
}