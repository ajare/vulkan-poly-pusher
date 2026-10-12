#pragma once

#include "mpp/Config.h"

#include <cstdint>
#include <memory>
#include <vector>

#include "mpp/mesh/Vertex.h"
#include "mpp/mesh/VertexBufferDefinition.h"
#include "mpp/mesh/VertexBufferStorageType.h"

namespace mpp
{
	class Mesh;
	class RenderSystem;
	namespace detail { class PersistentMappedBuffer; }

	class _MPPAPI VertexBuffer
	{
		struct Attribute
		{
			int id;
			size_t componentSize, sizeInBytes;
			std::uint32_t dataType;
			size_t offsetInBytes;
			bool normalise;
		};

	private:

		std::uint32_t mVBO;

		RenderSystem* mwRenderSystem;

		mesh::VertexBufferStorageType mStorageType;

		size_t mVertexCount;
		
		size_t mVertexStride;

		bool mStreaming;

		bool mStatic;
		
		std::vector<Attribute> mAttributes;

		std::vector<int8_t> mData;

		size_t mMaxDataSize;

		std::unique_ptr<detail::PersistentMappedBuffer> mStreamBuffer;
		std::uint32_t mConfiguredBuffer{ 0 };
		size_t mConfiguredOffset{ SIZE_MAX };

	private:
		friend class Mesh;

		void allocate(size_t size);
		void prepareForRender();

	public:

		VertexBuffer(RenderSystem* renderSystem, mesh::VertexBufferStorageType storageType, size_t vertexCount, size_t vertexStride, bool streaming, bool staticData, std::shared_ptr<const int8_t> data);

		virtual ~VertexBuffer();

		void setAttribute(int id, mesh::Vertex::DataType dataType, size_t componentSize, int offset, bool normalise);

		size_t getNumVertices() const;
			
		size_t getVertexStride() const;

		bool isStatic() const;

		bool usesPersistentMapping() const;

		size_t getNumAttributes() const;

		void enableAttribute(uint32_t index, bool enable);

		std::vector<int8_t>& getBufferData();

		void mapBufferData(size_t numVertices);

		void mapBufferData(uint32_t startVertex, size_t numVertices);

		void bind();

		void unbind();

		void load();

		void unload();
	};

}