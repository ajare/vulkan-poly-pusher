#pragma once

#include <vector>
#include <memory>
#include "Config.h"
#include "Vertex.h"
#include "VertexBufferAttributeLayout.h"
#include "VertexBufferStorageType.h"

namespace mpp
{

	namespace mesh
	{

		class _MPPMESHAPI VertexBufferDefinition
		{
			int mVertexCount;

			int mVertexStride;

			bool mStreaming;

			// Array, so must supply deleter, ie: std::shared_ptr<int8_t> ptr(new int8_t[vertexCount], [](int8_t *p) { delete[] p; } );
			std::shared_ptr<const int8_t> mData;

			VertexBufferAttributeLayout mSpecification;

		public:

			VertexBufferDefinition(VertexBufferAttributeLayout const& spec, int vertexCount, int vertexStride, bool streaming, std::shared_ptr<const int8_t> vertexData);

			int getVertexStride() const;

			int getVertexCount() const;

			bool getStreaming() const;

			int getDataSize() const;

			std::shared_ptr<const int8_t> getData() const;

			VertexBufferAttributeLayout const& getAttributeLayout() const;

			size_t getNumAttributes() const;

			VertexBufferAttributeLayout::Attribute const& getAttribute(int index) const;
		};

	}
}