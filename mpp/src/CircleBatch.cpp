#include <cmath>

#include "mpp/CircleBatch.h"
#include "mpp/DefaultShaders.h"
#include "mpp/ProgrammaticBasicMaterialStream.h"
#include "mpp/ResourceManager.h"

using namespace std;

namespace mpp
{
	using namespace mesh;

	/*
	 * Constructor.
	 *
	 */
	CircleBatch::CircleBatch(string const& name,
		CircleBatchOptions const& options,
		size_t indexWidth,
		float maxRadius,
		float borderSize,
		bool antiAlias,
		size_t initialCapacity,
		RenderSystem* renderSystem,
		ResourceManager* resourceMgr)
		: Batch(
			name, 
			initialCapacity, 
			VertexShader2dCircle, 
			antiAlias ? FragmentShader2dCircleAntialiased : FragmentShader2dCircle, 
			"circle",
			options.colourAttrib,
			options.useDiffuse,
			renderSystem, 
			resourceMgr)
		, mOptions(options)
		, mRadius(maxRadius)
		, mBorderSize(borderSize)
		, mAntiAlias(antiAlias)
		, mIndexWidth(indexWidth)
	{
		// Set vertex options
		float size = mRadius * 2;
		Caps const& caps = renderSystem->getCaps();
		bool canUsePointSprites = caps.pointSizeRange[0] < size && caps.pointSizeRange[1] > size;

		if (options.vertexOptions == CircleBatchOptions::VertexOptions::Points && !canUsePointSprites)
		{
			THROW_MPP("Cannot use point sprites for this CircleBatch.", __LINE__, __FILE__, __func__);
		}

		if (canUsePointSprites && options.vertexOptions != CircleBatchOptions::VertexOptions::Triangles)
		{
			mOptions.vertexOptions = CircleBatchOptions::VertexOptions::Points;
		}
		else
		{
			mOptions.vertexOptions = CircleBatchOptions::VertexOptions::Triangles;
		}
	}

	mesh::Primitive::Type CircleBatch::getPrimitiveType() const
	{
		return usingPointSprites() ? mesh::Primitive::Type::Points : mesh::Primitive::Type::Triangles;
	}

	bool CircleBatch::indexedVertices() const
	{
		return !usingPointSprites();
	}

	/*
	 * Create indices for an object.
	 *
	 */
	void CircleBatch::createIndexData(vector<uint8_t>& data, uint32_t start, size_t count)
	{
		if (count == 0)
		{
			return;
		}

		size_t vertexSize{ 6 * (mIndexWidth / 8) };
		data.resize(count * vertexSize);

		uint32_t* ptr = (uint32_t*)&data[start * vertexSize]; // Indices will be 16 or 32-bit, so use 32 to cover both
		int indexBytes = (int)mIndexWidth / 8;

		for (uint32_t i = start; i < count; ++i)
		{
			auto x = i * 4;

			if (indexBytes == 2)
			{
				*ptr++ = (x + 0) + ((x + 1) << 16);
				*ptr++ = (x + 2) + ((x + 2) << 16);
				*ptr++ = (x + 3) + ((x + 0) << 16);
			}
			else if (indexBytes == 4)
			{
				*ptr++ = x + 0;
				*ptr++ = x + 1;
				*ptr++ = x + 2;
				*ptr++ = x + 2;
				*ptr++ = x + 3;
				*ptr++ = x + 0;
			}
		}
	}

	mesh::MeshSpecification CircleBatch::createMeshSpecification(mesh::Primitive::Type primitiveType)
	{
		auto meshSpec = mesh::MeshSpecification(primitiveType);
		auto layout = meshSpec.createVertexBufferAttributeLayout(false);

		if (usingPointSprites())
		{
			layout->createAttribute(mesh::Vertex::Component::Position2, mOptions.positionType, false);
		}
		else
		{
			layout->createAttribute(mesh::Vertex::Component::Position4, mOptions.positionType, false);
		}

		layout->createAttribute(mesh::Vertex::Component::UserDefined4, "OPTIONS", mesh::Vertex::DataType::Float, false);
		layout->createAttribute(mesh::Vertex::Component::UserDefined4, "BORDERCOLOUR", getColourAttribute().dataType, true);
		layout->createAttribute(mesh::Vertex::Component::UserDefined4, "INNERCOLOUR", getColourAttribute().dataType, true);

		return meshSpec;
	}

	void CircleBatch::addIndexedPrimitives(shared_ptr<ProgrammaticModelStream> ms, int meshIndex)
	{
		for (size_t i = 0; i < getCapacity(~0u); ++i)
		{
			auto x = (uint32_t)i * 4;
			ms->addTriangle(meshIndex, x + 0, x + 1, x + 2);
			ms->addTriangle(meshIndex, x + 2, x + 3, x + 0);
		}
	}

	uint32_t CircleBatch::getProgramFlags() const
	{
		uint32_t flags = usingPointSprites() ?
			MPP_PROGRAM_TAGS_PRIM_POINTS : MPP_PROGRAM_TAGS_PRIM_TRIANGLES;

		return flags;
	}

	int CircleBatch::getIndexWidth() const
	{
		return (int)mIndexWidth;
	}

	size_t CircleBatch::getPrimitiveCount(size_t objectCount) const
	{
		return objectCount * (usingPointSprites() ? 1 : 2);
	}

	size_t CircleBatch::getVertexCount(size_t primitiveCount) const
	{
		// If using triangles, we expect primitiveCount to be a multiple of 2
		return primitiveCount * (usingPointSprites() ? 1 : 2);
	}

	bool CircleBatch::usingPointSprites() const
	{
		return mOptions.vertexOptions == CircleBatchOptions::VertexOptions::Points;
	}

	float CircleBatch::getRadius() const
	{
		return mRadius;
	}

}