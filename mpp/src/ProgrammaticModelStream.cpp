#include <algorithm>
#include <cassert>
#include <cstring>

#include "mpp/Config.h"
#include "mpp/ProgrammaticModelStream.h"
#include "mpp/MppException.h"
#include <cstring>

using namespace std;

namespace mpp
{

	ProgrammaticModelStream::ProgrammaticModelStream(ResourceManager* resourceMgr)
		: ModelStream(resourceMgr)
	{
	}

	void ProgrammaticModelStream::createMeshDataStreams()
	{
		// Set component streams for each mesh
		for (MeshDataStreamDefinition& meshDef : mMeshDataDefinitions)
		{
			uint32_t vertexOffset = 0;
			size_t vertexStride = meshDef.specification.getVertexStrideInBytes();
			size_t srcVertexDataSize = meshDef.vertexData.size();

			// Set counts
			meshDef.vertexCount = srcVertexDataSize / vertexStride;

			auto elementSize = (int)mesh::Primitive::size(meshDef.specification.getPrimitiveType());
			int indexWidthBytes = (int)meshDef.indexWidth / 8;

			meshDef.primitiveCount = meshDef.specification.verticesIndexed() ? (meshDef.indexData.size() / (elementSize * indexWidthBytes)) : (meshDef.vertexCount / elementSize);

			// Go through each component in order, and build streams.
			auto dataPtr = new int8_t[srcVertexDataSize];
			if (srcVertexDataSize > 0)
			{
				memcpy(dataPtr, &(meshDef.vertexData[0]), srcVertexDataSize);
			}

			auto sharedDataPtr = std::shared_ptr<const int8_t>((const int8_t*)dataPtr, [](const int8_t *p) { delete[] p; });

			for (size_t i = 0; i < meshDef.specification.getNumVertexBufferAttributeLayouts(); ++i)
			{
				auto layout = meshDef.specification.getVertexBufferAttributeLayout((uint32_t)i);

				for (size_t j = 0; j < layout.getNumAttributes(); ++j)
				{
					auto attrib = layout.getAttribute(j);

					VertexDataStreamDefinition vertexStreamDef;

					vertexStreamDef.data = sharedDataPtr;
					vertexStreamDef.dataType = attrib.dataType;
					vertexStreamDef.offset = vertexOffset;
					vertexStreamDef.stride = (int)vertexStride;

					vertexOffset += (int)attrib.sizeInBytes();

					switch (attrib.component)
					{
					case mesh::Vertex::Component::Position4:
						meshDef.componentStreams[mpp::mesh::Vertex::Component::Position4] = vertexStreamDef;
						[[fallthrough]];

					case mesh::Vertex::Component::Position3:
						meshDef.componentStreams[mpp::mesh::Vertex::Component::Position3] = vertexStreamDef;
						[[fallthrough]];

					case mesh::Vertex::Component::Position2:
						meshDef.componentStreams[mpp::mesh::Vertex::Component::Position2] = vertexStreamDef;
						break;

					case mesh::Vertex::Component::TexCoord4:
						meshDef.componentStreams[mpp::mesh::Vertex::Component::TexCoord4] = vertexStreamDef;
						[[fallthrough]];

					case mesh::Vertex::Component::TexCoord3:
						meshDef.componentStreams[mpp::mesh::Vertex::Component::TexCoord3] = vertexStreamDef;
						[[fallthrough]];

					case mesh::Vertex::Component::TexCoord2:
						meshDef.componentStreams[mpp::mesh::Vertex::Component::TexCoord2] = vertexStreamDef;
						break;

					case mesh::Vertex::Component::Colour4:
						meshDef.componentStreams[mpp::mesh::Vertex::Component::Colour4] = vertexStreamDef;
						[[fallthrough]];

					case mesh::Vertex::Component::Colour3:
						meshDef.componentStreams[mpp::mesh::Vertex::Component::Colour3] = vertexStreamDef;
						[[fallthrough]];

					case mesh::Vertex::Component::Colour1:
						meshDef.componentStreams[mpp::mesh::Vertex::Component::Colour1] = vertexStreamDef;
						break;

					case mesh::Vertex::Component::Tangent4:
						meshDef.componentStreams[mpp::mesh::Vertex::Component::Tangent4] = vertexStreamDef;
						break;

					case mesh::Vertex::Component::Normal4:
						meshDef.componentStreams[mpp::mesh::Vertex::Component::Normal4] = vertexStreamDef;
						[[fallthrough]];

					case mesh::Vertex::Component::Normal3:
						meshDef.componentStreams[mpp::mesh::Vertex::Component::Normal3] = vertexStreamDef;
						break;

					case mesh::Vertex::Component::UserDefined4:
						meshDef.componentStreams[mpp::mesh::Vertex::Component::UserDefined4] = vertexStreamDef;
						[[fallthrough]];

					case mesh::Vertex::Component::UserDefined3:
						meshDef.componentStreams[mpp::mesh::Vertex::Component::UserDefined3] = vertexStreamDef;
						[[fallthrough]];

					case mesh::Vertex::Component::UserDefined2:
						meshDef.componentStreams[mpp::mesh::Vertex::Component::UserDefined2] = vertexStreamDef;
						[[fallthrough]];

					case mesh::Vertex::Component::UserDefined1:
						meshDef.componentStreams[mpp::mesh::Vertex::Component::UserDefined1] = vertexStreamDef;
						break;
					}
				}
			}
		}
	}

	size_t ProgrammaticModelStream::getNumMeshes() const
	{
		return mMeshDataDefinitions.size();
	}

	mesh::MeshSpecification const& ProgrammaticModelStream::getMeshSpecification(size_t meshIndex) const
	{
		return mMeshDataDefinitions[meshIndex].specification;
	}

	void ProgrammaticModelStream::getMeshCounts(size_t meshIndex, size_t* primitiveCount, size_t* vertexCount)
	{
		auto meshDef = mMeshDataDefinitions[meshIndex];

		*primitiveCount = meshDef.primitiveCount;
		*vertexCount = meshDef.vertexCount;
	}

	ModelStream::VertexDataStreamDefinition ProgrammaticModelStream::getMeshDataStream(size_t meshIndex, mesh::Vertex::Component component) const
	{
		return mMeshDataDefinitions[meshIndex].componentStreams.at(component);
	}

	size_t ProgrammaticModelStream::getMeshIndexWidth(size_t meshIndex) const
	{
		return mMeshDataDefinitions[meshIndex].indexWidth;
	}

	float ProgrammaticModelStream::getMeshPointSize(size_t meshIndex) const
	{
		return mMeshDataDefinitions[meshIndex].pointSize;
	}

	uint8_t const* ProgrammaticModelStream::getMeshIndexData(size_t meshIndex) const
	{
		auto const& indexData = mMeshDataDefinitions[meshIndex].indexData;

		if (!indexData.empty())
		{
			return &(mMeshDataDefinitions[meshIndex].indexData[0]);
		}
		else
		{
			return nullptr;
		}
	}

	string const& ProgrammaticModelStream::getMeshName(size_t meshIndex) const
	{
		return mMeshDataDefinitions[meshIndex].name;
	}

	string const& ProgrammaticModelStream::getMeshMaterial(size_t meshIndex) const
	{
		return mMeshDataDefinitions[meshIndex].material;
	}

	size_t ProgrammaticModelStream::createMesh(string const& name, mesh::MeshSpecification const& specification, string const& material, int indexWidth, float pointSize)
	{
		if (getMeshId(name) >= 0)
		{
			string errMsg = "Mesh '" + name + "' already defined in programmatic model stream.";
			THROW_MPP(errMsg, __LINE__, __FILE__, __func__);
		}

		if (specification.verticesIndexed() && indexWidth != 16 && indexWidth != 32)
		{
			THROW_MPP("Index width must be either 16 or 32.", __LINE__, __FILE__, __func__);
		}

		size_t index = mMeshDataDefinitions.size();

		MeshDataStreamDefinition meshDef;
		meshDef.specification = specification;
		meshDef.name = name;
		meshDef.indexWidth = indexWidth;
		meshDef.pointSize = pointSize;
		meshDef.material = material;

		mMeshDataDefinitions.push_back(meshDef);

		return index;
	}

	int32_t ProgrammaticModelStream::getMeshId(string const& name) const
	{
		for (int32_t i = 0; i < (int32_t)mMeshDataDefinitions.size(); ++i)
		{
			if (mMeshDataDefinitions[i].name == name)
			{
				return i;
			}
		}
		return -1;
	}

	void ProgrammaticModelStream::addVertexData(size_t meshIndex, vector<int8_t> const& vertexData)
	{
		MeshDataStreamDefinition& meshDef = mMeshDataDefinitions[meshIndex];
		meshDef.vertexData.insert(meshDef.vertexData.end(), begin(vertexData), end(vertexData));
	}

	void ProgrammaticModelStream::addVertexData(size_t meshIndex, mesh::VertexData const& vertexData)
	{
		addVertexData(meshIndex, vertexData.getData());
	}

	void ProgrammaticModelStream::addPoint(size_t meshIndex, uint32_t v)
	{
		MeshDataStreamDefinition& meshDef = mMeshDataDefinitions[meshIndex];

		if (meshDef.indexWidth == 16)
		{
			meshDef.indexData.push_back(v & 255);
			meshDef.indexData.push_back((v >> 8) & 255);
		}
		else
		{
			meshDef.indexData.push_back(v & 255);
			meshDef.indexData.push_back((v >> 8) & 255);
			meshDef.indexData.push_back((v >> 16) & 255);
			meshDef.indexData.push_back(v >> 24);
		}
	}

	void ProgrammaticModelStream::addLine(size_t meshIndex, uint32_t v0, uint32_t v1)
	{
		MeshDataStreamDefinition& meshDef = mMeshDataDefinitions[meshIndex];

		if (meshDef.indexWidth == 16)
		{
			meshDef.indexData.push_back(v0 & 255);
			meshDef.indexData.push_back((v0 >> 8) & 255);
			meshDef.indexData.push_back(v1 & 255);
			meshDef.indexData.push_back((v1 >> 8) & 255);
		}
		else
		{
			meshDef.indexData.push_back(v0 & 255);
			meshDef.indexData.push_back((v0 >> 8) & 255);
			meshDef.indexData.push_back((v0 >> 16) & 255);
			meshDef.indexData.push_back(v0 >> 24);
			meshDef.indexData.push_back(v1 & 255);
			meshDef.indexData.push_back((v1 >> 8) & 255);
			meshDef.indexData.push_back((v1 >> 16) & 255);
			meshDef.indexData.push_back(v1 >> 24);
		}
	}

	void ProgrammaticModelStream::addTriangle(size_t meshIndex, uint32_t v0, uint32_t v1, uint32_t v2)
	{
		MeshDataStreamDefinition& meshDef = mMeshDataDefinitions[meshIndex];

		if (meshDef.indexWidth == 16)
		{
			meshDef.indexData.push_back(v0 & 255);
			meshDef.indexData.push_back((v0 >> 8) & 255);
			meshDef.indexData.push_back(v1 & 255);
			meshDef.indexData.push_back((v1 >> 8) & 255);
			meshDef.indexData.push_back(v2 & 255);
			meshDef.indexData.push_back((v2 >> 8) & 255);
		}
		else
		{
			meshDef.indexData.push_back(v0 & 255);
			meshDef.indexData.push_back((v0 >> 8) & 255);
			meshDef.indexData.push_back((v0 >> 16) & 255);
			meshDef.indexData.push_back(v0 >> 24);
			meshDef.indexData.push_back(v1 & 255);
			meshDef.indexData.push_back((v1 >> 8) & 255);
			meshDef.indexData.push_back((v1 >> 16) & 255);
			meshDef.indexData.push_back(v1 >> 24);
			meshDef.indexData.push_back(v2 & 255);
			meshDef.indexData.push_back((v2 >> 8) & 255);
			meshDef.indexData.push_back((v2 >> 16) & 255);
			meshDef.indexData.push_back(v2 >> 24);
		}
	}
}
