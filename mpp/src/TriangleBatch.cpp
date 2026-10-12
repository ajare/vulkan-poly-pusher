#include <cmath>

#include "mpp/TriangleBatch.h"
#include "mpp/DefaultShaders.h"
#include "mpp/ProgrammaticBasicMaterialStream.h"
#include "mpp/ResourceManager.h"
#include "mpp/MppException.h"

using namespace std;

namespace mpp
{
	using namespace mesh;

	/*
	 * Constructor.
	 *
	 */
	TriangleBatch::TriangleBatch(string const& name,
		TriangleBatchOptions const& options,
		size_t indexWidth,
		ResourcePtr textureOrMaterial,
		size_t initialCapacity,
		RenderSystem* renderSystem,
		ResourceManager* resourceMgr)
		: Batch(name, initialCapacity, VertexShader2dTemplate, FragmentShader2dTemplate, "tris", options.colourAttrib, options.useDiffuse, renderSystem, resourceMgr)
		, mOptions(options)
		, mIndexWidth(indexWidth)
		, mTextureOrMaterial(textureOrMaterial)
	{
	}

	bool TriangleBatch::indexedVertices() const
	{
		return mOptions.indexed;
	}

	mesh::Primitive::Type TriangleBatch::getPrimitiveType() const
	{
		return mesh::Primitive::Type::Triangles;
	}

	mesh::MeshSpecification TriangleBatch::createMeshSpecification(mesh::Primitive::Type primitiveType)
	{
		auto meshSpec = mesh::MeshSpecification(primitiveType);
		auto dynamicLayout = meshSpec.createVertexBufferAttributeLayout(false);
		mesh::VertexBufferAttributeLayout* staticLayout{ nullptr };

		meshSpec.setIndexedVertices(mOptions.indexed);

		// Position
		if (mOptions.dimension == TriangleBatchOptions::Dimension::P2D)
		{
			dynamicLayout->createAttribute(mesh::Vertex::Component::Position2, mOptions.positionType, false);
		}
		else
		{
			dynamicLayout->createAttribute(mesh::Vertex::Component::Position3, mOptions.positionType, false);
			dynamicLayout->createAttribute(mesh::Vertex::Component::Normal3, mOptions.positionType, false);
		}

		// Texture coords
		if (usingTexture())
		{
			mesh::VertexBufferAttributeLayout* texcoordLayout{ nullptr };
			if (mOptions.texcoordAttrib.fixedValues)
			{
				if (!staticLayout)
				{
					staticLayout = meshSpec.createVertexBufferAttributeLayout(true);
				}

				texcoordLayout = staticLayout;
			}
			else
			{
				texcoordLayout = dynamicLayout;
			}

			texcoordLayout->createAttribute(mesh::Vertex::Component::TexCoord2, mOptions.texcoordAttrib.dataType, false);
		}

		// Colour
		if (mOptions.colourAttrib.dataType != mesh::Vertex::DataType::None)
		{
			mesh::VertexBufferAttributeLayout* colourLayout{ nullptr };
			if (mOptions.colourAttrib.fixedValues)
			{
				if (!staticLayout)
				{
					staticLayout = meshSpec.createVertexBufferAttributeLayout(true);
				}

				colourLayout = staticLayout;
			}
			else
			{
				colourLayout = dynamicLayout;
			}

			colourLayout->createAttribute(mesh::Vertex::Component::Colour4, mOptions.colourAttrib.dataType, mesh::Vertex::isDataTypeNormalisable(mOptions.colourAttrib.dataType));
		}

		return meshSpec;
	}

	uint32_t TriangleBatch::getProgramFlags() const
	{
		uint32_t flags = MPP_PROGRAM_TAGS_PRIM_TRIANGLES
			| (usingTexture() ? MPP_PROGRAM_TAGS_TEXTURE : 0)
			| (usingDiffuse() ? MPP_PROGRAM_TAGS_DIFFUSE : 0);

		return flags;
	}

	int TriangleBatch::getIndexWidth() const
	{
		return (int)mIndexWidth;
	}

	ResourcePtr TriangleBatch::createMaterial(string const& name, ResourcePtr texture, uint32_t programFlags, bool is2d)
	{
		auto res = mOptions.specifyMaterial ? mTextureOrMaterial
			: Batch::createMaterial(getName() + "_TriBatch", 
				mTextureOrMaterial, 
				getProgramFlags(), 
				mOptions.dimension == TriangleBatchOptions::Dimension::P2D);

		return res;
	}
	
	size_t TriangleBatch::getPrimitiveCount(size_t objectCount) const
	{
		return objectCount;
	}

	/*
	 * Get the number of vertices required, given the number of primitives.
	 *
	 */
	size_t TriangleBatch::getVertexCount(size_t primitiveCount) const
	{
		if (mIndexWidth == 0)
		{
			return primitiveCount * 3;
		}
		else
		{
			return 0;
		}
	}

	bool TriangleBatch::usingTexture() const
	{
		return true;
	}

	bool TriangleBatch::positionFixed() const
	{
		return false;
	}

	bool TriangleBatch::texcoordsFixed() const
	{
		return mOptions.texcoordAttrib.fixedValues;
	}

	bool TriangleBatch::colourFixed() const
	{
		return mOptions.colourAttrib.fixedValues;
	}
}