#include <iostream>
#include <cassert>

#include <GL/glew.h>

#include "mpp/VertexBuffer.h"
#include "mpp/RenderSystem.h"
#include "mpp/MppException.h"
#include "mpp/GLErrorCheck.h"
#include "PersistentMappedBuffer.h"

using namespace std;

namespace mpp
{
	using namespace mpp::mesh;

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
	 * Set an attribute for a program to use.
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

		switch (dataType)
		{
		case Vertex::DataType::Byte:							attr.dataType = GL_BYTE; break;
		case Vertex::DataType::UnsignedByte:					attr.dataType = GL_UNSIGNED_BYTE; break;
		case Vertex::DataType::Short:							attr.dataType = GL_SHORT; break;
		case Vertex::DataType::UnsignedShort:					attr.dataType = GL_UNSIGNED_SHORT; break;
		case Vertex::DataType::Int:								attr.dataType = GL_INT; break;
		case Vertex::DataType::UnsignedInt:						attr.dataType = GL_UNSIGNED_INT; break;
		case Vertex::DataType::HalfFloat:						attr.dataType = GL_HALF_FLOAT; break;
		case Vertex::DataType::Float:							attr.dataType = GL_FLOAT; break;
		case Vertex::DataType::Double:							attr.dataType = GL_DOUBLE; break;
		case Vertex::DataType::Int_2_10_10_10_REV:				attr.dataType = GL_INT_2_10_10_10_REV; break;
		case Vertex::DataType::UnsignedInt_2_10_10_10_REV:		attr.dataType = GL_UNSIGNED_INT_2_10_10_10_REV; break;
		default:								
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
		return mStreamBuffer && mStreamBuffer->isPersistent();
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
	 * Enable specified attribute.
	 *
	 */
	void VertexBuffer::enableAttribute(uint32_t index, bool enable)
	{
		assert(index < getNumAttributes() && "VertexBuffer::enableAttribute() 'index' argument out of range!");

		Attribute const& attrib = mAttributes[index];
		size_t const baseOffset = mStreamBuffer ? mStreamBuffer->getActiveOffset() : 0;

		if (enable)
		{
			GL_CHECK(glEnableVertexAttribArray(attrib.id));

			switch (attrib.dataType)
			{
			case GL_BYTE:
			case GL_UNSIGNED_BYTE:
			case GL_SHORT:
			case GL_UNSIGNED_SHORT:
			case GL_INT:
			case GL_UNSIGNED_INT:
			case GL_INT_2_10_10_10_REV:
			case GL_UNSIGNED_INT_2_10_10_10_REV:
				if (attrib.normalise)
				{
					GL_CHECK(glVertexAttribPointer(attrib.id, (GLint)attrib.componentSize, attrib.dataType, attrib.normalise ? GL_TRUE : GL_FALSE, (GLsizei)mVertexStride, (const GLvoid*)(baseOffset + attrib.offsetInBytes)));
				}
				else
				{
					GL_CHECK(glVertexAttribIPointer(attrib.id, (GLint)attrib.componentSize, attrib.dataType, (GLsizei)mVertexStride, (const GLvoid*)(baseOffset + attrib.offsetInBytes)));
				}
				break;

			case GL_FLOAT:
			case GL_HALF_FLOAT:
				GL_CHECK(glVertexAttribPointer(attrib.id, (GLint)attrib.componentSize, attrib.dataType, attrib.normalise ? GL_TRUE : GL_FALSE, (GLsizei)mVertexStride, (const GLvoid*)(baseOffset + attrib.offsetInBytes)));
				break;

			case GL_DOUBLE:
				GL_CHECK(glVertexAttribLPointer(attrib.id, (GLint)attrib.componentSize, attrib.dataType, (GLsizei)mVertexStride, (const GLvoid*)(baseOffset + attrib.offsetInBytes)));
				break;

			default:
				THROW_MPP("Unsupported GL data in databuffer.", __LINE__, __FILE__, __func__);
			}
		}
		else
		{
			GL_CHECK(glDisableVertexAttribArray(attrib.id));
		}
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
	 * Allocate buffer memory.
	 *
	 */
	void VertexBuffer::allocate(size_t size)
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

		if (mStreaming)
		{
			if (size > mData.size()) THROW_MPP("Streaming vertex buffer allocation exceeds its CPU data.", __LINE__, __FILE__, __func__);
			if (!mStreamBuffer)
			{
				mStreamBuffer = make_unique<detail::PersistentMappedBuffer>();
				mStreamBuffer->create(GL_ARRAY_BUFFER, max<size_t>(1, size), max<size_t>(1, mVertexStride), mwRenderSystem->getCaps().streamingGeometry,
					size ? mData.data() : nullptr, size, "Streaming Vertex Buffer");
			}
			mVBO = mStreamBuffer->getBuffer();
		}
		else
		{
			if (size == 0)
			{
				GL_CHECK(glBufferData(GL_ARRAY_BUFFER, size, nullptr, glStorageType));
			}
			else
			{
				GL_CHECK(glBufferData(GL_ARRAY_BUFFER, size, &(mData[0]), glStorageType));
			}
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
		bind();

		// If the data has increased in size, then reallocate
		if (mVertexStride != 0 && numVertices > SIZE_MAX / mVertexStride) THROW_MPP("Vertex buffer upload size overflow.", __LINE__, __FILE__, __func__);
		size_t curSize = numVertices * mVertexStride;
		if (curSize > mData.size()) THROW_MPP("Vertex buffer upload exceeds its CPU data.", __LINE__, __FILE__, __func__);
		if (mStreamBuffer)
		{
			mStreamBuffer->upload(curSize ? mData.data() : nullptr, curSize, 0, curSize);
			mVBO = mStreamBuffer->getBuffer();
			mMaxDataSize = max(mMaxDataSize, curSize);
		}
		else
		{
			allocate(curSize);
		}
	}

	/*
	 * Reupload the buffer data.  This method only updates the specified section of the buffer, so may be more
	 * efficient for only modifying a small part.
	 *
	 */
	void VertexBuffer::mapBufferData(uint32_t startVertex, size_t numVertices)
	{
		bind();

		// If (startVertex + numVertices) is greater than max vertices, then reallocate
		if (numVertices > SIZE_MAX - startVertex || (mVertexStride != 0 && (startVertex + numVertices) > SIZE_MAX / mVertexStride))
			THROW_MPP("Vertex buffer range upload size overflow.", __LINE__, __FILE__, __func__);
		size_t curSize = (startVertex + numVertices) * mVertexStride;
		if (curSize > mData.size()) THROW_MPP("Vertex buffer range upload exceeds its CPU data.", __LINE__, __FILE__, __func__);
		if (mStreamBuffer)
		{
			size_t const completeSize = mData.size();
			mStreamBuffer->upload(completeSize ? mData.data() : nullptr, completeSize, startVertex * mVertexStride, numVertices * mVertexStride);
			mVBO = mStreamBuffer->getBuffer();
			mMaxDataSize = max(mMaxDataSize, completeSize);
		}
		else if (curSize > mMaxDataSize)
		{
			allocate(curSize);
		}
		else
		{
			GL_CHECK(glBufferSubData(GL_ARRAY_BUFFER, startVertex * mVertexStride, numVertices * mVertexStride, &(mData[startVertex * mVertexStride])));
		}
	}

	/*
	 * Bind buffer
	 *
	 */
	void VertexBuffer::bind()
	{
		GL_CHECK(glBindBuffer(GL_ARRAY_BUFFER, mVBO));
	}

	void VertexBuffer::prepareForRender()
	{
		if (!mStreamBuffer) return;
		mVBO = mStreamBuffer->getBuffer();
		auto const activeOffset = mStreamBuffer->getActiveOffset();
		if (mConfiguredBuffer != mVBO || mConfiguredOffset != activeOffset)
		{
			bind();
			for (size_t index = 0; index < getNumAttributes(); ++index) enableAttribute(static_cast<uint32_t>(index), true);
			mConfiguredBuffer = mVBO;
			mConfiguredOffset = activeOffset;
		}
		mStreamBuffer->markUsed();
	}

	/*
	 * Unbind buffer
	 *
	 */
	void VertexBuffer::unbind()
	{
		GL_CHECK(glBindBuffer(GL_ARRAY_BUFFER, 0));
	}

	/*
	 * Create GL buffer.
	 *
	 */
	void VertexBuffer::load()
	{
		unload();
		if (!mStreaming) GL_CHECK(glGenBuffers(1, &mVBO));
	
		if (!mStreaming) bind();

		// Set name for debugging. Streaming storage owns and labels its GL name.
		if (!mStreaming)
		{
			string label = "Vertex Buffer";
			GL_CHECK(glObjectLabel(GL_BUFFER, mVBO, -1, label.c_str()));
		}

		allocate(mVertexStride * mVertexCount);
			
		for (size_t i = 0; i < getNumAttributes(); ++i)
		{
			enableAttribute((uint32_t)i, true);
		}
		mConfiguredBuffer = mVBO;
		mConfiguredOffset = mStreamBuffer ? mStreamBuffer->getActiveOffset() : 0;
	}

	/*
	 * Destroy GL buffer
	 *
	 */
	void VertexBuffer::unload()
	{
		mConfiguredBuffer = 0;
		mConfiguredOffset = SIZE_MAX;
		if (mStreamBuffer)
		{
			mStreamBuffer.reset();
			mVBO = 0;
		}
		else if (mVBO != 0)
		{
			GL_CHECK(glDeleteBuffers(1, &mVBO));
			mVBO = 0;
		}
	}
}