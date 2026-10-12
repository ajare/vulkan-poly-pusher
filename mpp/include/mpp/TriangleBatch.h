#pragma once

#include <functional>
#include <vector>

#include "mpp/Batch.h"

namespace mpp
{

	struct TriangleBatchOptions
	{
		enum class Dimension
		{
			P2D,
			P3D
		};

		Dimension dimension{ Dimension::P2D };
		bool specifyMaterial{ false };
		mpp::mesh::Vertex::DataType positionType;
		BatchVertexAttribute texcoordAttrib;
		BatchVertexAttribute colourAttrib;
		bool useDiffuse;
		bool indexed{ false };
	};

	class _MPPAPI TriangleBatch : public Batch
	{
		TriangleBatchOptions mOptions;

		size_t mIndexWidth;

	protected:

		ResourcePtr mTextureOrMaterial;

	private:

		bool indexedVertices() const;

	protected:

		mesh::Primitive::Type getPrimitiveType() const;

		mesh::MeshSpecification createMeshSpecification(mesh::Primitive::Type primitiveType) override;

		uint32_t getProgramFlags() const override;

		int getIndexWidth() const override;

		ResourcePtr createMaterial(std::string const& name, ResourcePtr texture, uint32_t programFlags, bool is2d = true) override;

	public:

		TriangleBatch(std::string const& name,
			TriangleBatchOptions const& options,
			size_t indexWidth,
			ResourcePtr textureOrMaterial,
			size_t initialCapacity,
			RenderSystem* renderSystem,
			ResourceManager* resourceMgr);

		size_t getPrimitiveCount(size_t objectCount) const;

		size_t getVertexCount(size_t primitiveCount) const;

		bool usingTexture() const;

		bool positionFixed() const;

		bool texcoordsFixed() const;

		bool colourFixed() const;

	};
}
