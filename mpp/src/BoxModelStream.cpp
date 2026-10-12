#include <algorithm>
#include <cassert>

#include "mpp/Config.h"
#include "mpp/MppException.h"
#include "mpp/BoxModelStream.h"

using namespace std;

namespace mpp
{
	BoxModelStream::BoxModelStream(ResourceManager* resourceMgr, mesh::MeshSpecification const& meshSpec, string const& material, float width, float height, float depth)
		: PrimitiveModelStream(resourceMgr, meshSpec, material)
	{
		// Boxes always use indexed vertices.
		mMeshDataDefinition.specification.setIndexedVertices(true);

		uint32_t strideInBytes;
		auto componentOffsets = getComponentOffsets(strideInBytes);

		// Preallocate vertex buffer
		int verticesPerFace = (meshSpec.verticesIndexed() ? 4 : 6);
		const int numVertices = verticesPerFace * 6;
		auto bufferSize = strideInBytes * numVertices;

		mMeshDataDefinition.vertexData.resize(bufferSize);

		// Generate vertices
		float w2 = width / 2;
		float h2 = height / 2;
		float d2 = depth / 2;

		for (size_t i = 0; i < meshSpec.getNumVertexBufferAttributeLayouts(); ++i)
		{
			auto const& layout = meshSpec.getVertexBufferAttributeLayout((uint32_t)i);

			for (size_t j = 0; j < layout.getNumAttributes(); ++j)
			{
				auto const& attrib = layout.getAttribute(j);

				// Get offset and stride for component
				auto offset = (uint32_t)componentOffsets[mesh::Vertex::getComponentName(attrib.component)];

				switch (attrib.component)
				{
				case mesh::Vertex::Component::Position3:
				case mesh::Vertex::Component::Position4:
					// Top face
					setData(offset, attrib.component, attrib.dataType, attrib.normalised, -w2, h2, -d2); offset += strideInBytes;
					setData(offset, attrib.component, attrib.dataType, attrib.normalised, -w2, h2, d2); offset += strideInBytes;
					setData(offset, attrib.component, attrib.dataType, attrib.normalised, w2, h2, d2); offset += strideInBytes;
					setData(offset, attrib.component, attrib.dataType, attrib.normalised, w2, h2, -d2); offset += strideInBytes;

					// Bottom face
					setData(offset, attrib.component, attrib.dataType, attrib.normalised, -w2, -h2, -d2); offset += strideInBytes;
					setData(offset, attrib.component, attrib.dataType, attrib.normalised, w2, -h2, -d2); offset += strideInBytes;
					setData(offset, attrib.component, attrib.dataType, attrib.normalised, w2, -h2, d2); offset += strideInBytes;
					setData(offset, attrib.component, attrib.dataType, attrib.normalised, -w2, -h2, d2); offset += strideInBytes;

					// Front face
					setData(offset, attrib.component, attrib.dataType, attrib.normalised, -w2, -h2, d2); offset += strideInBytes;
					setData(offset, attrib.component, attrib.dataType, attrib.normalised, -w2, h2, d2); offset += strideInBytes;
					setData(offset, attrib.component, attrib.dataType, attrib.normalised, w2, h2, d2); offset += strideInBytes;
					setData(offset, attrib.component, attrib.dataType, attrib.normalised, w2, -h2, d2); offset += strideInBytes;

					// Back face
					setData(offset, attrib.component, attrib.dataType, attrib.normalised, -w2, -h2, -d2); offset += strideInBytes;
					setData(offset, attrib.component, attrib.dataType, attrib.normalised, w2, -h2, -d2); offset += strideInBytes;
					setData(offset, attrib.component, attrib.dataType, attrib.normalised, w2, h2, -d2); offset += strideInBytes;

					setData(offset, attrib.component, attrib.dataType, attrib.normalised, -w2, h2, -d2); offset += strideInBytes;

					// Right face
					setData(offset, attrib.component, attrib.dataType, attrib.normalised, w2, -h2, -d2); offset += strideInBytes;
					setData(offset, attrib.component, attrib.dataType, attrib.normalised, w2, -h2, d2); offset += strideInBytes;
					setData(offset, attrib.component, attrib.dataType, attrib.normalised, w2, h2, d2); offset += strideInBytes;

					setData(offset, attrib.component, attrib.dataType, attrib.normalised, w2, h2, -d2); offset += strideInBytes;

					// Left face
					setData(offset, attrib.component, attrib.dataType, attrib.normalised, -w2, -h2, -d2); offset += strideInBytes;
					setData(offset, attrib.component, attrib.dataType, attrib.normalised, -w2, h2, -d2); offset += strideInBytes;
					setData(offset, attrib.component, attrib.dataType, attrib.normalised, -w2, h2, d2); offset += strideInBytes;

					setData(offset, attrib.component, attrib.dataType, attrib.normalised, -w2, -h2, d2); offset += strideInBytes;
					if (!meshSpec.verticesIndexed())
					{
						setData(offset, attrib.component, attrib.dataType, attrib.normalised, -w2, -h2, -d2); offset += strideInBytes;
					}
					break;
				case mesh::Vertex::Component::Normal3:
				case mesh::Vertex::Component::Normal4:
					// Top face
					setData(offset, attrib.component, attrib.dataType, attrib.normalised, 0, 1, 0);	offset += strideInBytes;
					setData(offset, attrib.component, attrib.dataType, attrib.normalised, 0, 1, 0); offset += strideInBytes;
					setData(offset, attrib.component, attrib.dataType, attrib.normalised, 0, 1, 0); offset += strideInBytes;
					setData(offset, attrib.component, attrib.dataType, attrib.normalised, 0, 1, 0); offset += strideInBytes;

					// Bottom face
					setData(offset, attrib.component, attrib.dataType, attrib.normalised, 0, -1, 0); offset += strideInBytes;
					setData(offset, attrib.component, attrib.dataType, attrib.normalised, 0, -1, 0); offset += strideInBytes;
					setData(offset, attrib.component, attrib.dataType, attrib.normalised, 0, -1, 0); offset += strideInBytes;
					setData(offset, attrib.component, attrib.dataType, attrib.normalised, 0, -1, 0); offset += strideInBytes;

					// Front face
					setData(offset, attrib.component, attrib.dataType, attrib.normalised, 0, 0, 1); offset += strideInBytes;
					setData(offset, attrib.component, attrib.dataType, attrib.normalised, 0, 0, 1); offset += strideInBytes;
					setData(offset, attrib.component, attrib.dataType, attrib.normalised, 0, 0, 1); offset += strideInBytes;
					setData(offset, attrib.component, attrib.dataType, attrib.normalised, 0, 0, 1); offset += strideInBytes;

					// Back face
					setData(offset, attrib.component, attrib.dataType, attrib.normalised, 0, 0, -1); offset += strideInBytes;
					setData(offset, attrib.component, attrib.dataType, attrib.normalised, 0, 0, -1); offset += strideInBytes;
					setData(offset, attrib.component, attrib.dataType, attrib.normalised, 0, 0, -1); offset += strideInBytes;
					setData(offset, attrib.component, attrib.dataType, attrib.normalised, 0, 0, -1); offset += strideInBytes;

					// Right face
					setData(offset, attrib.component, attrib.dataType, attrib.normalised, 1, 0, 0); offset += strideInBytes;
					setData(offset, attrib.component, attrib.dataType, attrib.normalised, 1, 0, 0); offset += strideInBytes;
					setData(offset, attrib.component, attrib.dataType, attrib.normalised, 1, 0, 0); offset += strideInBytes;
					setData(offset, attrib.component, attrib.dataType, attrib.normalised, 1, 0, 0); offset += strideInBytes;

					// Left face
					setData(offset, attrib.component, attrib.dataType, attrib.normalised, -1, 0, 0); offset += strideInBytes;
					setData(offset, attrib.component, attrib.dataType, attrib.normalised, -1, 0, 0); offset += strideInBytes;
					setData(offset, attrib.component, attrib.dataType, attrib.normalised, -1, 0, 0); offset += strideInBytes;
					setData(offset, attrib.component, attrib.dataType, attrib.normalised, -1, 0, 0); offset += strideInBytes;
					break;
				case mesh::Vertex::Component::TexCoord2:
				case mesh::Vertex::Component::TexCoord3:
				case mesh::Vertex::Component::TexCoord4:
					// Top face
					setData(offset, attrib.component, attrib.dataType, attrib.normalised, 0, 0); offset += strideInBytes;
					setData(offset, attrib.component, attrib.dataType, attrib.normalised, 0, 1); offset += strideInBytes;
					setData(offset, attrib.component, attrib.dataType, attrib.normalised, 1, 1); offset += strideInBytes;
					setData(offset, attrib.component, attrib.dataType, attrib.normalised, 1, 0); offset += strideInBytes;

					// Bottom face
					setData(offset, attrib.component, attrib.dataType, attrib.normalised, 0, 0); offset += strideInBytes;
					setData(offset, attrib.component, attrib.dataType, attrib.normalised, 1, 0); offset += strideInBytes;
					setData(offset, attrib.component, attrib.dataType, attrib.normalised, 1, 1); offset += strideInBytes;
					setData(offset, attrib.component, attrib.dataType, attrib.normalised, 0, 1); offset += strideInBytes;

					// Front face
					setData(offset, attrib.component, attrib.dataType, attrib.normalised, 0, 0); offset += strideInBytes;
					setData(offset, attrib.component, attrib.dataType, attrib.normalised, 0, 1); offset += strideInBytes;
					setData(offset, attrib.component, attrib.dataType, attrib.normalised, 1, 1); offset += strideInBytes;
					setData(offset, attrib.component, attrib.dataType, attrib.normalised, 1, 0); offset += strideInBytes;

					// Back face
					setData(offset, attrib.component, attrib.dataType, attrib.normalised, 0, 0); offset += strideInBytes;
					setData(offset, attrib.component, attrib.dataType, attrib.normalised, 1, 0); offset += strideInBytes;
					setData(offset, attrib.component, attrib.dataType, attrib.normalised, 1, 1); offset += strideInBytes;
					setData(offset, attrib.component, attrib.dataType, attrib.normalised, 0, 1); offset += strideInBytes;

					// Right face
					setData(offset, attrib.component, attrib.dataType, attrib.normalised, 0, 0); offset += strideInBytes;
					setData(offset, attrib.component, attrib.dataType, attrib.normalised, 0, 1); offset += strideInBytes;
					setData(offset, attrib.component, attrib.dataType, attrib.normalised, 1, 1); offset += strideInBytes;
					setData(offset, attrib.component, attrib.dataType, attrib.normalised, 1, 0); offset += strideInBytes;

					// Left face
					setData(offset, attrib.component, attrib.dataType, attrib.normalised, 0, 0); offset += strideInBytes;
					setData(offset, attrib.component, attrib.dataType, attrib.normalised, 1, 0); offset += strideInBytes;
					setData(offset, attrib.component, attrib.dataType, attrib.normalised, 1, 1); offset += strideInBytes;
					setData(offset, attrib.component, attrib.dataType, attrib.normalised, 0, 1); offset += strideInBytes;
					break;
				case mesh::Vertex::Component::Colour1:
				case mesh::Vertex::Component::Colour3:
				case mesh::Vertex::Component::Colour4:
					// Top face
					setData(offset, attrib.component, attrib.dataType, attrib.normalised, 1, 1, 1); offset += strideInBytes;
					setData(offset, attrib.component, attrib.dataType, attrib.normalised, 1, 1, 1); offset += strideInBytes;
					setData(offset, attrib.component, attrib.dataType, attrib.normalised, 1, 1, 1); offset += strideInBytes;
					setData(offset, attrib.component, attrib.dataType, attrib.normalised, 1, 1, 1); offset += strideInBytes;

					// Bottom face
					setData(offset, attrib.component, attrib.dataType, attrib.normalised, 1, 1, 1); offset += strideInBytes;
					setData(offset, attrib.component, attrib.dataType, attrib.normalised, 1, 1, 1); offset += strideInBytes;
					setData(offset, attrib.component, attrib.dataType, attrib.normalised, 1, 1, 1); offset += strideInBytes;
					setData(offset, attrib.component, attrib.dataType, attrib.normalised, 1, 1, 1); offset += strideInBytes;

					// Front face
					setData(offset, attrib.component, attrib.dataType, attrib.normalised, 1, 1, 1); offset += strideInBytes;
					setData(offset, attrib.component, attrib.dataType, attrib.normalised, 1, 1, 1); offset += strideInBytes;
					setData(offset, attrib.component, attrib.dataType, attrib.normalised, 1, 1, 1); offset += strideInBytes;
					setData(offset, attrib.component, attrib.dataType, attrib.normalised, 1, 1, 1); offset += strideInBytes;

					// Back face
					setData(offset, attrib.component, attrib.dataType, attrib.normalised, 1, 1, 1); offset += strideInBytes;
					setData(offset, attrib.component, attrib.dataType, attrib.normalised, 1, 1, 1); offset += strideInBytes;
					setData(offset, attrib.component, attrib.dataType, attrib.normalised, 1, 1, 1); offset += strideInBytes;
					setData(offset, attrib.component, attrib.dataType, attrib.normalised, 1, 1, 1); offset += strideInBytes;

					// Right face
					setData(offset, attrib.component, attrib.dataType, attrib.normalised, 1, 1, 1); offset += strideInBytes;
					setData(offset, attrib.component, attrib.dataType, attrib.normalised, 1, 1, 1); offset += strideInBytes;
					setData(offset, attrib.component, attrib.dataType, attrib.normalised, 1, 1, 1); offset += strideInBytes;
					setData(offset, attrib.component, attrib.dataType, attrib.normalised, 1, 1, 1); offset += strideInBytes;

					// Left face
					setData(offset, attrib.component, attrib.dataType, attrib.normalised, 1, 1, 1); offset += strideInBytes;
					setData(offset, attrib.component, attrib.dataType, attrib.normalised, 1, 1, 1); offset += strideInBytes;
					setData(offset, attrib.component, attrib.dataType, attrib.normalised, 1, 1, 1); offset += strideInBytes;
					setData(offset, attrib.component, attrib.dataType, attrib.normalised, 1, 1, 1); offset += strideInBytes;
					break;
				}
			}
		}

		for (int i = 0; i < numVertices; i += verticesPerFace)
		{
			addTriangle(i + 0, i + 1, i + 2);
			if (meshSpec.verticesIndexed())
				addTriangle(i + 2, i + 3, i + 0);
			else
				addTriangle(i + 3, i + 4, i + 5);
		}
	}
}