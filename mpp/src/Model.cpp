#include <format>
#include "mpp/Config.h"

#if defined(_WIN32)
#include <Windows.h>
#endif

#include <cassert>
#include <GL/glew.h>
#include <GL/gl.h>

#include <half/half.hpp>

#include "utils/StringUtils.h"

#include "mpp/Model.h"
#include "mpp/ModelStream.h"
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
	Model::Model(string const& name, RenderSystem* renderSystem, ResourceManager* resourceMgr, ResourceStreamPtr resourceStream)
		: Resource(name, "Model", renderSystem, resourceMgr, resourceStream)
	{
	}

	/*
	 * Destructor.
	 *
	 */
	Model::~Model()
	{
		destroy();
	}

	glm::vec3 Model::readPositionFromStream(int8_t const* stream, mesh::VertexBufferAttributeLayout::Attribute const& attrib)
	{
		glm::vec3 pos;

		int c;
		switch (attrib.component)
		{
		case Vertex::Component::Position2:
			c = 2;
			break;
		case Vertex::Component::Position3:
		case Vertex::Component::Position4:
			c = 3;
			break;
		default:
			throw MppException("Vertex attribute component not supported: " + Vertex::getComponentName(attrib.component));
		}

		switch (attrib.dataType)
		{
		case Vertex::DataType::Byte:
			for (int i = 0; i < c; ++i)
			{
				pos[i] = (float)(*(int8_t const*)(stream));
				if (attrib.normalised)
				{
					pos[i] /= 255.0f;
				}
				stream += Vertex::getDataTypeSize(attrib.dataType);
			}
		break;
		case Vertex::DataType::UnsignedByte:
			for (int i = 0; i < c; ++i)
			{
				pos[i] = (float)(*(uint8_t const*)(stream));
				if (attrib.normalised)
				{
					pos[i] /= 255.0f;
				}
				stream += Vertex::getDataTypeSize(attrib.dataType);
			}
			break;
		case Vertex::DataType::Short:
			for (int i = 0; i < c; ++i)
			{
				pos[i] = (float)(*(int16_t const*)(stream));
				if (attrib.normalised)
				{
					pos[i] /= 65535.0f;
				}
				stream += Vertex::getDataTypeSize(attrib.dataType);
			}
			break;
		case Vertex::DataType::UnsignedShort:
			for (int i = 0; i < c; ++i)
			{
				pos[i] = (float)(*(uint16_t const*)(stream));
				if (attrib.normalised)
				{
					pos[i] /= 65535.0f;
				}
				stream += Vertex::getDataTypeSize(attrib.dataType);
			}
			break;
		case Vertex::DataType::Int:
			for (int i = 0; i < c; ++i)
			{
				pos[i] = (float)(*(int32_t const*)(stream));
				if (attrib.normalised)
				{
					pos[i] /= 4294967296.0f;
				}
				stream += Vertex::getDataTypeSize(attrib.dataType);
			}
			break;
		case Vertex::DataType::UnsignedInt:
			for (int i = 0; i < c; ++i)
			{
				pos[i] = (float)(*(uint32_t const*)(stream));
				if (attrib.normalised)
				{
					pos[i] /= 4294967296.0f;
				}
				stream += Vertex::getDataTypeSize(attrib.dataType);
			}
			break;
		case Vertex::DataType::HalfFloat:
			for (int i = 0; i < c; ++i)
			{
				pos[i] = (float)(*(half_float::half const*)(stream));
				stream += Vertex::getDataTypeSize(attrib.dataType);
			}
			break;
		case Vertex::DataType::Float:
			for (int i = 0; i < c; ++i)
			{
				pos[i] = *(float const*)(stream);
				stream += Vertex::getDataTypeSize(attrib.dataType);
			}
			break;
		case Vertex::DataType::Double:
			for (int i = 0; i < c; ++i)
			{
				pos[i] = (float)(*(double const*)(stream));
				stream += Vertex::getDataTypeSize(attrib.dataType);
			}
			break;
		case Vertex::DataType::Int_2_10_10_10_REV:
			{
				uint32_t v = *(uint32_t const*)(stream);
				pos.x = (float)((v & 511) * -(int)(v & (1 << 9)));
				pos.y = (float)(((v >> 10) & 511) * -(int)(v & (1 << 19)));
				pos.z = (float)(((v >> 20) & 511) * -(int)(v & (1 << 29)));
				if (attrib.normalised)
				{
					pos.x /= 511.0f;
					pos.y /= 511.0f;
					pos.z /= 511.0f;
				}

				stream += Vertex::getDataTypeSize(attrib.dataType);
			}
			break;
		case Vertex::DataType::UnsignedInt_2_10_10_10_REV:
			{
				uint32_t v = *(uint32_t const*)(stream);
				pos.x = (float)(v & 1023);
				pos.y = (float)((v >> 10) & 1023);
				pos.z = (float)((v >> 20) & 1023);
				if (attrib.normalised)
				{
					pos.x /= 1023.0f;
					pos.y /= 1023.0f;
					pos.z /= 1023.0f;
				}

				stream += Vertex::getDataTypeSize(attrib.dataType);
			}
			break;
		default:
			throw MppException(std::format("Vertex attribute datatype not supported: {}", (uint32_t)attrib.dataType));
		}

		return pos;
	}


	/*
	 * Calculate bounds.
	 *
	 */
	void Model::calculateBounds(mesh::VertexBufferAttributeLayout::Attribute const& posAttr, mesh::VertexBufferDefinition const* bufferDef)
	{
		if (posAttr.attributeId != -1)
		{
			auto bufferData = bufferDef->getData().get() + (int)posAttr.offsetInBytes;
			for (int k = 0; k < bufferDef->getVertexCount(); ++k)
			{
				auto pos = readPositionFromStream(bufferData, posAttr);

				if (pos.x < mBounds[0].x)
				{
					mBounds[0].x = pos.x;
				}
				if (pos.y < mBounds[0].y)
				{
					mBounds[0].y = pos.y;
				}
				if (pos.z < mBounds[0].z)
				{
					mBounds[0].z = pos.z;
				}

				if (pos.x > mBounds[1].x)
				{
					mBounds[1].x = pos.x;
				}
				if (pos.y > mBounds[1].y)
				{
					mBounds[1].y = pos.y;
				}
				if (pos.z > mBounds[1].z)
				{
					mBounds[1].z = pos.z;
				}

				bufferData += bufferDef->getVertexStride();
			}
		}
	}

	/*
	 * Create the data required.
	 *
	 */
	void Model::createImpl()
	{
		ModelStream* mStr = dynamic_cast<ModelStream*>(getResourceStream().get());
		if (!mStr)
		{
			THROW_MPP("Could not cast to type 'ModelStream'", __LINE__, __FILE__, __func__);
		}

		auto resourceMgr = getResourceManager();

		// Initialise extents calcualation
		mBoundsCalculated = mStr->getCalculateBounds();
		if (mStr->getCalculateBounds())
		{
			mBounds[0].x = 1e10f;
			mBounds[0].y = 1e10f;
			mBounds[0].z = 1e10f;
			mBounds[1].x = -1e10f;
			mBounds[1].y = -1e10f;
			mBounds[1].z = -1e10f;
		}
		else
		{
			mBounds[0].x = 0.0f;
			mBounds[0].y = 0.0f;
			mBounds[0].z = 0.0f;
			mBounds[1].x = 0.0f;
			mBounds[1].y = 0.0f;
			mBounds[1].z = 0.0f;
		}

		// Set up meshes
		for (size_t i = 0; i < mStr->getNumMeshDefinitions(); ++i)
		{
			MeshDefinition* meshDef = mStr->getMeshDefinition(i);

			// If it's an MPP model, prepend name, as the material will be specific to it.
			string materialName = mStr->markUpMaterialName(getName(), meshDef->getMaterial());
			
			ResourcePtr	material = resourceMgr->getResource(materialName);
			acquireDependentResource(material);
			material->load();

			// Don't check vertex attribute mapping for internal resources as they may not
			// actually have a mapping yet.
			if (!utils::StringUtils::startsWith(getName(), "__mpp") &&
				!checkVertexAttributeMapping(material, meshDef))
			{
				THROW_MPP(
					std::format("Vertex attribute mismatch between material '{}' ({} attributes) and mesh '{}' ({} attributes) of model '{}'.",
						material->getName(), static_cast<Program*>(static_cast<Material*>(material.get())->getProgram().get())->getVertexAttributes().size(),
						meshDef->getName(), [&]() { size_t count = 0; for (size_t layout = 0; layout < meshDef->getNumVertexBufferDefinitions(); ++layout) count += meshDef->getVertexBufferDefinition(layout)->getNumAttributes(); return count; }(), getName()),
					__LINE__, __FILE__, __func__);
			}

			Mesh* mesh = nullptr;

			// Get index data and convert as required
			auto storageType = meshDef->getStorageType();
			auto primitiveType = meshDef->getPrimitiveType();
			int primitiveSize = (int)mesh::Primitive::size(primitiveType);
			int primitiveCount = meshDef->getNumPrimitives();
			float pointSize = meshDef->getPointSize();

			if (meshDef->isIndexed())
			{
				int indexWidth = meshDef->getIndexWidth();
				int indexWidthBytes = indexWidth / 8;
				int indexSize = primitiveCount * primitiveSize * indexWidthBytes;

				auto indexDataPtr = (uint8_t*)meshDef->getIndexData().get();

				vector<uint8_t> indexData;
				for (int i = 0; i < indexSize; ++i)
				{
					indexData.push_back(*indexDataPtr++);
				}

				mesh = new Mesh(getRenderSystem(), meshDef->getName(), material, primitiveType, primitiveCount, indexWidth, indexData, storageType, pointSize);
			}
			else
			{
				mesh = new Mesh(getRenderSystem(), meshDef->getName(), material, primitiveType, primitiveCount, storageType, pointSize);
			}
			mesh->setMaterialRevisionCounter(&mMaterialRevision);

			for (size_t j = 0; j < meshDef->getNumVertexBufferDefinitions(); ++j)
			{
				VertexBufferDefinition const* bufferDef = meshDef->getVertexBufferDefinition(j);

				VertexBuffer* buffer = mesh->createVertexBuffer(
					bufferDef->getVertexCount(),
					bufferDef->getVertexStride(),
					bufferDef->getStreaming(),
					false,
					bufferDef->getData());

				// Set attributes
				mesh::VertexBufferAttributeLayout::Attribute posAttr;
				for (size_t k = 0; k < bufferDef->getNumAttributes(); ++k)
				{
					auto const& attrib = bufferDef->getAttribute((int)k);
					buffer->setAttribute(
						attrib.attributeId,
						attrib.dataType,
						Vertex::getComponentSize(attrib.component),
						(int)attrib.offsetInBytes,
						attrib.normalised);

					// Get position attribute for extents calculation
					if (attrib.component == Vertex::Component::Position2 ||
						attrib.component == Vertex::Component::Position3 ||
						attrib.component == Vertex::Component::Position4)
					{
						posAttr = attrib;
					}
				}

				// Get all position data from vertexbuffer and calculate model extents
				if (mStr->getCalculateBounds())
				{
					calculateBounds(posAttr, bufferDef);
				}
			}

			mMeshes.push_back(mesh);
		}
	}

	/*
	 * Destroy the model data.
	 *
	 */
	void Model::destroyImpl()
	{
		for (auto it: mMeshes)
		{
			delete it;
		}
		
		mMeshes.clear();
		++mMaterialRevision;
	}

	/*
	 * Create OpenGL model.
	 *
	 */
	void Model::loadImpl()
	{
		for (auto it: mMeshes)
		{
			it->load();
		}
	}

	/*
	 * Destroy the OpenGL model.
	 *
	 */
	void Model::unloadImpl()
	{
		for (auto it: mMeshes)
		{
			it->unload();
		}
	}

	/*
	 * Check that the material's program vertex attributes match the mesh's.
	 *
	 */
	bool Model::checkVertexAttributeMapping(ResourcePtr material, MeshDefinition* meshDef)
	{
		// Get mesh attributes
		vector<size_t> meshComponentSizes;
		for (size_t i = 0; i < meshDef->getNumVertexBufferDefinitions(); ++i)
		{
			auto vbDef = meshDef->getVertexBufferDefinition(i);
			for (size_t j = 0; j < vbDef->getNumAttributes(); ++j)
			{
				auto attrib = vbDef->getAttribute((int)j);
				meshComponentSizes.push_back(Vertex::getComponentSize(attrib.component));
			}
		}

		// Compare against program attributes
		auto programRes = static_cast<Material*>(material.get())->getProgram();
		auto program = static_cast<Program*>(programRes.get());

		auto const& programAttrs = program->getVertexAttributes();
		if (meshComponentSizes.size() != programAttrs.size())
		{
			return false;
		}

		for (size_t i = 0; i < programAttrs.size(); ++i)
		{
			if (programAttrs[i].numComponents != meshComponentSizes[i])
			{
				return false;
			}
		}

		return true;
	}

	/*
	 * Get the number of triangles in the model
	 *
	 */
	int Model::getNumTriangles() const
	{
		int tris = 0;
		for (auto it : mMeshes)
		{
			tris += (int)it->getNumPrimitives();
		}

		return tris;
	}

	/*
	 * Get the number of meshes in the model
	 *
	 */
	int Model::getNumMeshes() const
	{
		return (int)mMeshes.size();
	}

	uint64_t Model::getMaterialRevision() const
	{
		return mMaterialRevision;
	}

	/*
	 * Get the indexed mesh.
	 *
	 */
	Mesh const* Model::getMesh(int index) const
	{
		assert(index >= 0 && "Model::getMesh() 'index' argument out of range!");
		return mMeshes[index];
	}

	/*
	 * Get the indexed mesh.
	 *
	 */
	Mesh* Model::getMesh(int index)
	{
		assert(index >= 0 && "Model::getMesh() 'index' argument out of range!");
		return mMeshes[index];
	}

	/*
	 * Get model extents
	 */
	void Model::getBounds(glm::vec3& bMin, glm::vec3& bMax) const
	{
		bMin = mBounds[0];
		bMax = mBounds[1];
	}

	/*
	 * Are the model extents a measurement of this model's geometry?
	 */
	bool Model::hasBounds() const
	{
		return mBoundsCalculated && mBounds[0].x <= mBounds[1].x &&
			mBounds[0].y <= mBounds[1].y && mBounds[0].z <= mBounds[1].z;
	}

	/*
	 * Set meshes to be dynamically updatable, overriding current settings.  The meshes will have to
	 * be reloaded if they are currently loaded.
	 */
	void Model::setMeshesDynamic()
	{
		for (auto const mesh: mMeshes)
		{
			mesh->setStorageType(mesh::VertexBufferStorageType::Dynamic);
		}
	}

	/*
	 * Set meshes to be static, overriding current settings.  The meshes will have to
	 * be reloaded if they are currently loaded.
	 */
	void Model::setMeshesStatic()
	{
		for (auto const mesh: mMeshes)
		{
			mesh->setStorageType(mesh::VertexBufferStorageType::Static);
		}
	}

	/*
	 * How many GL names are created?
	 *
	 */
	int Model::getIdCount() const
	{
		int c = 0;
		for (auto it : mMeshes)
		{
			c += it->getIdCount();
		}

		return c;
	}

	/*
	 * How many GL names are created?
	 *
	 */
	int Model::getLiveIdCount() const
	{
		int c = 0;
		for (auto it : mMeshes)
		{
			c += it->getLiveIdCount();
		}

		return c;
	}
}