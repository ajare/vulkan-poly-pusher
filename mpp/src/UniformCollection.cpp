#if defined(_WIN32)
#include <Windows.h>
#endif

#include <functional>




#pragma warning(push)
#pragma warning(disable : 4201)
#include <glm/gtc/type_ptr.hpp>
#pragma warning(pop)

#include "mpp/UniformCollection.h"

#include "mpp/MppException.h"

using namespace std;

namespace mpp
{

	size_t UniformCollection::getNumUniforms() const
	{
		return mUniformData.size();
	}

	map<std::string, UniformCollection::UniformData> const& UniformCollection::getUniformData() const
	{
		return mUniformData;
	}

	void UniformCollection::setUniform(string const& name, int32_t value)
	{
		UniformData ud(
			MPP_PROGRAM_MARKUP_UNIFORM(name),
			program::GLSLType::Int,
			1,
			sizeof(int32_t),
			1
		);

		memcpy(ud.data, &value, ud.size);
		mUniformData.emplace(name, move(ud));
	}

	void UniformCollection::setUniform(string const& name, uint32_t value)
	{
		UniformData ud(
			MPP_PROGRAM_MARKUP_UNIFORM(name),
			program::GLSLType::Uint,
			1,
			sizeof(uint32_t),
			1
		);

		memcpy(ud.data, &value, ud.size);
		mUniformData.emplace(name, move(ud));
	}

	/*
	 * Set a uniform value for this mesh instance.
	 *
	 */
	void UniformCollection::setUniform(string const& name, float value)
	{
		UniformData ud(
			MPP_PROGRAM_MARKUP_UNIFORM(name),
			program::GLSLType::Float,
			1,
			sizeof(float),
			1
		);

		memcpy(ud.data, &value, ud.size);
		mUniformData.emplace(name, move(ud));
	}

	/*
	 * Set a uniform value for this mesh instance.
	 *
	 */
	void UniformCollection::setUniform(string const& name, glm::vec2 const& value)
	{
		UniformData ud(
			MPP_PROGRAM_MARKUP_UNIFORM(name),
			program::GLSLType::Float,
			1,
			sizeof(glm::vec2),
			2
		);

		memcpy(ud.data, glm::value_ptr(value), ud.size);
		mUniformData.emplace(name, move(ud));
	}

	/*
	 * Set a uniform value for this mesh instance.
	 *
	 */
	void UniformCollection::setUniform(string const& name, glm::vec3 const& value)
	{
		UniformData ud(
			MPP_PROGRAM_MARKUP_UNIFORM(name),
			program::GLSLType::Float,
			1,
			sizeof(glm::vec3),
			3
		);

		memcpy(ud.data, glm::value_ptr(value), ud.size);
		mUniformData.emplace(name, move(ud));
	}

	/*
	 * Set a uniform value for this mesh instance.
	 *
	 */
	void UniformCollection::setUniform(string const& name, glm::vec4 const& value)
	{
		UniformData ud(
			MPP_PROGRAM_MARKUP_UNIFORM(name),
			program::GLSLType::Float,
			1,
			sizeof(glm::vec4),
			4
		);

		memcpy(ud.data, glm::value_ptr(value), ud.size);
		mUniformData.emplace(name, move(ud));
	}

	void UniformCollection::setUniform(string const& name, glm::mat4 const& value)
	{
		UniformData ud(
			MPP_PROGRAM_MARKUP_UNIFORM(name),
			program::GLSLType::FloatMatrix,
			1,
			sizeof(glm::mat4),
			4
		);

		memcpy(ud.data, glm::value_ptr(value), ud.size);
		mUniformData.emplace(name, move(ud));
	}

	void UniformCollection::setUniform(string const& name, size_t count, size_t numElements, int32_t const* values)
	{
		UniformData ud(
			MPP_PROGRAM_MARKUP_UNIFORM(name),
			program::GLSLType::Int,
			count,
			sizeof(int32_t) * count * numElements,
			numElements
		);

		memcpy(ud.data, values, ud.size);
		mUniformData.emplace(name, move(ud));
	}

	void UniformCollection::setUniform(string const& name, size_t count, size_t numElements, uint32_t const* values)
	{
		UniformData ud(
			MPP_PROGRAM_MARKUP_UNIFORM(name),
			program::GLSLType::Uint,
			count,
			sizeof(uint32_t) * count * numElements,
			numElements
		);

		memcpy(ud.data, values, ud.size);
		mUniformData.emplace(name, move(ud));
	}

	void UniformCollection::setUniform(string const& name, size_t count, size_t numElements, float const* values)
	{
		UniformData ud(
			MPP_PROGRAM_MARKUP_UNIFORM(name),
			program::GLSLType::Float,
			count,
			sizeof(float) * count * numElements,
			numElements
		);

		memcpy(ud.data, values, ud.size);
		mUniformData.emplace(name, move(ud));
	}

	void UniformCollection::setUniform(string const& name, size_t count, glm::vec2 const* values)
	{
		UniformData ud(
			MPP_PROGRAM_MARKUP_UNIFORM(name),
			program::GLSLType::Float,
			count,
			sizeof(glm::vec2) * count,
			2
		);

		memcpy(ud.data, values, ud.size);
		mUniformData.emplace(name, move(ud));
	}

	void UniformCollection::setUniform(string const& name, size_t count, glm::vec3 const* values)
	{
		UniformData ud(
			MPP_PROGRAM_MARKUP_UNIFORM(name),
			program::GLSLType::Float,
			count,
			sizeof(glm::vec3) * count,
			3
		);

		memcpy(ud.data, values, ud.size);
		mUniformData.emplace(name, move(ud));
	}

	void UniformCollection::setUniform(string const& name, size_t count, glm::vec4 const* values)
	{
		UniformData ud(
			MPP_PROGRAM_MARKUP_UNIFORM(name),
			program::GLSLType::Float,
			count,
			sizeof(glm::vec4) * count,
			4
		);

		memcpy(ud.data, values, ud.size);
		mUniformData.emplace(name, move(ud));
	}

	void UniformCollection::setUniform(string const& name, program::GLSLType type, size_t count, size_t numElements, char const* data)
	{
		size_t typeSize;
		switch (type)
		{
		case program::GLSLType::Bool:
			typeSize = 4; break;
		case program::GLSLType::Int:
			typeSize = 4; break;
		case program::GLSLType::Uint:
			typeSize = 4; break;
		case program::GLSLType::Float:
			typeSize = 4; break;
		case program::GLSLType::Double:
			typeSize = 8; break;
		case program::GLSLType::FloatMatrix:
		case program::GLSLType::DoubleMatrix:
		case program::GLSLType::Unknown:
		case program::GLSLType::User:
		default:
			throw MppException("Cannot determine size of this GL type.");
		}

		UniformData ud(
			MPP_PROGRAM_MARKUP_UNIFORM(name),
			type,
			count,
			typeSize * count * numElements,
			numElements
		);

		memcpy(ud.data, data, ud.size);
		mUniformData.emplace(name, move(ud));
	}

	void UniformCollection::updateUniform(string const& name, int32_t value)
	{
		auto& ud = mUniformData.find(name)->second;
		memcpy(ud.data, &value, ud.size);
	}

	void UniformCollection::updateUniform(string const& name, uint32_t value)
	{
		auto& ud = mUniformData.find(name)->second;
		memcpy(ud.data, &value, ud.size);
	}

	/*
	 * Set a uniform value for this mesh instance.
	 *
	 */
	void UniformCollection::updateUniform(string const& name, float value)
	{
		auto& ud = mUniformData.find(name)->second;
		memcpy(ud.data, &value, ud.size);
	}

	/*
	 * Set a uniform value for this mesh instance.
	 *
	 */
	void UniformCollection::updateUniform(string const& name, glm::vec2 const& value)
	{
		auto& ud = mUniformData.find(name)->second;
		memcpy(ud.data, &value, ud.size);
	}

	/*
	 * Set a uniform value for this mesh instance.
	 *
	 */
	void UniformCollection::updateUniform(string const& name, glm::vec3 const& value)
	{
		auto& ud = mUniformData.find(name)->second;
		memcpy(ud.data, &value, ud.size);
	}

	/*
	 * Set a uniform value for this mesh instance.
	 *
	 */
	void UniformCollection::updateUniform(string const& name, glm::vec4 const& value)
	{
		auto& ud = mUniformData.find(name)->second;
		memcpy(ud.data, &value, ud.size);
	}

	void UniformCollection::updateUniform(string const& name, glm::mat4 const& value)
	{
		auto& ud = mUniformData.find(name)->second;
		memcpy(ud.data, glm::value_ptr(value), ud.size);
	}

	void UniformCollection::updateUniform(string const& name, int32_t const* values)
	{
		auto& ud = mUniformData.find(name)->second;
		memcpy(ud.data, values, ud.size);
	}

	void UniformCollection::updateUniform(string const& name, uint32_t const* values)
	{
		auto& ud = mUniformData.find(name)->second;
		memcpy(ud.data, values, ud.size);
	}

	void UniformCollection::updateUniform(string const& name, float const* values)
	{
		auto& ud = mUniformData.find(name)->second;
		memcpy(ud.data, values, ud.size);
	}

	void UniformCollection::updateUniform(string const& name, glm::vec2 const* values)
	{
		auto& ud = mUniformData.find(name)->second;
		memcpy(ud.data, values, ud.size);
	}

	void UniformCollection::updateUniform(string const& name, glm::vec3 const* values)
	{
		auto& ud = mUniformData.find(name)->second;
		memcpy(ud.data, values, ud.size);
	}

	void UniformCollection::updateUniform(string const& name, glm::vec4 const* values)
	{
		auto& ud = mUniformData.find(name)->second;
		memcpy(ud.data, values, sizeof(glm::vec4) * ud.size);
	}

	// bindUniforms is implemented per backend: natively through descriptor-backed
	// per-frame-context uniform buffers in the Vulkan renderer build
	// (vulkan/UniformBinding.cpp), and as an explicit failure in the CPU-only
	// build (compat/CpuResourceBoundary.cpp).

}
