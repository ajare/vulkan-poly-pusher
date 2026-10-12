#pragma once
#pragma once

#include <vector>

#include "mpp/Batch.h"

namespace mpp
{
	struct LineBatchOptions
	{
		mpp::mesh::Vertex::DataType positionType;
		BatchVertexAttribute colourAttrib;
		bool useDiffuse;
	};

	class _MPPAPI LineBatch : public Batch
	{
		LineBatchOptions mOptions;

	private:

		mesh::MeshSpecification createMeshSpecification(mesh::Primitive::Type primitiveType) override;

		bool indexedVertices() const;

	protected:

		mesh::Primitive::Type getPrimitiveType() const;

		uint32_t getProgramFlags() const override;

		int getIndexWidth() const override;

	public:

		LineBatch(std::string const& name,
			LineBatchOptions const& options,
			size_t initialCapacity,
			RenderSystem* renderSystem,
			ResourceManager* resourceMgr);

		size_t getPrimitiveCount(size_t objectCount) const;

		size_t getVertexCount(size_t primitiveCount) const;

		bool positionFixed() const;

		bool colourFixed() const;
	};
}
