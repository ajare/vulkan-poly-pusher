#pragma once

#include <vector>
#include "Config.h"
#include "Vertex.h"
#include "VertexBufferAttributeLayout.h"
#include "Primitive.h"
#include "VertexBufferStorageType.h"

namespace mpp
{
	namespace mesh
	{

		class _MPPMESHAPI MeshSpecification
		{
			Primitive::Type mPrimitiveType;

			VertexBufferStorageType mStorageType;

			bool mIndexedVertices;

			std::vector<VertexBufferAttributeLayout> mVertexBufferAttributeLayouts;

		private:

			bool compare(MeshSpecification const& other) const;

		public:

			MeshSpecification();

			explicit MeshSpecification(Primitive::Type primitiveType);

			MeshSpecification(Primitive::Type primitiveType, VertexBufferStorageType storageType);

			MeshSpecification(MeshSpecification const& other);

			bool operator==(MeshSpecification const& other) const;
			
			bool operator!=(MeshSpecification const& other) const;

			void setPrimitiveType(Primitive::Type primitiveType);

			Primitive::Type getPrimitiveType() const;

			void setStorageType(VertexBufferStorageType storageType);

			VertexBufferStorageType getStorageType() const;

			void setIndexedVertices(bool indexed);

			bool verticesIndexed() const;

			VertexBufferAttributeLayout* createVertexBufferAttributeLayout(bool staticData);

			size_t getNumVertexBufferAttributeLayouts() const;

			VertexBufferAttributeLayout const& getVertexBufferAttributeLayout(uint32_t index) const;

			VertexBufferAttributeLayout& getVertexBufferAttributeLayout(uint32_t index);

			size_t getVertexStrideInBytes() const;

			size_t getNumComponents() const;

			std::string getDescriptor(std::string const& prefix) const;

			uint32_t getHashCode() const;

			std::string getHashString() const;
		};

	}
}