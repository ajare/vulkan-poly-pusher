#pragma once

#include <initializer_list>

#include "mpp/ModelStream.h"

namespace mpp
{
	class _MPPAPI PrimitiveModelStream : public ModelStream
	{
	protected:

		struct MeshDataStreamDefinition
		{
			mesh::MeshSpecification specification;
			std::string name;
			std::string material;

			std::vector<int8_t> vertexData;
			
			float pointSize;
			
			int indexWidth;
			std::vector<uint32_t> indexData;

			// Component data
			int vertexCount, primitiveCount;
			std::map<mpp::mesh::Vertex::Component, VertexDataStreamDefinition> componentStreams;

			MeshDataStreamDefinition()
				: pointSize(1.0f)
				, indexWidth(32)
				, vertexCount(0)
				, primitiveCount(0)
			{
			}
		};

	private:

		uint32_t mStrideInBytes;

	protected:

		MeshDataStreamDefinition mMeshDataDefinition;

	protected:

		void createMeshDataStreams();

		std::map<std::string, size_t> getComponentOffsets(uint32_t& strideInBytes);

		size_t getNumMeshes() const;

		mesh::MeshSpecification const& getMeshSpecification(size_t meshIndex) const;

		void getMeshCounts(size_t meshIndex, size_t* primitiveCount, size_t* vertexCount);

		VertexDataStreamDefinition getMeshDataStream(size_t meshIndex, mesh::Vertex::Component component) const;

		size_t getMeshIndexWidth(size_t meshIndex) const;

		float getMeshPointSize(size_t meshIndex) const;

		uint8_t const* getMeshIndexData(size_t meshIndex) const;

		std::string const& getMeshName(size_t meshIndex) const;

		std::string const& getMeshMaterial(size_t meshIndex) const;

		template<typename T>
		void setVertexData(uint32_t offset, std::initializer_list<T> const& vertex)
		{
			uint8_t* dataPtr = ((uint8_t*)&mMeshDataDefinition.vertexData[0]) + offset;
			for (auto it: vertex)
			{
				T value = it;
				memcpy(dataPtr, &value, sizeof(T));
				dataPtr += sizeof(T);
			}
		}

		void addTriangle(uint32_t v0, uint32_t v1, uint32_t v2);

	public:

		PrimitiveModelStream(ResourceManager* resourceMgr, mesh::MeshSpecification const& meshSpec, std::string const& material);

		void setData(uint32_t offset, mesh::Vertex::Component component, mesh::Vertex::DataType dataType, bool normalised, double x, double y = 0.0, double z = 0.0, double w = 1.0);
	};
}
