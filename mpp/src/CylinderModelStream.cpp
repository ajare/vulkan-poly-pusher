#include <algorithm>
#include <cassert>

#include "mpp/Config.h"
#include "mpp/CylinderModelStream.h"

using namespace std;

namespace mpp
{
	CylinderModelStream::CylinderModelStream(ResourceManager* resourceMgr, mesh::MeshSpecification const& meshSpec, string const& material, float length, float radius1, float radius2, int res)
		: PrimitiveModelStream(resourceMgr, meshSpec, material)
		, mResolution(res)
	{
		// Cylinders always use indexed vertices.
		mMeshDataDefinition.specification.setIndexedVertices(true);

		uint32_t strideInBytes;
		auto componentOffsets = getComponentOffsets(strideInBytes);

		// Preallocate vertex buffer
		const int numVertices = (res + 1) * 4 + 2; // Two extra for caps centres.
		auto bufferSize = strideInBytes * numVertices;

		mMeshDataDefinition.vertexData.resize(bufferSize);

		// Generate vertices
		double l2 = length / 2;

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
					// Top cap
					setData(offset, attrib.component, attrib.dataType, attrib.normalised, 0.0, l2, 0.0);
					offset += strideInBytes;
					
					// Bottom cap
					setData(offset, attrib.component, attrib.dataType, attrib.normalised, 0.0, -l2, 0.0);
					offset += strideInBytes;

					// Sides
					for (int i = 0; i <= res; ++i)
					{
						float nx, nz, x1, x2, z1, z2, angleInc = 2 * 3.14159f / res;

						nx = sin(angleInc * i);
						nz = cos(angleInc * i);

						x1 = nx * radius1;
						z1 = nz * radius1;

						x2 = nx * radius2;
						z2 = nz * radius2;

						setData(offset, attrib.component, attrib.dataType, attrib.normalised, x1, l2, z1);
						offset += strideInBytes;

						setData(offset, attrib.component, attrib.dataType, attrib.normalised, x1, l2, z1);
						offset += strideInBytes;

						setData(offset, attrib.component, attrib.dataType, attrib.normalised, x2, -l2, z2);
						offset += strideInBytes;

						setData(offset, attrib.component, attrib.dataType, attrib.normalised, x2, -l2, z2);
						offset += strideInBytes;
					}
					break;
				case mesh::Vertex::Component::Normal3:
				case mesh::Vertex::Component::Normal4:
					// Top cap
					setData(offset, attrib.component, attrib.dataType, attrib.normalised, 0.0, 1.0, 0.0);
					offset += strideInBytes;

					// Top cap
					setData(offset, attrib.component, attrib.dataType, attrib.normalised, 0.0, -1.0, 0.0);
					offset += strideInBytes;

					// Sides
					for (int i = 0; i <= res; ++i)
					{
						double nx, nz, angleInc = 2 * 3.14159 / res;

						nx = sin(angleInc * i);
						nz = cos(angleInc * i);

						setData(offset, attrib.component, attrib.dataType, attrib.normalised, 0, 1, 0);
						offset += strideInBytes;

						setData(offset, attrib.component, attrib.dataType, attrib.normalised, nx, 0, nz);
						offset += strideInBytes;

						setData(offset, attrib.component, attrib.dataType, attrib.normalised, 0, -1, 0);
						offset += strideInBytes;

						setData(offset, attrib.component, attrib.dataType, attrib.normalised, nx, 0, nz);
						offset += strideInBytes;
					}
					break;
				case mesh::Vertex::Component::TexCoord2:
				case mesh::Vertex::Component::TexCoord3:
				case mesh::Vertex::Component::TexCoord4:
					// Top cap
					setData(offset, attrib.component, attrib.dataType, attrib.normalised, 0.5, 0.5);
					offset += strideInBytes;

					// Bottom cap
					setData(offset, attrib.component, attrib.dataType, attrib.normalised, 0.5, 0.5);
					offset += strideInBytes;

					// Sides
					for (int i = 0; i <= res; ++i)
					{
						double angleInc = 2 * 3.14159 / res;
						double u = sin(angleInc) * 0.5 + 0.5;
						double v = cos(angleInc) * 0.5 + 0.5;

						setData(offset, attrib.component, attrib.dataType, attrib.normalised, u, v);
						offset += strideInBytes;

						setData(offset, attrib.component, attrib.dataType, attrib.normalised, i / (double)res, 1);
						offset += strideInBytes;

						setData(offset, attrib.component, attrib.dataType, attrib.normalised, u, v);
						offset += strideInBytes;

						setData(offset, attrib.component, attrib.dataType, attrib.normalised, i / (double)res, 0);
						offset += strideInBytes;
					}
					break;
				case mesh::Vertex::Component::Colour1:
					// Top cap
					setData(offset, attrib.component, attrib.dataType, attrib.normalised, 1.0);
					offset += strideInBytes;

					// Bottom cap
					setData(offset, attrib.component, attrib.dataType, attrib.normalised, 1.0);
					offset += strideInBytes;

					// Sides
					for (int i = 0; i <= res; ++i)
					{
						setData(offset, attrib.component, attrib.dataType, attrib.normalised, 1.0);
						offset += strideInBytes;

						setData(offset, attrib.component, attrib.dataType, attrib.normalised, 1.0);
						offset += strideInBytes;

						setData(offset, attrib.component, attrib.dataType, attrib.normalised, 1.0);
						offset += strideInBytes;

						setData(offset, attrib.component, attrib.dataType, attrib.normalised, 1.0);
						offset += strideInBytes;
					}
					break;
				case mesh::Vertex::Component::Colour3:
				case mesh::Vertex::Component::Colour4:
					// Top cap
					setData(offset, attrib.component, attrib.dataType, attrib.normalised, 1.0, 1.0, 1.0);
					offset += strideInBytes;

					// Bottom cap
					setData(offset, attrib.component, attrib.dataType, attrib.normalised, 1.0, 1.0, 1.0);
					offset += strideInBytes;

					// Sides
					for (int i = 0; i <= res; ++i)
					{
						setData(offset, attrib.component, attrib.dataType, attrib.normalised, 1.0, 1.0, 1.0);
						offset += strideInBytes;

						setData(offset, attrib.component, attrib.dataType, attrib.normalised, 1.0, 1.0, 1.0);
						offset += strideInBytes;

						setData(offset, attrib.component, attrib.dataType, attrib.normalised, 1.0, 1.0, 1.0);
						offset += strideInBytes;

						setData(offset, attrib.component, attrib.dataType, attrib.normalised, 1.0, 1.0, 1.0);
						offset += strideInBytes;
					}
					break;
				}
			}
		}
		
		// Top indices
		for (int i = 0; i < res; ++i)
		{
			addTriangle(0, 2 + i * 4, 2 + (i + 1) * 4);
		}

		// Bottom indices
		for (int i = 0; i < res; ++i)
		{
			addTriangle(1, 2 + i * 4 + 2, 2 + (i + 1) * 4 + 2);
		}

		// Side indices
		for (int i = 0; i < res; ++i)
		{
			auto v0 = 2 + i * 4 + 1;
			auto v1 = 2 + i * 4 + 3;
			auto v2 = 2 + (i + 1) * 4 + 3;
			auto v3 = 2 + (i + 1) * 4 + 1;
			
			addTriangle(v0, v1, v2);
			addTriangle(v2, v3, v0);
		}
	}
}