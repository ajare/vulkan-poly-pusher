#include <algorithm>
#include <cassert>

#include <half/half.hpp>

#include "mpp/Config.h"
#include "mpp/GridModelStream.h"
#include "mpp/MppException.h"

using namespace std;

namespace mpp
{
	GridModelStream::GridModelStream(ResourceManager* resourceMgr, mesh::MeshSpecification const& meshSpec, string const& material, double width, double depth, size_t dimX, size_t dimZ, float textureRepeatU, float textureRepeatV)
		: PrimitiveModelStream(resourceMgr, meshSpec, material)
	{
		// Spheres always use indexed vertices.
		mMeshDataDefinition.specification.setIndexedVertices(true);

		uint32_t strideInBytes;
		auto componentOffsets = getComponentOffsets(strideInBytes);

		// Preallocate vertex buffer
		const size_t numVertices = (dimX + 1) * (dimZ + 1);
		auto bufferSize = strideInBytes * numVertices;

		mMeshDataDefinition.vertexData.resize(bufferSize);

		// Generate vertices
		double w2 = width / 2;
		double d2 = depth / 2;
		double dw = width / dimX;
		double dh = depth / dimZ;

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
				case mesh::Vertex::Component::Position2:
					for (size_t z = 0; z <= dimZ; ++z)
					{
						for (size_t x = 0; x <= dimX; ++x)
						{
							setData(offset, attrib.component, attrib.dataType, attrib.normalised, -w2 + dw * x, -d2 + dh * z);
							offset += strideInBytes;
						}
					}
					break;
				case mesh::Vertex::Component::Position3:
				case mesh::Vertex::Component::Position4:
					for (size_t z = 0; z <= dimZ; ++z)
					{
						for (size_t x = 0; x <= dimX; ++x)
						{
							setData(offset, attrib.component, attrib.dataType, attrib.normalised, -w2 + dw * x, 0.0, -d2 + dh * z);
							offset += strideInBytes;
						}
					}
					break;
				case mesh::Vertex::Component::Normal3:
				case mesh::Vertex::Component::Normal4:
					for (size_t z = 0; z <= dimZ; ++z)
					{
						for (size_t x = 0; x <= dimX; ++x)
						{
							setData(offset, attrib.component, attrib.dataType, attrib.normalised, 0.0, 1.0, 0.0);
							offset += strideInBytes;
						}
					}
					break;
				case mesh::Vertex::Component::TexCoord2:
				case mesh::Vertex::Component::TexCoord3:
				case mesh::Vertex::Component::TexCoord4:
					for (size_t z = 0; z <= dimZ; ++z)
					{
						for (size_t x = 0; x <= dimX; ++x)
						{
							double u = (double)(x * textureRepeatU);
							double v = (double)(z * textureRepeatV);

							// For non-integer types, we scale to [0,1].
							// TODO: need an 'is-floating-point' trait to do this properly.
							if (mesh::Vertex::isDataTypeFloatingPoint(attrib.dataType))
							{
								u /= dimX;
								v /= dimZ;
							}

							setData(offset, attrib.component, attrib.dataType, attrib.normalised, u, v);
							offset += strideInBytes;
						}
					}
					break;
				case mesh::Vertex::Component::Colour1:
					for (size_t z = 0; z <= dimZ; ++z)
					{
						for (size_t x = 0; x <= dimX; ++x)
						{
							setData(offset, attrib.component, attrib.dataType, attrib.normalised, 1.0);
							offset += strideInBytes;
						}
					}
					break;
				case mesh::Vertex::Component::Colour3:
				case mesh::Vertex::Component::Colour4:
					for (size_t z = 0; z <= dimZ; ++z)
					{
						for (size_t x = 0; x <= dimX; ++x)
						{
							setData(offset, attrib.component, attrib.dataType, attrib.normalised, 1.0, 1.0, 1.0);
							offset += strideInBytes;
						}
					}
					break;
				}
			}
		}

		// Faces
		for (size_t z = 0; z < dimZ; ++z)
		{
			for (size_t x = 0; x < dimX; ++x)
			{
				auto offset = (dimX + 1) * z + x;
				addTriangle((uint32_t)offset, (uint32_t)(offset + dimX + 1), (uint32_t)(offset + dimX + 2));
				addTriangle((uint32_t)(offset + dimX + 2), (uint32_t)(offset + 1), (uint32_t)offset);
			}
		}
	}
}