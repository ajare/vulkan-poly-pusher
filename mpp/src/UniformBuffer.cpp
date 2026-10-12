#include <algorithm>
#include <iostream>
#include <cassert>

#include <GL/glew.h>

#include "mpp/UniformBuffer.h"
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
	UniformBuffer::UniformBuffer(RenderSystem* renderSystem, shared_ptr<const int8_t> data, size_t dataSize, uint32_t binding)
		: mUBO(0)
		, mwRenderSystem(renderSystem)
		, mDataSize(dataSize)
		, mBinding(binding)
	{
		if (!mwRenderSystem || dataSize == 0 || !data)
			THROW_MPP("UniformBuffer requires a RenderSystem and non-empty initial data.", __LINE__, __FILE__, __func__);
		mData.reserve(dataSize);
		int8_t const* dataPtr = data.get();

		for (size_t i = 0; i < dataSize; ++i)
		{
			mData.push_back(*dataPtr++);
		}
	}

	/*
	 * Destructor.
	 *
	 */
	UniformBuffer::~UniformBuffer()
	{
		unload();
	}

	/*
	 * Return vertex data for potential modification
	 *
	 */
	vector<int8_t>& UniformBuffer::getBufferData()
	{
		return mData;
	}

	bool UniformBuffer::usesPersistentMapping() const
	{
		return mStreamBuffer && mStreamBuffer->isPersistent();
	}

	/*
	 * Allocate buffer memory.
	 *
	 */
	void UniformBuffer::allocate()
	{
		GLint alignment = 1;
		GL_CHECK(glGetIntegerv(GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT, &alignment));
		mStreamBuffer = make_unique<detail::PersistentMappedBuffer>();
		mStreamBuffer->create(GL_UNIFORM_BUFFER, mDataSize, max(1, alignment), mwRenderSystem->getCaps().streamingGeometry,
			mData.data(), mDataSize, "Uniform Buffer");
		mUBO = mStreamBuffer->getBuffer();
	}

	void UniformBuffer::updateData(uint32_t offset, size_t size)
	{
		if (offset > mDataSize || size > mDataSize - offset) THROW_MPP("Uniform buffer update range is out of bounds.", __LINE__, __FILE__, __func__);
		if (!mStreamBuffer) THROW_MPP("Cannot update an unloaded uniform buffer.", __LINE__, __FILE__, __func__);
		mStreamBuffer->upload(mData.data(), mDataSize, offset, size);
		mUBO = mStreamBuffer->getBuffer();
		activate();
	}

	/*
	 * Reupload the buffer data.  This method pulls the whole buffer memory down, and back up, even
	 * if only a part of it has been updated.  Thus it may be inefficient if only a small part of the
	 * buffer has been modified.
	 *
	 */
	void UniformBuffer::mapBufferData()
	{
		bind();

		if (!mStreamBuffer) THROW_MPP("Cannot update an unloaded uniform buffer.", __LINE__, __FILE__, __func__);
		mStreamBuffer->upload(mData.data(), mDataSize, 0, mDataSize);
		mUBO = mStreamBuffer->getBuffer();
		activate();
	}

	/*
	 * Bind buffer
	 *
	 */
	void UniformBuffer::bind()
	{
		GL_CHECK(glBindBuffer(GL_UNIFORM_BUFFER, mUBO));
	}

	/*
	 * Unbind buffer
	 *
	 */
	void UniformBuffer::activate()
	{
		if (mStreamBuffer)
		{
			GL_CHECK(glBindBufferRange(GL_UNIFORM_BUFFER, mBinding, mStreamBuffer->getBuffer(), static_cast<GLintptr>(mStreamBuffer->getActiveOffset()), static_cast<GLsizeiptr>(mDataSize)));
			mStreamBuffer->markUsed();
		}
		else
		{
			GL_CHECK(glBindBufferBase(GL_UNIFORM_BUFFER, mBinding, mUBO));
		}
	}

	void UniformBuffer::unbind()
	{
		GL_CHECK(glBindBuffer(GL_UNIFORM_BUFFER, 0));
	}

	/*
	 * Create GL buffer.
	 *
	 */
	void UniformBuffer::load()
	{
		unload();
		allocate();
		bind();
		activate();
	}

	/*
	 * Destroy GL buffer
	 *
	 */
	void UniformBuffer::unload()
	{
		if (mStreamBuffer)
		{
			mStreamBuffer.reset();
			mUBO = 0;
		}
		else if (mUBO != 0)
		{
			GL_CHECK(glDeleteBuffers(1, &mUBO));
			mUBO = 0;
		}
	}
}