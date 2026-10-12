#include <cmath>

#include "mpp/LineBatch.h"
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
	LineBatch::LineBatch(string const& name,
		LineBatchOptions const& options,
		size_t initialCapacity,
		RenderSystem* renderSystem,
		ResourceManager* resourceMgr)
		: Batch(name, initialCapacity, VertexShader2dTemplate, FragmentShader2dTemplate, "line", options.colourAttrib, options.useDiffuse, renderSystem, resourceMgr)
		, mOptions(options)
	{
	}

	/*
	 * Create mesh specification.
	 *
	 */
	mesh::MeshSpecification LineBatch::createMeshSpecification(mesh::Primitive::Type primitiveType)
	{
		auto meshSpec = mesh::MeshSpecification(primitiveType);
		auto dynamicLayout = meshSpec.createVertexBufferAttributeLayout(false);

		dynamicLayout->createAttribute(mesh::Vertex::Component::Position2, mOptions.positionType, false);

		if (mOptions.colourAttrib.dataType != mesh::Vertex::DataType::None)
		{
			mesh::VertexBufferAttributeLayout* colourLayout{ dynamicLayout };

			if (mOptions.colourAttrib.fixedValues)
			{
				colourLayout = meshSpec.createVertexBufferAttributeLayout(true);
			}

			colourLayout->createAttribute(mesh::Vertex::Component::Colour4, mOptions.colourAttrib.dataType, true);
		}

		return meshSpec;
	}

	/*
	 * We don't use indexed vertices for line batches, even though we could.
	 *
	 */
	bool LineBatch::indexedVertices() const
	{
		return false;
	}

	mesh::Primitive::Type LineBatch::getPrimitiveType() const
	{
		return mesh::Primitive::Type::Lines;
	}

	uint32_t LineBatch::getProgramFlags() const
	{
		uint32_t flags = MPP_PROGRAM_TAGS_PRIM_LINES
			| (usingDiffuse() ? MPP_PROGRAM_TAGS_DIFFUSE : 0);

		return flags;
	}

	int LineBatch::getIndexWidth() const
	{
		return 0;
	}

	size_t LineBatch::getPrimitiveCount(size_t objectCount) const
	{
		return objectCount;
	}

	/*
	 * Get the number of vertices required, given the number of primitives.
	 *
	 */
	size_t LineBatch::getVertexCount(size_t primitiveCount) const
	{
		return primitiveCount * 2;
	}


	bool LineBatch::positionFixed() const
	{
		return false;
	}

	bool LineBatch::colourFixed() const
	{
		return mOptions.colourAttrib.fixedValues;
	}

}