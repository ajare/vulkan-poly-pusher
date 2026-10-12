#include <algorithm>
#include <cassert>

#include "mpp/Config.h"
#include "mpp/SphereModelStream.h"

using namespace std;

namespace mpp
{
	SphereModelStream::SphereModelStream(ResourceManager* resourceMgr, mesh::MeshSpecification const& meshSpec, string const& material, double radius, int res)
		: PrimitiveModelStream(resourceMgr, meshSpec, material)
		, mRadius(radius)
		, mResolution(res)
	{
		// Spheres always use indexed vertices.
		mMeshDataDefinition.specification.setIndexedVertices(true);

		uint32_t strideInBytes;
		auto componentOffsets = getComponentOffsets(strideInBytes);

		// Generate triangle indices first, as they are needed for subdividing
		// the position data.
		addTriangle(1, 4, 0);
		addTriangle(4, 9, 0);
		addTriangle(4, 5, 9);
		addTriangle(8, 5, 4);
		addTriangle(1, 8, 4);
		addTriangle(1, 10, 8);
		addTriangle(10, 3, 8);
		addTriangle(8, 3, 5);
		addTriangle(3, 2, 5);
		addTriangle(3, 7, 2);
		addTriangle(3, 10, 7);
		addTriangle(10, 6, 7);
		addTriangle(6, 11, 7);
		addTriangle(6, 0, 11);
		addTriangle(6, 1, 0);
		addTriangle(10, 1, 6);
		addTriangle(11, 0, 9);
		addTriangle(2, 11, 9);
		addTriangle(5, 2, 9);
		addTriangle(11, 2, 7);

		// Generate vertices
		double x = 0.525731112119133606;
		double z = 0.850650808352039932;

		vector<double> positions = 
		{ 
			-x, 0, z,
			x, 0, z,
			-x, 0, -z,
			x, 0, -z,
			0, z, x,
			0, z, -x,
			0, -z, x,
			0, -z, -x,
			z, x, 0,
			-z, x, 0,
			z, -x, 0,
			-z, -x, 0 
		};

		for (int i = 1; i < res; ++i)
		{
			subdivide(positions);
		}

		renormalise(positions);

		// Preallocate vertex buffer
		size_t bufferSize = strideInBytes * (positions.size() / 3);
		mMeshDataDefinition.vertexData.resize(bufferSize);

		// Set vertex data
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
					for (size_t i = 0; i < positions.size(); i += 3, offset += strideInBytes)
					{
						setData(offset, attrib.component, attrib.dataType, attrib.normalised, positions[i + 0], positions[i + 1], positions[i + 2]);
					}
					break;
				case mesh::Vertex::Component::Normal3:
				case mesh::Vertex::Component::Normal4:
					for (size_t i = 0; i < positions.size(); i += 3, offset += strideInBytes)
					{
						auto len = sqrt(positions[i + 0] * positions[i + 0] +
							positions[i + 1] * positions[i + 1] +
							positions[i + 2] * positions[i + 2]);

						setData(offset, attrib.component, attrib.dataType, attrib.normalised, positions[i + 0] / len, positions[i + 1] / len, positions[i + 2] / len);
					}
					break;
				case mesh::Vertex::Component::TexCoord2:
				case mesh::Vertex::Component::TexCoord3:
				case mesh::Vertex::Component::TexCoord4:
					for (size_t i = 0; i < positions.size(); i += 3, offset += strideInBytes)
					{
						auto len = sqrt(positions[i + 0] * positions[i + 0] +
							positions[i + 1] * positions[i + 1] +
							positions[i + 2] * positions[i + 2]);

						double u, v;
						getUvCoord(positions[i + 0] / len, positions[i + 1] / len, positions[i + 2] / len, &u, &v);
						setData(offset, attrib.component, attrib.dataType, attrib.normalised, u, v);
					}
					break;
				case mesh::Vertex::Component::Colour1:
					for (size_t i = 0; i < positions.size(); i += 3, offset += strideInBytes)
					{
						setData(offset, attrib.component, attrib.dataType, attrib.normalised, 1.0);
					}
					break;
				case mesh::Vertex::Component::Colour3:
				case mesh::Vertex::Component::Colour4:
					for (size_t i = 0; i < positions.size(); i += 3, offset += strideInBytes)
					{
						setData(offset, attrib.component, attrib.dataType, attrib.normalised, 1.0, 1.0, 1.0);
					}
					break;
				}
			}
		}
	}

	void SphereModelStream::subdivide(vector<double>& positions)
	{
		map<uint64_t, uint32_t> midpointIndices;

		int indicesToProcess = (int)mMeshDataDefinition.indexData.size();
		for (int i = 0; i < indicesToProcess; i += 3)
		{
			uint32_t i0 = mMeshDataDefinition.indexData[i + 0];
			uint32_t i1 = mMeshDataDefinition.indexData[i + 1];
			uint32_t i2 = mMeshDataDefinition.indexData[i + 2];

			uint32_t m01 = getMidpointIndex(midpointIndices, positions, i0, i1);
			uint32_t m12 = getMidpointIndex(midpointIndices, positions, i1, i2);
			uint32_t m20 = getMidpointIndex(midpointIndices, positions, i2, i0);

			// Add new indices
			mMeshDataDefinition.indexData[i + 0] = i0;
			mMeshDataDefinition.indexData[i + 1] = m01;
			mMeshDataDefinition.indexData[i + 2] = m20;

			mMeshDataDefinition.indexData.push_back(i1);
			mMeshDataDefinition.indexData.push_back(m12);
			mMeshDataDefinition.indexData.push_back(m01);

			mMeshDataDefinition.indexData.push_back(i2);
			mMeshDataDefinition.indexData.push_back(m20);
			mMeshDataDefinition.indexData.push_back(m12);

			mMeshDataDefinition.indexData.push_back(m20);
			mMeshDataDefinition.indexData.push_back(m01);
			mMeshDataDefinition.indexData.push_back(m12);
		}
	}

	uint32_t SphereModelStream::getMidpointIndex(map<uint64_t, uint32_t>& midpointIndices, vector<double>& positions, uint32_t i0, uint32_t i1)
	{
		uint64_t key = min<uint64_t>(i0, i1) << 32;
		key |= max(i0, i1);

		auto it = midpointIndices.find(key);
		if (it == midpointIndices.end())
		{
			uint32_t index = (uint32_t)positions.size() / 3;
			midpointIndices[key] = index;

			for (size_t i = 0; i < 3; ++i)
			{
				double v = (positions[i0 * 3 + i] + positions[i1 * 3 + i]) / 2.0f;
				positions.push_back(v);
			}

			return index;
		}
		else
		{
			return it->second;
		}
	}

	void SphereModelStream::renormalise(std::vector<double>& positions)
	{
		for (size_t i = 0; i < positions.size(); i += 3)
		{
			double& x = positions[i + 0];
			double& y = positions[i + 1];
			double& z = positions[i + 2];

			auto d = mRadius / sqrt(x * x + y * y + z * z);
			x *= d;
			y *= d;
			z *= d;
		}
	}

	void SphereModelStream::getUvCoord(double nx, double ny, double nz, double* u, double* v)
	{
		double phi = ny;
		double lambda;
		if (ny == 1.0 || ny == -1.0)
		{
			// At a pole.
			lambda = 0.0;
		}
		else
		{
			lambda = atan2(nz, nx);
		}

		*u = -((lambda / 3.14159) * 0.5 + 0.5);
		*u *= 2; // stretch for 2-1 ratio;

		*v = tan(phi);
	}
}