#pragma once

#include <vector>

#include "mpp/Batch.h"

namespace mpp
{

	struct CircleBatchOptions
	{
		enum class VertexOptions
		{
			Auto,
			Points,
			Triangles
		};

		VertexOptions vertexOptions;
		mpp::mesh::Vertex::DataType positionType;
		BatchVertexAttribute colourAttrib;
		bool useDiffuse;
	};

	class _MPPAPI CircleBatch : public Batch
	{
		CircleBatchOptions mOptions;

		float mRadius;

		float mBorderSize;

		bool mAntiAlias;

		size_t mIndexWidth;

	private:

		bool indexedVertices() const;

		void createIndexData(std::vector<uint8_t>& data, uint32_t start, size_t count);

	protected:

		mesh::Primitive::Type getPrimitiveType() const;

		mesh::MeshSpecification createMeshSpecification(mesh::Primitive::Type primitiveType) override;

		void addIndexedPrimitives(std::shared_ptr<ProgrammaticModelStream> ms, int meshIndex) override;

		uint32_t getProgramFlags() const override;

		int getIndexWidth() const override;

	public:

		CircleBatch(std::string const& name,
			CircleBatchOptions const& options,
			size_t indexWidth,
			float maxRadius,
			float borderSize,
			bool antiAlias,
			size_t initialCapacity,
			RenderSystem* renderSystem,
			ResourceManager* resourceMgr);

		size_t getPrimitiveCount(size_t objectCount) const;

		size_t getVertexCount(size_t primitiveCount) const;

		bool usingPointSprites() const;

		float getRadius() const;
	};

}
