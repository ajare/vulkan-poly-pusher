// Native Vulkan implementation of the frozen VertexBuffer public API.
// CPU-side behaviour (validation, CPU copies, numeric vocabulary) is retained
// from the reference; GPU realization uses staged uploads into device-local
// storage owned by the window's private geometry store. No GL buffer or
// vertex-attribute state exists; those entry points fail explicitly.
#include <algorithm>
#include <cassert>
#include <stdexcept>

#include "mpp/VertexBuffer.h"
#include "mpp/RenderSystem.h"
#include "mpp/MppException.h"
#include "../PersistentMappedBuffer.h"
#include "WindowBridge.h"
#include "GeometryStore.h"
#include "VertexLayoutTranslation.h"

using namespace std;

namespace mpp
{
	using namespace mpp::mesh;

	namespace
	{
		detail::GeometryStore& geometryStore()
		{
			// Standard startup requires exactly one live window; the store throws
			// an actionable error when no initialized renderer/window exists.
			return detail::associatedWindow()->geometry();
		}
	}

	/*
	 * Constructor.  Pass already-created vertex data directly in.
	 *
	 */
	VertexBuffer::VertexBuffer(RenderSystem* renderSystem, VertexBufferStorageType storageType, size_t vertexCount, size_t vertexStride, bool streaming, bool staticData, shared_ptr<const int8_t> data)
		: mVBO(0)
		, mwRenderSystem(renderSystem)
		, mStorageType(storageType)
		, mVertexCount(vertexCount)
		, mVertexStride(vertexStride)
		, mStreaming(streaming)
		, mStatic(staticData)
	{
		if (!mwRenderSystem)
		{
			THROW_MPP("VertexBuffer requires a RenderSystem.", __LINE__, __FILE__, __func__);
		}
		auto const maxStride = mwRenderSystem->getCaps().maxVertexAttributeStride;
		if (vertexStride != 0 && vertexCount > SIZE_MAX / vertexStride)
		{
			THROW_MPP("Vertex buffer CPU allocation size overflow.", __LINE__, __FILE__, __func__);
		}
		if (maxStride != 0 && vertexStride > maxStride)
		{
			THROW_MPP("Vertex buffer stride " + to_string(vertexStride) + " exceeds the GPU maximum of " + to_string(maxStride) + " bytes.", __LINE__, __FILE__, __func__);
		}
		mData.reserve(vertexCount * vertexStride);
		int8_t const* dataPtr = data.get();
		if (vertexCount * vertexStride != 0 && !dataPtr) THROW_MPP("Vertex buffer has no initial CPU data.", __LINE__, __FILE__, __func__);

		for (size_t i = 0; i < vertexCount * vertexStride; ++i)
		{
			mData.push_back(*dataPtr++);
		}

		mMaxDataSize = vertexCount * vertexStride;
	}

	/*
	 * Destructor.
	 *
	 */
	VertexBuffer::~VertexBuffer()
	{
		unload();
	}

	/*
	 * Set an attribute for a program to use. The retained numeric data-type
	 * vocabulary is translated only at the Vulkan boundary (load()).
	 *
	 */
	void VertexBuffer::setAttribute(int id, Vertex::DataType dataType, size_t componentSize, int offset, bool normalise)
	{
		auto const maxAttributes = mwRenderSystem->getCaps().maxVertexAttributes;
		if (id < 0 || (maxAttributes != 0 && static_cast<uint32_t>(id) >= maxAttributes))
		{
			THROW_MPP("Vertex attribute location " + to_string(id) + " exceeds the GPU-supported range.", __LINE__, __FILE__, __func__);
		}
		if (offset < 0)
		{
			THROW_MPP("Vertex attribute offset cannot be negative.", __LINE__, __FILE__, __func__);
		}

		Attribute attr;

		attr.id = id;

		try
		{
			attr.dataType = detail::legacyVertexDataTypeConstant(dataType);
		}
		catch (std::exception const&)
		{
			THROW_MPP("Unsupported datatype.", __LINE__, __FILE__, __func__);
		}

		attr.componentSize = componentSize;
		attr.sizeInBytes = componentSize * Vertex::getDataTypeSize(dataType);
		attr.offsetInBytes = (size_t)offset;
		attr.normalise = normalise;
		if (attr.offsetInBytes > mVertexStride || attr.sizeInBytes > mVertexStride - attr.offsetInBytes)
		{
			THROW_MPP("Vertex attribute extends beyond the vertex stride.", __LINE__, __FILE__, __func__);
		}

		mAttributes.push_back(attr);
	}

	/*
	 * Get number of vertices in buffer.
	 *
	 */
	size_t VertexBuffer::getNumVertices() const
	{
		return mVertexCount;
	}

	/*
	 * Get vertex stride size.
	 *
	 */
	size_t VertexBuffer::getVertexStride() const
	{
		return mVertexStride;
	}

	/*
	 * Is the data static?
	 *
	 */
	bool VertexBuffer::isStatic() const
	{
		return mStatic;
	}

	bool VertexBuffer::usesPersistentMapping() const
	{
		// Truthful: the persistent-mapped streaming path is not delivered; all
		// uploads are staged. See M01-geometry-upload.md.
		return false;
	}

	/*
	 * Get the number of attributes this buffer has.
	 *
	 */
	size_t VertexBuffer::getNumAttributes() const
	{
		return mAttributes.size();
	}

	/*
	 * GL vertex-attribute state does not exist in the native backend. Attributes
	 * are validated and translated at load() and realized with pipelines (#95).
	 *
	 */
	void VertexBuffer::enableAttribute(uint32_t index, bool enable)
	{
		(void)index; (void)enable;
		throw std::runtime_error("VertexBuffer::enableAttribute is not delivered in the Vulkan geometry-upload subset (no GL vertex state exists; attributes are translated at load and realized with pipelines)");
	}

	/*
	 * Return vertex data for potential modification
	 *
	 */
	vector<int8_t>& VertexBuffer::getBufferData()
	{
		return mData;
	}

	/*
	 * Allocate buffer memory: create or replace the native device-local buffer
	 * with a staged full upload. Replacement defers destruction of the previous
	 * storage until in-flight submissions complete.
	 *
	 */
	void VertexBuffer::allocate(size_t size)
	{
		if (size > mData.size()) THROW_MPP("Vertex buffer upload exceeds its CPU data.", __LINE__, __FILE__, __func__);
		auto& store = geometryStore();
		if (mVBO)
		{
			store.replace(mVBO, size ? mData.data() : nullptr, size);
		}
		else
		{
			mVBO = store.create(VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, size ? mData.data() : nullptr, size, "Vertex Buffer");
		}
		mMaxDataSize = max(mMaxDataSize, size);
	}

	/*
	 * Reupload the buffer data.  This method pulls the whole buffer memory down, and back up, even
	 * if only a part of it has been updated.  Thus it may be inefficient if only a small part of the
	 * buffer has been modified.
	 *
	 */
	void VertexBuffer::mapBufferData(size_t numVertices)
	{
		if (!mVBO) THROW_MPP("Vertex buffer is not loaded; call Mesh::load() before mapping.", __LINE__, __FILE__, __func__);

		// If the data has increased in size, then reallocate
		if (mVertexStride != 0 && numVertices > SIZE_MAX / mVertexStride) THROW_MPP("Vertex buffer upload size overflow.", __LINE__, __FILE__, __func__);
		size_t curSize = numVertices * mVertexStride;
		if (curSize > mData.size()) THROW_MPP("Vertex buffer upload exceeds its CPU data.", __LINE__, __FILE__, __func__);
		// Full maps re-specify storage, as the reference does with glBufferData;
		// replacement retires the previous storage once in-flight work completes.
		allocate(curSize);
	}

	/*
	 * Reupload the buffer data.  This method only updates the specified section of the buffer, so may be more
	 * efficient for only modifying a small part.
	 *
	 */
	void VertexBuffer::mapBufferData(uint32_t startVertex, size_t numVertices)
	{
		if (!mVBO) THROW_MPP("Vertex buffer is not loaded; call Mesh::load() before mapping.", __LINE__, __FILE__, __func__);

		// If (startVertex + numVertices) is greater than max vertices, then reallocate
		if (numVertices > SIZE_MAX - startVertex || (mVertexStride != 0 && (startVertex + numVertices) > SIZE_MAX / mVertexStride))
			THROW_MPP("Vertex buffer range upload size overflow.", __LINE__, __FILE__, __func__);
		size_t curSize = (startVertex + numVertices) * mVertexStride;
		if (curSize > mData.size()) THROW_MPP("Vertex buffer range upload exceeds its CPU data.", __LINE__, __FILE__, __func__);
		if (curSize > mMaxDataSize)
		{
			allocate(curSize);
		}
		else if (numVertices)
		{
			geometryStore().upload(mVBO, mData.data() + startVertex * mVertexStride, numVertices * mVertexStride, startVertex * mVertexStride);
		}
	}

	/*
	 * GL buffer bindings do not exist in the native backend.
	 *
	 */
	void VertexBuffer::bind()
	{
		throw std::runtime_error("VertexBuffer::bind is not delivered in the Vulkan geometry-upload subset (no GL buffer binding exists)");
	}

	void VertexBuffer::prepareForRender()
	{
		// Private plumbing for the GL draw path; the native draw path arrives
		// with pipeline realization (#95). Never invoked in this subset.
	}

	/*
	 * GL buffer bindings do not exist in the native backend.
	 *
	 */
	void VertexBuffer::unbind()
	{
		throw std::runtime_error("VertexBuffer::unbind is not delivered in the Vulkan geometry-upload subset (no GL buffer binding exists)");
	}

	/*
	 * Create the native buffer: validate and translate the declared attribute
	 * layout at the Vulkan boundary, then stage the initial contents.
	 *
	 */
	void VertexBuffer::load()
	{
		unload();

		auto& store = geometryStore();
		for (auto const& attribute : mAttributes)
		{
			VkFormat format{};
			try
			{
				format = detail::translateVertexFormat(attribute.dataType, attribute.componentSize, attribute.normalise);
			}
			catch (std::exception const& error)
			{
				throw std::runtime_error("Vertex attribute location " + to_string(attribute.id) + ": " + error.what());
			}
			store.requireVertexFormat(format, "Vertex attribute location " + to_string(attribute.id));
		}

		allocate(mVertexStride * mVertexCount);
		mConfiguredBuffer = mVBO;
		mConfiguredOffset = 0;
	}

	/*
	 * Retire the native buffer; destruction completes once in-flight
	 * submissions finish. Tolerates a torn-down renderer (native storage was
	 * already freed with the device).
	 *
	 */
	void VertexBuffer::unload()
	{
		mConfiguredBuffer = 0;
		mConfiguredOffset = SIZE_MAX;
		if (mVBO != 0)
		{
			try { geometryStore().release(mVBO); }
			catch (...) { /* renderer/window already torn down: storage died with the device */ }
			mVBO = 0;
		}
	}
}
