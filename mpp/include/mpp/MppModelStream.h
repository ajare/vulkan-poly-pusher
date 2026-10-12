#pragma once

#include "mpp/ModelStream.h"

namespace mpp
{

	class _MPPAPI MppModelStream : public ModelStream
	{
		struct MeshDataStreamDefinition
		{
			mesh::MeshSpecification specification;
			std::string name;
			std::string material;

			mesh::Primitive::Type primitiveType;

			float pointSize;
			
			size_t indexWidth;
			std::shared_ptr<const uint8_t> indexData;

			// Component data
			size_t vertexCount, primitiveCount;
			std::map<mesh::Vertex::Component, VertexDataStreamDefinition> componentStreams;

			MeshDataStreamDefinition()
				: primitiveType(mesh::Primitive::Type::Triangles)
				, pointSize(1.0f)
				, indexWidth(32)
				, vertexCount(0)
				, primitiveCount(0)
			{
			}
		};

	private:

		std::string mFilename;

		std::vector<MeshDataStreamDefinition*> mMeshDataDefinitions;

	private:

		void createChildResourceStreamsImpl();

		void createMeshDataStreams();

		VertexDataStreamDefinition getMeshDataStream(size_t meshIndex, mesh::Vertex::Component component) const;

		mesh::MeshSpecification const& getMeshSpecification(size_t meshIndex) const;

		size_t getMeshIndexWidth(size_t meshIndex) const;

		float getMeshPointSize(size_t meshIndex) const;

		uint8_t const* getMeshIndexData(size_t meshIndex) const;

		std::string const& getMeshName(size_t meshIndex) const;

		std::string const& getMeshMaterial(size_t meshIndex) const;

		size_t getNumMeshes() const;

		void getMeshCounts(size_t meshIndex, size_t* primitiveCount, size_t* vertexCount);

	public:

		MppModelStream(ResourceManager* resourceMgr, std::string const& filename);

		~MppModelStream();

		std::string markUpMaterialName(std::string const& name, std::string const& material);
	};

}
