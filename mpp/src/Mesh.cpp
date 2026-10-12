#if defined(_WIN32)
#include <Windows.h>
#endif

#include <cassert>
#include <GL/glew.h>
#include <GL/gl.h>

#include "mpp/Mesh.h"
#include "mpp/ModelStream.h"
#include "mpp/RenderSystem.h"
#include "mpp/GLErrorCheck.h"
#include "mpp/MppException.h"
#include "PersistentMappedBuffer.h"

using namespace std;

namespace mpp
{
	/*
	 * Constructor (non-indexed).
	 *
	 */
	Mesh::Mesh(RenderSystem* renderSystem, string const& name, ResourcePtr material, mesh::Primitive::Type type, size_t primitiveCount, mesh::VertexBufferStorageType storageType, float pointSize)
		: mName(name)
		, mVAO(0)
		, mIBO(0)
		, mStorageType(storageType)
		, mIndexWidth(0)
		, mIndexDataSize(0)
		, mPointSize(pointSize)
		, mwRenderSystem(renderSystem)
		, mMaterial(material)
		, mIsIndexed(false)
		, mPrimitiveCount(primitiveCount)
		, mIsLoaded(false)
	{
		setPrimitiveData(type);
	}

	/*
	 * Constructor (indexed).
	 *
	 */
	Mesh::Mesh(RenderSystem* renderSystem, string const& name, ResourcePtr material, mesh::Primitive::Type type, size_t primitiveCount, size_t indexWidth, vector<uint8_t> const& indices, mesh::VertexBufferStorageType storageType, float pointSize)
		: mName(name)
		, mVAO(0)
		, mIBO(0)
		, mStorageType(storageType)
		, mIndexWidth(indexWidth)
		, mIndexDataSize(indices.size())
		, mPointSize(pointSize)
		, mwRenderSystem(renderSystem)
		, mMaterial(material)
		, mIsIndexed(true)
		, mPrimitiveCount(primitiveCount)
		, mIsLoaded(false)
	{
		setPrimitiveData(type);
		mIndexData = indices;
	}

	/*
	 * Destructor.
	 *
	 */
	Mesh::~Mesh()
	{
		unload();

		for (auto vertexBuffer: mVertexBuffers)
		{
			delete vertexBuffer;
		}
	}

	/*
	 * How many GL names are created?
	 *
	 */
	int Mesh::getIdCount() const
	{
		return isIndexed() ? 2 : 1;
	}

	/*
	 * How many GL names are created?
	 *
	 */
	int Mesh::getLiveIdCount() const
	{
		int c = 0;
		GL_CHECK(c += glIsVertexArray(mVAO));

		if (isIndexed())
		{
			GL_CHECK(c += glIsBuffer(mIBO));
		}

		return c;
	}

	/*
	 * Get name, to be used to manipulate instances.
	 *
	 */
	string const& Mesh::getName() const
	{
		return mName;
	}

	/*
	 * Set the primitive type/size
	 *
	 */
	void Mesh::setPrimitiveData(mesh::Primitive::Type type)
	{
		mPrimitiveType = type;
		switch (mPrimitiveType)
		{
		case mesh::Primitive::Type::Points:
			mPrimitiveRenderType = GL_POINTS;
			mPrimitiveSize = 1;
			break;

		case mesh::Primitive::Type::Lines:
			mPrimitiveRenderType = GL_LINES;
			mPrimitiveSize = 2;
			break;

		case mesh::Primitive::Type::Triangles:
			mPrimitiveRenderType = GL_TRIANGLES;
			mPrimitiveSize = 3;
			break;
		}
	}

	/*
	 * Set material.
	 *
	 */
	void Mesh::setMaterial(ResourcePtr material)
	{
		if (mMaterial == material) return;
		mMaterial = material;
		if (mMaterialRevision) ++*mMaterialRevision;
	}

	/*
	 * Get material.
	 *
	 */
	ResourcePtr Mesh::getMaterial() const
	{
		return mMaterial;
	}

	/*
	 * Set index data.
	 *
	 */
	void Mesh::setIndexData(vector<uint8_t> const& indexData, size_t indexWidth)
	{
		auto numIndices = (uint32_t)(indexData.size() / (indexWidth >> 8));
		setIndexData((int8_t const*)&(indexData[0]), numIndices, indexWidth);
	}

	void Mesh::setIndexData(int8_t const* indexData, uint32_t numIndices, size_t indexWidth)
	{
		mIsIndexed = true;
		mIndexWidth = indexWidth;
		mIndexDataSize = numIndices * (indexWidth >> 3);
		
		if (mIndexData.size() < mIndexDataSize)
		{
			mIndexData.resize(mIndexDataSize);
		}

		memcpy(&(mIndexData[0]), indexData, mIndexDataSize);
	}

	/*
	 * Are this mesh's vertices rendered indexed?
	 *
	 */
	bool Mesh::isIndexed() const
	{
		return mIsIndexed;
	}

	bool Mesh::usesPersistentIndexMapping() const
	{
		return mIndexStreamBuffer && mIndexStreamBuffer->isPersistent();
	}

	/*
	 * Set the number of primitives to render.
	 *
	 */
	void Mesh::setNumPrimitives(size_t count)
	{
		mPrimitiveCount = count;
	}

	/*
	 * Get primitive count.
	 *
	 */
	size_t Mesh::getNumPrimitives() const
	{
		return mPrimitiveCount;
	}

	/*
	 * Set point size.
	 *
	 */
	void Mesh::setPointSize(float size)
	{
		mPointSize = size;
	}

	/*
	 * Get point size.
	 *
	 */
	float Mesh::getPointSize() const
	{
		return mPointSize;
	}

	/*
	 * Add a vertex buffer.
	 *
	 */
	VertexBuffer* Mesh::createVertexBuffer(size_t vertexCount, size_t vertexStride, bool streaming, bool staticData, shared_ptr<const int8_t> vertexData)
	{
		VertexBuffer* buf = new VertexBuffer(mwRenderSystem, mStorageType, vertexCount, vertexStride, streaming, staticData, vertexData);

		mVertexBuffers.push_back(buf);
		return buf;
	}

	/*
	 * Get number of vertex buffers in this mesh.
	 *
	 */
	size_t Mesh::getNumVertexBuffers() const
	{
		return mVertexBuffers.size();
	}

	/*
	 * Get indexed vertex buffer.
	 *
	 */
	VertexBuffer* Mesh::getVertexBuffer(int index)
	{
		assert((index >= 0 && index < (int)getNumVertexBuffers()) && "Mesh::getVertexBuffer() 'index' argument out of range!");
		return mVertexBuffers[index];
	}

	vector<VertexBuffer*> const& Mesh::getVertexBuffers() const
	{
		return mVertexBuffers;
	}

	/*
	 * Return index data for potential modification.
	 *
	 */
	vector<uint8_t>& Mesh::getIndexData()
	{
		return mIndexData;
	}

	/*
	 * Re-map index data.
	 *
	 */
	void Mesh::mapIndexData(size_t numPrimitives)
	{
		GL_CHECK(glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mIBO));

		// If the data has increased in size, then reallocate
		auto const indexStride = mesh::Primitive::size(mPrimitiveType) * (mIndexWidth / 8);
		if (indexStride != 0 && numPrimitives > SIZE_MAX / indexStride) THROW_MPP("Index buffer upload size overflow.", __LINE__, __FILE__, __func__);
		auto newSize = numPrimitives * indexStride;

		if (newSize > mIndexData.size()) THROW_MPP("Index buffer upload exceeds its CPU data.", __LINE__, __FILE__, __func__);
		if (mIndexStreamBuffer)
		{
			mIndexStreamBuffer->upload(newSize ? mIndexData.data() : nullptr, newSize, 0, newSize);
			mIBO = mIndexStreamBuffer->getBuffer();
			mIndexDataSize = max(mIndexDataSize, newSize);
		}
		else
		{
			allocateIndexData(numPrimitives);
		}
	}

	/*
	 * Re-map index data.
	 *
	 */
	void Mesh::mapIndexData(int startPrimitive, int numPrimitives)
	{
		GL_CHECK(glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mIBO));

		// If the data has increased in size, then reallocate
		if (startPrimitive < 0 || numPrimitives < 0) THROW_MPP("Index buffer update range cannot be negative.", __LINE__, __FILE__, __func__);
		auto const indexStride = mesh::Primitive::size(mPrimitiveType) * (mIndexWidth / 8);
		auto const endPrimitive = static_cast<size_t>(startPrimitive) + static_cast<size_t>(numPrimitives);
		if (endPrimitive < static_cast<size_t>(startPrimitive) || (indexStride != 0 && endPrimitive > SIZE_MAX / indexStride))
			THROW_MPP("Index buffer range upload size overflow.", __LINE__, __FILE__, __func__);
		size_t newSize = endPrimitive * indexStride;

		if (newSize > mIndexData.size()) THROW_MPP("Index buffer range upload exceeds its CPU data.", __LINE__, __FILE__, __func__);
		if (mIndexStreamBuffer)
		{
			size_t const completeSize = mIndexData.size();
			mIndexStreamBuffer->upload(completeSize ? mIndexData.data() : nullptr, completeSize, startPrimitive * indexStride, numPrimitives * indexStride);
			mIBO = mIndexStreamBuffer->getBuffer();
			mIndexDataSize = max(mIndexDataSize, completeSize);
		}
		else if (newSize > mIndexDataSize)
		{
			allocateIndexData(startPrimitive + numPrimitives);
		}
		else
		{
			GL_CHECK(glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, startPrimitive * indexStride, numPrimitives * indexStride, &(mIndexData[startPrimitive * indexStride])));
		}
	}

	/*
	 * Allocate index data storage.
	 *
	 */
	void Mesh::allocateIndexData(size_t numPrimitives)
	{
		GLenum glStorageType{ 0 };
		switch (mStorageType)
		{
		case mesh::VertexBufferStorageType::Static:
			glStorageType = GL_STATIC_DRAW;
			break;

		case mesh::VertexBufferStorageType::Dynamic:
			glStorageType = GL_DYNAMIC_DRAW;
			break;

		default:
			throw MppException("Unsupported VertexBufferStorageType value.");
		}

		auto const indexStride = mesh::Primitive::size(mPrimitiveType) * (mIndexWidth / 8);
		if (indexStride != 0 && numPrimitives > SIZE_MAX / indexStride) THROW_MPP("Index buffer allocation size overflow.", __LINE__, __FILE__, __func__);
		mIndexDataSize = numPrimitives * indexStride;
		if (mIndexDataSize > mIndexData.size()) THROW_MPP("Index buffer allocation exceeds its CPU data.", __LINE__, __FILE__, __func__);

		if (mStorageType == mesh::VertexBufferStorageType::Dynamic)
		{
			if (!mIndexStreamBuffer)
			{
				mIndexStreamBuffer = make_unique<detail::PersistentMappedBuffer>();
				mIndexStreamBuffer->create(GL_ELEMENT_ARRAY_BUFFER, max<size_t>(1, mIndexDataSize), max<size_t>(1, mIndexWidth / 8), mwRenderSystem->getCaps().streamingGeometry,
					mIndexDataSize ? mIndexData.data() : nullptr, mIndexDataSize, "Streaming Index Buffer: " + getName());
			}
			else
			{
				mIndexStreamBuffer->upload(mIndexDataSize ? mIndexData.data() : nullptr, mIndexDataSize, 0, mIndexDataSize);
			}
			mIBO = mIndexStreamBuffer->getBuffer();
		}
		else if (mIndexDataSize == 0)
		{
			GL_CHECK(glBufferData(GL_ELEMENT_ARRAY_BUFFER, 0, nullptr, glStorageType));
		}
		else
		{
			GL_CHECK(glBufferData(GL_ELEMENT_ARRAY_BUFFER, mIndexDataSize, &(mIndexData[0]), glStorageType));
		}
	}

	/*
	 * Load mesh into VRAM.
	 *
	 */
	void Mesh::load()
	{
		// Load into one vertex buffer
		GL_CHECK(glGenVertexArrays(1, &mVAO));
		GL_CHECK(glBindVertexArray(mVAO));

		// Set name for debugging
		string label = "VertexArray: " + getName();
		glObjectLabel(GL_VERTEX_ARRAY, mVAO, -1, label.c_str());

		// Create index buffer
		if (isIndexed())
		{
			if (mStorageType != mesh::VertexBufferStorageType::Dynamic)
			{
				GL_CHECK(glGenBuffers(1, &mIBO));
				GL_CHECK(glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mIBO));

				// Set name for debugging
				string label = "Buffer: " + getName();
				glObjectLabel(GL_BUFFER, mIBO, -1, label.c_str());
			}

			allocateIndexData(mPrimitiveCount);
			mConfiguredIndexBuffer = mIBO;
		}

		// Create vertex buffers
		for (auto vertexBuffer: mVertexBuffers)
		{
			vertexBuffer->load();
		}

		// Unbind VAO
		GL_CHECK(glBindVertexArray(0));

		// Load material
		mMaterial->load();

		mIsLoaded = true;
	}

	/*
	* Unload mesh from VRAM.
	*
	*/
	void Mesh::unload()
	{
		mConfiguredIndexBuffer = 0;
		if (mVAO != 0)
		{
			GL_CHECK(glDeleteVertexArrays(1, &mVAO));
			mVAO = 0;
		}
		if (mIndexStreamBuffer)
		{
			mIndexStreamBuffer.reset();
			mIBO = 0;
		}
		else if (mIBO != 0)
		{
			GL_CHECK(glDeleteBuffers(1, &mIBO));
			mIBO = 0;
		}

		for (auto vertexBuffer: mVertexBuffers)
		{
			vertexBuffer->unload();
		}

		mIsLoaded = false;
	}

	/*
	 * Bind the mesh for rendering.
	 *
	 */
	void Mesh::bind(bool use) const
	{
		if (use)
		{
			GL_CHECK(glBindVertexArray(mVAO));
			if (mIndexStreamBuffer)
			{
				if (mConfiguredIndexBuffer != mIndexStreamBuffer->getBuffer())
				{
					mIndexStreamBuffer->bind();
					mConfiguredIndexBuffer = mIndexStreamBuffer->getBuffer();
				}
				mIndexStreamBuffer->markUsed();
			}
			for (auto vertexBuffer : mVertexBuffers) vertexBuffer->prepareForRender();
		}
		else
		{
			GL_CHECK(glBindVertexArray(0));
		}
	}

	/*
	 * Send vertex data.
	 *
	 */
	size_t Mesh::getActiveIndexOffset() const
	{
		return mIndexStreamBuffer ? mIndexStreamBuffer->getActiveOffset() : 0;
	}

	void Mesh::render(size_t instanceCount) const
	{
		render(instanceCount, 0, mPrimitiveCount);
	}
		
	/*
	 * Send vertex data.
	 *
	 */
	void Mesh::render(size_t instanceCount, uint32_t start, size_t count) const
	{
		if (mIsIndexed)
		{
			GLenum indexType = mIndexWidth == 16 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT;
			auto offset = (void*)(intptr_t)(getActiveIndexOffset() + start * mPrimitiveSize * (mIndexWidth >> 3));

			if (instanceCount == 1)
			{
				GL_CHECK(glDrawElements(mPrimitiveRenderType, (GLsizei)(count * mPrimitiveSize), indexType, offset));
			}
			else
			{
				GL_CHECK(glDrawElementsInstanced(mPrimitiveRenderType, (GLsizei)(count * mPrimitiveSize), indexType, offset, (GLsizei)instanceCount));
			}
		}
		else
		{
			if (instanceCount == 1)
			{
				GL_CHECK(glDrawArrays(mPrimitiveRenderType, start, (GLsizei)(count * mPrimitiveSize)));
			}
			else
			{
				GL_CHECK(glDrawArraysInstanced(mPrimitiveRenderType, start, (GLsizei)(count * mPrimitiveSize), (GLsizei)instanceCount));
			}
		}
	}

	/*
	 * Set storage type.
	 *
	 */
	void Mesh::setStorageType(mesh::VertexBufferStorageType storageType)
	{
		mStorageType = storageType;

		if (mIsLoaded)
		{
			unload();
			load();
		}
	}

	/*
	 * Size (in bytes) of a vertex
	 *
	 */
	size_t Mesh::getVertexSize() const
	{
		size_t vertexSize = 0;
		for (auto const& vertexBuffer : mVertexBuffers)
		{
			vertexSize += vertexBuffer->getVertexStride();
		}

		return vertexSize;
	}
}