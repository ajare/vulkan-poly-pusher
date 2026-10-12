#include <vector>

#include <GL/glew.h>
#include <GL/gl.h>

#include "mpp/GLErrorCheck.h"
#include "mpp/MppException.h"
#include "mpp/ShaderStorageBuffer.h"

using namespace std;

namespace mpp
{
	ShaderStorageBuffer::~ShaderStorageBuffer()
	{
		destroy();
	}

	void ShaderStorageBuffer::create(size_t size, void const* initialData, string const& label)
	{
		destroy();

		if (size == 0)
		{
			THROW_MPP("Shader storage buffer '" + label + "' requires a non-zero size.", __LINE__, __FILE__, __func__);
		}

		GL_CHECK(glGenBuffers(1, &mBuffer));
		if (mBuffer == 0)
		{
			THROW_MPP("Could not create shader storage buffer '" + label + "'.", __LINE__, __FILE__, __func__);
		}

		// Zeroed rather than left undefined: an indirect command buffer read
		// before its first dispatch has to describe an empty draw, not garbage.
		vector<uint8_t> zeroes;
		if (!initialData) zeroes.resize(size, 0);

		GL_CHECK(glBindBuffer(GL_SHADER_STORAGE_BUFFER, mBuffer));
		GL_CHECK(glBufferData(GL_SHADER_STORAGE_BUFFER, (GLsizeiptr)size, initialData ? initialData : zeroes.data(), GL_DYNAMIC_COPY));
		GL_CHECK(glObjectLabel(GL_BUFFER, mBuffer, -1, label.c_str()));
		GL_CHECK(glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0));

		mSize = size;
	}

	void ShaderStorageBuffer::destroy() noexcept
	{
		if (mBuffer != 0)
		{
			glDeleteBuffers(1, &mBuffer);
			mBuffer = 0;
		}
		mSize = 0;
	}

	void ShaderStorageBuffer::bindStorage(uint32_t bindingIndex) const
	{
		GL_CHECK(glBindBufferBase(GL_SHADER_STORAGE_BUFFER, bindingIndex, mBuffer));
	}

	void ShaderStorageBuffer::bindDrawIndirect() const
	{
		GL_CHECK(glBindBuffer(GL_DRAW_INDIRECT_BUFFER, mBuffer));
	}

	void ShaderStorageBuffer::bindDispatchIndirect() const
	{
		GL_CHECK(glBindBuffer(GL_DISPATCH_INDIRECT_BUFFER, mBuffer));
	}
}
