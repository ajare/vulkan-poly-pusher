#pragma once

#include <string>
#include <map>
#include <vector>
#include <any>

#include "mpp/mesh/Config.h"

namespace mpp
{
	namespace mesh
	{
		class _MPPMESHAPI MaterialInformation
		{
		public:

			enum class PositionType
			{
				p2D,
				p3D
			};

			struct Shader
			{
				enum class Type
				{
					Vertex,
					Geometry,
					Fragment
				};

				Type type;
				std::string name;
			};

			struct Texture
			{
				bool isResource;
				std::string binding;
				std::string resource;
			};

			struct Uniform
			{
				std::string name;
				std::string type;
				size_t numComponents;
				std::any values[4];
			};

		private:

			std::string mName;

			PositionType mPositionType;

			std::vector<Shader> mShaders;

			std::vector<Texture> mTextures;

			std::vector<Uniform> mUniforms;

		public:

			MaterialInformation() {}

			explicit MaterialInformation(std::string const& name);

			std::string const& getName() const;

			void setPositionType(PositionType positionType);

			PositionType getPositionType() const;

			void addShader(Shader::Type type, std::string const& name);

			std::vector<Shader> const& getShaders() const;

			void addTexture(bool isResource, std::string const& binding, std::string const& resource);

			std::vector<Texture> const& getTextures() const;

			void addUniform(std::string const& name, size_t numComponents, int32_t const* values);

			void addUniform(std::string const& name, size_t numComponents, uint32_t const* values);

			void addUniform(std::string const& name, size_t numComponents, float const* values);

			std::vector<Uniform> const& getUniforms() const;
		};
	}
}