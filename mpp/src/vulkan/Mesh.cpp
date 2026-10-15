// Native Vulkan implementation of the frozen Mesh public API. CPU-side
// behaviour (validation, CPU copies, primitive vocabulary) is retained from
// the reference; index/vertex storage is realized as device-local native
// buffers with staged uploads and completion-based deferred destruction.
// Drawing, GL bindings and persistent mapping are not delivered and fail
// explicitly; material resource realization arrives with the material slice.
#include <cassert>
#include <cstring>
#include <stdexcept>

#include "mpp/Mesh.h"
#include "mpp/RenderSystem.h"
#include "mpp/MppException.h"
#include "../PersistentMappedBuffer.h"
#include "WindowBridge.h"
#include "GeometryStore.h"
#include "LegacyGpuValues.h"

using namespace std;

namespace mpp
{
	namespace
	{
		detail::GeometryStore& geometryStore()
		{
			return detail::associatedWindow()->geometry();
		}

		// Opaque-ID liveness is a diagnostic; after renderer/window teardown the
		// device (and every native buffer with it) is already gone.
		bool liveId(uint32_t id)
		{
			if (!id) return false;
			try { return geometryStore().isLive(id); }
			catch (...) { return false; }
		}
	}

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
	 * How many native buffer IDs this mesh owns. There is no vertex-array
	 * object in the native backend; counts cover vertex buffers plus the index
	 * buffer (see M01-geometry-upload.md).
	 *
	 */
	int Mesh::getIdCount() const
	{
		return int(mVertexBuffers.size()) + (isIndexed() ? 1 : 0);
	}

	/*
	 * How many native buffer IDs are currently realized.
	 *
	 */
	int Mesh::getLiveIdCount() const
	{
		int c = 0;
		if (isIndexed() && liveId(mIBO)) ++c;
		for (auto vertexBuffer : mVertexBuffers)
		{
			if (liveId(vertexBuffer->mVBO)) ++c;
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
	 * Set the primitive type/size. The retained numeric topology vocabulary is
	 * translated at pipeline realization (#95), not here.
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
	 * Set index data. Note: the reference computed the element count with
	 * (indexWidth >> 8), a baseline-defect candidate (BD-4); the intended
	 * byte-width conversion (>> 3) is used here.
	 *
	 */
	void Mesh::setIndexData(vector<uint8_t> const& indexData, size_t indexWidth)
	{
		if (!indexWidth || (indexWidth & 7)) THROW_MPP("Index width must be a whole number of bytes.", __LINE__, __FILE__, __func__);
		auto numIndices = (uint32_t)(indexData.size() / (indexWidth >> 3));
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
		// Truthful: persistent-mapped streaming indices are not delivered; all
		// uploads are staged. See M01-geometry-upload.md.
		return false;
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
	 * Re-map index data: staged full replacement (growing storage if needed).
	 *
	 */
	void Mesh::mapIndexData(size_t numPrimitives)
	{
		if (!mIBO) THROW_MPP("Mesh index buffer is not loaded; call Mesh::load() before mapping.", __LINE__, __FILE__, __func__);

		// If the data has increased in size, then reallocate
		auto const indexStride = mesh::Primitive::size(mPrimitiveType) * (mIndexWidth / 8);
		if (indexStride != 0 && numPrimitives > SIZE_MAX / indexStride) THROW_MPP("Index buffer upload size overflow.", __LINE__, __FILE__, __func__);
		auto newSize = numPrimitives * indexStride;

		if (newSize > mIndexData.size()) THROW_MPP("Index buffer upload exceeds its CPU data.", __LINE__, __FILE__, __func__);
		if (newSize > mIndexDataSize)
		{
			allocateIndexData(numPrimitives);
		}
		else
		{
			geometryStore().upload(mIBO, newSize ? mIndexData.data() : nullptr, newSize, 0);
		}
	}

	/*
	 * Re-map index data: staged update of the specified primitive range.
	 *
	 */
	void Mesh::mapIndexData(int startPrimitive, int numPrimitives)
	{
		if (!mIBO) THROW_MPP("Mesh index buffer is not loaded; call Mesh::load() before mapping.", __LINE__, __FILE__, __func__);

		// If the data has increased in size, then reallocate
		if (startPrimitive < 0 || numPrimitives < 0) THROW_MPP("Index buffer update range cannot be negative.", __LINE__, __FILE__, __func__);
		auto const indexStride = mesh::Primitive::size(mPrimitiveType) * (mIndexWidth / 8);
		auto const endPrimitive = static_cast<size_t>(startPrimitive) + static_cast<size_t>(numPrimitives);
		if (endPrimitive < static_cast<size_t>(startPrimitive) || (indexStride != 0 && endPrimitive > SIZE_MAX / indexStride))
			THROW_MPP("Index buffer range upload size overflow.", __LINE__, __FILE__, __func__);
		size_t newSize = endPrimitive * indexStride;

		if (newSize > mIndexData.size()) THROW_MPP("Index buffer range upload exceeds its CPU data.", __LINE__, __FILE__, __func__);
		if (newSize > mIndexDataSize)
		{
			allocateIndexData(startPrimitive + numPrimitives);
		}
		else if (numPrimitives)
		{
			geometryStore().upload(mIBO, mIndexData.data() + static_cast<size_t>(startPrimitive) * indexStride,
				static_cast<size_t>(numPrimitives) * indexStride, static_cast<size_t>(startPrimitive) * indexStride);
		}
	}

	/*
	 * Allocate index storage: create or replace the native device-local index
	 * buffer with a staged full upload. Replacement is deferred-safe.
	 *
	 */
	void Mesh::allocateIndexData(size_t numPrimitives)
	{
		switch (mStorageType)
		{
		case mesh::VertexBufferStorageType::Static:
		case mesh::VertexBufferStorageType::Dynamic:
			break; // both realize through staged uploads in this subset

		default:
			throw MppException("Unsupported VertexBufferStorageType value.");
		}
		if (mIndexWidth != 16 && mIndexWidth != 32)
			THROW_MPP("Index width " + to_string(mIndexWidth) + " bits is not supported by the Vulkan geometry-upload subset; use 16 or 32.", __LINE__, __FILE__, __func__);

		auto const indexStride = mesh::Primitive::size(mPrimitiveType) * (mIndexWidth / 8);
		if (indexStride != 0 && numPrimitives > SIZE_MAX / indexStride) THROW_MPP("Index buffer allocation size overflow.", __LINE__, __FILE__, __func__);
		mIndexDataSize = numPrimitives * indexStride;
		if (mIndexDataSize > mIndexData.size()) THROW_MPP("Index buffer allocation exceeds its CPU data.", __LINE__, __FILE__, __func__);

		auto& store = geometryStore();
		if (mIBO)
		{
			store.replace(mIBO, mIndexDataSize ? mIndexData.data() : nullptr, mIndexDataSize);
		}
		else
		{
			mIBO = store.create(VK_BUFFER_USAGE_INDEX_BUFFER_BIT, mIndexDataSize ? mIndexData.data() : nullptr, mIndexDataSize, "Index Buffer: " + getName());
		}
	}

	/*
	 * Upload mesh storage into native buffers.
	 *
	 */
	void Mesh::load()
	{
		unload();
		try
		{
			// Create the index buffer
			if (isIndexed())
			{
				allocateIndexData(mPrimitiveCount);
				mConfiguredIndexBuffer = mIBO;
				geometryStore().bindLabel(mIBO, "Mesh '" + getName() + "' index buffer");
			}

			// Create vertex buffers
			int index = 0;
			for (auto vertexBuffer: mVertexBuffers)
			{
				vertexBuffer->load();
				geometryStore().bindLabel(vertexBuffer->mVBO,
					"Mesh '" + getName() + "' vertex buffer " + to_string(index++));
			}
		}
		catch (...)
		{
			unload(); // a partially uploaded mesh keeps no native storage
			throw;
		}

		// Material resource realization is not delivered in the geometry-upload
		// subset; it arrives with the material/pipeline slice (#95). The
		// reference loads the material here.

		mIsLoaded = true;
	}

	/*
	* Retire native mesh storage; destruction completes in the background once
	* in-flight submissions finish.
	*
	*/
	void Mesh::unload()
	{
		mConfiguredIndexBuffer = 0;
		if (mIBO != 0)
		{
			try { geometryStore().release(mIBO); }
			catch (...) { /* renderer/window already torn down */ }
			mIBO = 0;
		}

		for (auto vertexBuffer: mVertexBuffers)
		{
			vertexBuffer->unload();
		}

		mIsLoaded = false;
	}

	/*
	 * Private plumbing for the GL draw path. The native draw path binds
	 * pipeline state at render time (#95); never invoked in this subset.
	 *
	 */
	void Mesh::bind(bool use) const
	{
		(void)use;
	}

	/*
	 * Streaming index offsets belong to the undelivered persistent-mapped path.
	 *
	 */
	size_t Mesh::getActiveIndexOffset() const
	{
		return 0;
	}

	void Mesh::render(size_t instanceCount) const
	{
		render(instanceCount, 0, mPrimitiveCount);
	}

	/*
	 * Drawing is not part of the geometry-upload subset.
	 *
	 */
	void Mesh::render(size_t instanceCount, uint32_t start, size_t count) const
	{
		(void)instanceCount; (void)start; (void)count;
		throw std::runtime_error("Mesh::render is not delivered in the Vulkan geometry-upload subset; drawing arrives with pipeline realization (#95)");
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
