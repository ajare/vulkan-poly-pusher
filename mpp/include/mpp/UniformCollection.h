#pragma once

#include <cstring>
#include <map>

#pragma warning(push)
#pragma warning(disable : 4201)
#include <glm/gtc/matrix_transform.hpp>
#pragma warning(pop)

#include "mpp/program/glslTypes.h"

#include "mpp/Config.h"
#include "mpp/Program.h"

namespace mpp
{
	class _MPPAPI UniformCollection
	{
	public:

		struct UniformData
		{
			std::string name;
			program::GLSLType type;
			size_t count; // Number of values
			size_t size; // Size of data in bytes
			size_t numElements; // Elements, eg float/vec2/vec3 etc
			char* data{ nullptr };


		private:
			
			void copyFrom(UniformData const& other)
			{
				this->name = other.name;
				this->type = other.type;
				this->count = other.count;
				this->size = other.size;
				this->numElements = other.numElements;
				
				delete[] this->data;
				this->data = new char[this->size];
				memcpy(this->data, other.data, this->size);
			}
			
		public:

			UniformData()
				: type(program::GLSLType::Unknown)
				, count(0)
				, size(0)
				, numElements(0)
				, data(nullptr)
			{
			}

			UniformData(std::string const& name_, program::GLSLType type_, size_t count_, size_t size_, size_t numElements_)
				: name(name_)
				, type(type_)
				, count(count_)
				, size(size_)
				, numElements(numElements_)
				, data(nullptr)
			{
				data = new char[size];
			}
			
			UniformData(UniformData const& other)
			{
				copyFrom(other);
			}

			UniformData& operator=(UniformData const& other)
			{
				copyFrom(other);
				return *this;
			}

			~UniformData()
			{
				delete[] data;
			}
		};

		std::map<std::string, UniformData> mUniformData;

	public:

		size_t getNumUniforms() const;

		std::map<std::string, UniformData> const& getUniformData() const;

		void setUniform(std::string const& name, int32_t value);

		void setUniform(std::string const& name, uint32_t value);

		void setUniform(std::string const& name, float value);

		void setUniform(std::string const& name, glm::vec2 const& value);

		void setUniform(std::string const& name, glm::vec3 const& value);

		void setUniform(std::string const& name, glm::vec4 const& value);

		void setUniform(std::string const& name, glm::mat4 const& value);

		void setUniform(std::string const& name, size_t count, size_t numElements, int32_t const* values);

		void setUniform(std::string const& name, size_t count, size_t numElements, uint32_t const* values);

		void setUniform(std::string const& name, size_t count, size_t numElements, float const* values);

		void setUniform(std::string const& name, size_t count, glm::vec2 const* values);

		void setUniform(std::string const& name, size_t count, glm::vec3 const* values);

		void setUniform(std::string const& name, size_t count, glm::vec4 const* values);

		void setUniform(std::string const& name, program::GLSLType type, size_t count, size_t numElements, char const* data);

		void updateUniform(std::string const& name, int32_t value);

		void updateUniform(std::string const& name, uint32_t value);

		void updateUniform(std::string const& name, float value);

		void updateUniform(std::string const& name, glm::vec2 const& value);

		void updateUniform(std::string const& name, glm::vec3 const& value);

		void updateUniform(std::string const& name, glm::vec4 const& value);

		void updateUniform(std::string const& name, glm::mat4 const& value);

		void updateUniform(std::string const& name, int32_t const* values);

		void updateUniform(std::string const& name, uint32_t const* values);

		void updateUniform(std::string const& name, float const* values);

		void updateUniform(std::string const& name, glm::vec2 const* values);

		void updateUniform(std::string const& name, glm::vec3 const* values);

		void updateUniform(std::string const& name, glm::vec4 const* values);

		void bindUniforms(ResourcePtr program);
	};
}

