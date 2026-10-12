#include "utils/FileSystem.h"

#include "mpp/Config.h"
#include "mpp/MppModelStream.h"
#include "mpp/ProgrammaticBasicMaterialStream.h"
#include "mpp/PbrMaterialStream.h"
#include "mpp/ProgrammaticTextureStream.h"
#include "mpp/ProgrammaticStringStream.h"
#include "mpp/ModelSerializer.h"
#include "mpp/ResourceManager.h"

#define FLAG_INDEXED_VERTICES 0x0001

using namespace std;

namespace mpp
{
	MppModelStream::MppModelStream(ResourceManager* resourceMgr, string const& filename)
		: ModelStream(resourceMgr)
		, mFilename(filename)
	{
	}

	MppModelStream::~MppModelStream()
	{
		for (auto it : mMeshDataDefinitions)
		{
			delete it;
		}
	}

	void MppModelStream::createChildResourceStreamsImpl()
	{
		ModelSerializer ser(getResourceMgr());
		auto reader = ser.getReader(mFilename);

		// Create material resources
		auto numMeshes = reader.getNumMeshes();
		for (size_t i = 0; i < numMeshes; ++i)
		{
			string matName;
			auto materialStream = reader.getMaterialByMeshId((uint32_t)i, &matName);

			// Fix up paths of files so that any relative paths have the
			// model file's directory prepended, and add the load image function
			for (auto entry: materialStream->getChildren())
			{
				auto rs = entry.second;
				rs->setFileBasePaths(utils::FileSystem::baseDirectory(mFilename));

				if (rs->getType() == "Texture")
				{
					static_cast<ProgrammaticTextureStream*>(rs.get())->setImageLoadFunction(getResourceMgr()->getImageLoadFunction());
				}
			}

			addChild(matName, materialStream);
		}
	}

	void MppModelStream::createMeshDataStreams()
	{
		ModelSerializer ser(getResourceMgr());
		ser.load(mFilename);

		// Create meshes
		for (size_t i = 0; i < ser.getMeshCount(); ++i)
		{
			auto dataStreamDef = new MeshDataStreamDefinition();
			mMeshDataDefinitions.push_back(dataStreamDef);

			dataStreamDef->name = ser.getName(i);

			dataStreamDef->material = ser.getMaterial(i);

			auto matResource = getChildren().at(dataStreamDef->material);
			auto basicStream = dynamic_cast<BasicMaterialStream*>(matResource.get());
			auto pbrStream = dynamic_cast<PbrMaterialStream*>(matResource.get());
			if (!basicStream && !pbrStream) THROW_MPP("Model material stream is neither BasicMaterial nor PbrMaterial.", __LINE__, __FILE__, __func__);
			dataStreamDef->specification = basicStream ? basicStream->getMeshSpecification() : pbrStream->getMeshSpecification();

			dataStreamDef->indexWidth = ser.getIndexWidth(i);
			dataStreamDef->indexData = ser.getIndexData(i);

			dataStreamDef->primitiveType = ser.getPrimitiveType(i);
			dataStreamDef->primitiveCount = ser.getPrimitiveCount(i);

			for (size_t j = 0; j < dataStreamDef->specification.getNumVertexBufferAttributeLayouts(); ++j)
			{
				auto layout = dataStreamDef->specification.getVertexBufferAttributeLayout((uint32_t)j);

				size_t vertexCount, vertexStride;
				shared_ptr<const int8_t> vertexData;

				ser.getVertexStream(i, j, &vertexCount, &vertexStride, &vertexData);

				// All buffers will have the same vertex count.
				dataStreamDef->vertexCount = vertexCount;

				int offset = 0;
				for (size_t k = 0; k < layout.getNumAttributes(); ++k)
				{
					VertexDataStreamDefinition vertexStreamDef;

					vertexStreamDef.data = vertexData;

					auto attrib = layout.getAttribute(k);

					vertexStreamDef.dataType = attrib.dataType;
					vertexStreamDef.offset = offset;
					vertexStreamDef.stride = (int)vertexStride;

					dataStreamDef->componentStreams[attrib.component] = vertexStreamDef;

					offset += (int)attrib.sizeInBytes();
				}
			}
		}
	}

	ModelStream::VertexDataStreamDefinition MppModelStream::getMeshDataStream(size_t meshIndex, mesh::Vertex::Component component) const
	{
		return mMeshDataDefinitions[meshIndex]->componentStreams.at(component);
	}

	size_t MppModelStream::getMeshIndexWidth(size_t meshIndex) const
	{
		return mMeshDataDefinitions[meshIndex]->indexWidth;
	}

	float MppModelStream::getMeshPointSize(size_t meshIndex) const
	{
		return mMeshDataDefinitions[meshIndex]->pointSize;
	}

	uint8_t const* MppModelStream::getMeshIndexData(size_t meshIndex) const
	{
		return mMeshDataDefinitions[meshIndex]->indexData.get();
	}

	string const& MppModelStream::getMeshName(size_t meshIndex) const
	{
		return mMeshDataDefinitions[meshIndex]->name;
	}

	string const& MppModelStream::getMeshMaterial(size_t meshIndex) const
	{
		return mMeshDataDefinitions[meshIndex]->material;
	}

	size_t MppModelStream::getNumMeshes() const
	{
		return mMeshDataDefinitions.size();
	}

	void MppModelStream::getMeshCounts(size_t meshIndex, size_t* primitiveCount, size_t* vertexCount)
	{
		*primitiveCount = mMeshDataDefinitions[meshIndex]->primitiveCount;
		*vertexCount = mMeshDataDefinitions[meshIndex]->vertexCount;
	}

	mesh::MeshSpecification const& MppModelStream::getMeshSpecification(size_t meshIndex) const
	{
		return mMeshDataDefinitions[meshIndex]->specification;
	}

	string MppModelStream::markUpMaterialName(string const& name, string const& material)
	{
		return name + "/" + material;
	}
}