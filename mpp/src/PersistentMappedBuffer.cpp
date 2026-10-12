#include <GL/glew.h>

#include <algorithm>
#include <cstring>
#include <limits>

#include "PersistentMappedBuffer.h"

#include "mpp/GLErrorCheck.h"
#include "mpp/MppException.h"

using namespace std;

namespace mpp::detail
{
	size_t PersistentMappedBuffer::alignedSize(size_t value, size_t alignment)
	{
		alignment = max<size_t>(1, alignment);
		if (value > numeric_limits<size_t>::max() - (alignment - 1))
			THROW_MPP("Dynamic buffer size overflow.", __LINE__, __FILE__, __func__);
		return ((value + alignment - 1) / alignment) * alignment;
	}

	PersistentMappedBuffer::~PersistentMappedBuffer()
	{
		destroy();
	}

	void PersistentMappedBuffer::waitForSegment(uint32_t segment)
	{
		auto fence = static_cast<GLsync>(mFences[segment]);
		if (!fence) return;
		auto status = glClientWaitSync(fence, 0, 0);
		if (status == GL_TIMEOUT_EXPIRED)
			status = glClientWaitSync(fence, GL_SYNC_FLUSH_COMMANDS_BIT, GL_TIMEOUT_IGNORED);
		if (status == GL_WAIT_FAILED)
			THROW_MPP("Failed waiting for a persistent buffer segment.", __LINE__, __FILE__, __func__);
		GL_CHECK(glDeleteSync(fence));
		mFences[segment] = nullptr;
	}

	void PersistentMappedBuffer::releaseStorage() noexcept
	{
		for (auto& value : mFences)
		{
			if (value) glDeleteSync(static_cast<GLsync>(value));
			value = nullptr;
		}
		if (mMapping && mBuffer)
		{
			glBindBuffer(static_cast<GLenum>(mTarget), mBuffer);
			glUnmapBuffer(static_cast<GLenum>(mTarget));
		}
		mMapping = nullptr;
		if (mBuffer) glDeleteBuffers(1, &mBuffer);
		mBuffer = 0;
		mCapacity = 0;
		mSegmentStride = 0;
		mActiveSegment = 0;
		mActiveUsed = false;
	}

	void PersistentMappedBuffer::swap(PersistentMappedBuffer& other) noexcept
	{
		using std::swap;
		swap(mTarget, other.mTarget);
		swap(mBuffer, other.mBuffer);
		swap(mPersistent, other.mPersistent);
		swap(mMapping, other.mMapping);
		swap(mCapacity, other.mCapacity);
		swap(mSegmentStride, other.mSegmentStride);
		swap(mAlignment, other.mAlignment);
		swap(mActiveSegment, other.mActiveSegment);
		swap(mActiveUsed, other.mActiveUsed);
		swap(mFences, other.mFences);
		swap(mLabel, other.mLabel);
	}

	void PersistentMappedBuffer::createStorage(size_t capacity, void const* data, size_t size)
	{
		capacity = max<size_t>(1, capacity);
		mCapacity = capacity;
		mSegmentStride = alignedSize(capacity, mAlignment);
		if (mPersistent && mSegmentStride > numeric_limits<size_t>::max() / SegmentCount)
			THROW_MPP("Persistent buffer allocation size overflow.", __LINE__, __FILE__, __func__);
		size_t const allocationSize = mPersistent ? mSegmentStride * SegmentCount : capacity;

		GLuint candidate = 0;
		GL_CHECK(glGenBuffers(1, &candidate));
		try
		{
			GL_CHECK(glBindBuffer(static_cast<GLenum>(mTarget), candidate));
			if (mPersistent)
			{
				GLbitfield const flags = GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT;
				GL_CHECK(glBufferStorage(static_cast<GLenum>(mTarget), static_cast<GLsizeiptr>(allocationSize), nullptr, flags));
				void* mapping = nullptr;
				GL_CHECK(mapping = glMapBufferRange(static_cast<GLenum>(mTarget), 0, static_cast<GLsizeiptr>(allocationSize), flags));
				if (!mapping) THROW_MPP("Persistent buffer mapping returned null.", __LINE__, __FILE__, __func__);
				mMapping = static_cast<byte*>(mapping);
				if (size) memcpy(mMapping, data, size);
			}
			else
			{
				GL_CHECK(glBufferData(static_cast<GLenum>(mTarget), static_cast<GLsizeiptr>(allocationSize), nullptr, GL_DYNAMIC_DRAW));
				if (size) GL_CHECK(glBufferSubData(static_cast<GLenum>(mTarget), 0, static_cast<GLsizeiptr>(size), data));
			}
			GL_CHECK(glObjectLabel(GL_BUFFER, candidate, -1, mLabel.c_str()));
		}
		catch (...)
		{
			if (mMapping)
			{
				glBindBuffer(static_cast<GLenum>(mTarget), candidate);
				glUnmapBuffer(static_cast<GLenum>(mTarget));
				mMapping = nullptr;
			}
			glDeleteBuffers(1, &candidate);
			mCapacity = 0;
			mSegmentStride = 0;
			throw;
		}
		mBuffer = candidate;
	}

	void PersistentMappedBuffer::ensureCapacity(size_t required, void const* data, size_t size)
	{
		if (required <= mCapacity) return;
		size_t grown = max(required, mCapacity <= numeric_limits<size_t>::max() / 2 ? mCapacity * 2 : required);
		PersistentMappedBuffer replacement;
		replacement.create(mTarget, grown, mAlignment, mPersistent, data, size, mLabel);
		swap(replacement);
	}

	void PersistentMappedBuffer::create(uint32_t target, size_t capacity, size_t alignment, bool persistent,
		void const* data, size_t size, string label)
	{
		if (size > capacity || (size != 0 && !data))
			THROW_MPP("Invalid initial dynamic buffer data.", __LINE__, __FILE__, __func__);
		destroy();
		mTarget = target;
		mAlignment = max<size_t>(1, alignment);
		mPersistent = persistent;
		mLabel = move(label);
		createStorage(max(capacity, size), data, size);
	}

	void PersistentMappedBuffer::destroy() noexcept
	{
		releaseStorage();
		mTarget = 0;
		mPersistent = false;
		mAlignment = 1;
		mLabel.clear();
	}

	void PersistentMappedBuffer::bind() const
	{
		GL_CHECK(glBindBuffer(static_cast<GLenum>(mTarget), mBuffer));
	}

	void PersistentMappedBuffer::upload(void const* completeData, size_t completeSize, size_t changedOffset, size_t changedSize)
	{
		if (completeSize != 0 && !completeData)
			THROW_MPP("Dynamic buffer upload has no CPU data.", __LINE__, __FILE__, __func__);
		if (changedOffset > completeSize || changedSize > completeSize - changedOffset)
			THROW_MPP("Dynamic buffer update range is outside the CPU data.", __LINE__, __FILE__, __func__);
		ensureCapacity(completeSize, completeData, completeSize);
		bind();
		if (!mPersistent)
		{
			if (changedSize)
			{
				auto bytes = static_cast<byte const*>(completeData);
				GL_CHECK(glBufferSubData(static_cast<GLenum>(mTarget), static_cast<GLintptr>(changedOffset), static_cast<GLsizeiptr>(changedSize), bytes + changedOffset));
			}
			return;
		}

		if (mActiveUsed)
		{
			if (mFences[mActiveSegment]) THROW_MPP("Active persistent buffer segment still has a pending fence.", __LINE__, __FILE__, __func__);
			GL_CHECK(mFences[mActiveSegment] = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0));
			if (!mFences[mActiveSegment]) THROW_MPP("Failed creating a persistent buffer fence.", __LINE__, __FILE__, __func__);
			uint32_t const next = (mActiveSegment + 1) % SegmentCount;
			waitForSegment(next);
			mActiveSegment = next;
			mActiveUsed = false;
			// A rotated segment may contain an arbitrarily old version, so publish the
			// complete CPU copy rather than only the changed subrange.
			if (completeSize) memcpy(mMapping + getActiveOffset(), completeData, completeSize);
		}
		else if (changedSize)
		{
			auto bytes = static_cast<byte const*>(completeData);
			memcpy(mMapping + getActiveOffset() + changedOffset, bytes + changedOffset, changedSize);
		}
	}

	void PersistentMappedBuffer::markUsed()
	{
		if (mPersistent) mActiveUsed = true;
	}
}
