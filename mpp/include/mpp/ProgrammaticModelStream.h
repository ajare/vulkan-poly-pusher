#pragma once

#include <initializer_list>
#include <any>

#include "mpp/ModelStream.h"

#include "mpp/mesh/VertexData.h"

namespace mpp
{

	class _MPPAPI ProgrammaticModelStream : public ModelStream
	{
		struct MeshDataStreamDefinition
		{
			mesh::MeshSpecification specification;
			std::string name;
			std::string material;
			std::vector<uint8_t> vertexData;
			size_t indexWidth;
			float pointSize;
			std::vector<uint8_t> indexData;

			// Component data
			size_t vertexCount, primitiveCount;
			std::map<mpp::mesh::Vertex::Component, VertexDataStreamDefinition> componentStreams;

			MeshDataStreamDefinition()
				: indexWidth(32)
				, pointSize(1.0f)
				, vertexCount(0)
				, primitiveCount(0)
			{
			}
		};

	private:

		std::vector<MeshDataStreamDefinition> mMeshDataDefinitions;

	protected:

		void createMeshDataStreams();

		size_t getNumMeshes() const;

		mesh::MeshSpecification const& getMeshSpecification(size_t meshIndex) const;

		void getMeshCounts(size_t meshIndex, size_t* primitiveCount, size_t* vertexCount);

		VertexDataStreamDefinition getMeshDataStream(size_t meshIndex, mesh::Vertex::Component component) const;

		size_t getMeshIndexWidth(size_t meshIndex) const;

		float getMeshPointSize(size_t meshIndex) const;

		uint8_t const* getMeshIndexData(size_t meshIndex) const;

		std::string const& getMeshName(size_t meshIndex) const;

		std::string const& getMeshMaterial(size_t meshIndex) const;

	public:

		explicit ProgrammaticModelStream(ResourceManager* resourceMgr);

		size_t createMesh(std::string const& name, mesh::MeshSpecification const& specification, std::string const& material, int indexWidth, float pointSize = -1.0f);

		int32_t getMeshId(std::string const& name) const;

		void addVertexData(size_t meshIndex, std::vector<int8_t> const& vertexData);

		void addVertexData(size_t meshIndex, mesh::VertexData const& vertexData);

		void addPoint(size_t meshIndex, uint32_t v);

		void addLine(size_t meshIndex, uint32_t v0, uint32_t v1);

		void addTriangle(size_t meshIndex, uint32_t v0, uint32_t v1, uint32_t v2);
	};
}
