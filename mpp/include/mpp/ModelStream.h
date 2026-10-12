#pragma once

#include <map>
#include <vector>
#include <fstream>

#include "mpp/mesh/Vertex.h"
#include "mpp/mesh/MeshDefinition.h"
#include "mpp/mesh/MeshSpecification.h"

#include "mpp/Resource.h"
#include "mpp/ResourceStream.h"
#include "mpp/Program.h"

namespace mpp
{
	class RenderSystem;
	class ResourceManager;

	class _MPPAPI ModelStream : public ResourceStream
	{
	public:

		struct VertexDataStreamDefinition
		{
			std::shared_ptr<const int8_t> data;
			mesh::Vertex::DataType dataType;
			int offset, stride;

			VertexDataStreamDefinition()
				: dataType(mesh::Vertex::DataType::None)
				, offset(0)
				, stride(0)
			{
			}
		};

	private:
		bool mCalculateBounds;

	protected:
		std::vector<mesh::MeshDefinition*> mMeshDefinitions;

	private:

		mesh::MeshDefinition* createMeshDefinition(std::string const& name, mesh::Primitive::Type type, int primitiveCount, mesh::VertexBufferStorageType storageType, std::string const& material, int indexWidth, float pointSize = -1.0f);

		bool streamsAreTightlyPacked(mesh::VertexBufferAttributeLayout const& bufferSpec, std::map<mesh::Vertex::Component, VertexDataStreamDefinition> const& componentStreams);

		int8_t* copyVertexBufferData(mesh::VertexBufferAttributeLayout const& bufferSpec, VertexDataStreamDefinition componentStream, int vertexCount, int vertexStride);

		int8_t* deinterlaceVertexBufferData(mesh::VertexBufferAttributeLayout const& bufferSpec, std::map<mesh::Vertex::Component, VertexDataStreamDefinition> const& componentStreams, int vertexCount, int vertexStride);

	protected:

		void loadImpl();

		void unloadImpl();

		int getNumBufferDefinitions() const;

		virtual void createMeshDataStreams() = 0;

		virtual size_t getNumMeshes() const = 0;

		virtual mesh::MeshSpecification const& getMeshSpecification(size_t meshIndex) const = 0;

		virtual void getMeshCounts(size_t meshIndex, size_t* primitiveCount, size_t* vertexCount) = 0;

		virtual VertexDataStreamDefinition getMeshDataStream(size_t meshIndex, mesh::Vertex::Component component) const = 0;

		virtual size_t getMeshIndexWidth(size_t meshIndex) const = 0;

		virtual float getMeshPointSize(size_t meshIndex) const = 0;

		virtual uint8_t const* getMeshIndexData(size_t meshIndex) const = 0;

		virtual std::string const& getMeshName(size_t meshIndex) const = 0;

		virtual std::string const& getMeshMaterial(size_t meshIndex) const = 0;

	public:

		explicit ModelStream(ResourceManager* resourceMgr);

		virtual ~ModelStream();

		void setCalculateBounds(bool calculate);

		bool getCalculateBounds() const;

		size_t getNumMeshDefinitions() const;

		mesh::MeshDefinition* getMeshDefinition(size_t index);

		virtual std::string markUpMaterialName(std::string const& name, std::string const& material);

	};
}