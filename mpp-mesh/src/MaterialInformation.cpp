#include "mpp/mesh/MaterialInformation.h"

namespace mpp
{
	namespace mesh
	{

		using namespace std;

		MaterialInformation::MaterialInformation(string const& name)
			: mName(name)
		{
		}

		string const& MaterialInformation::getName() const
		{
			return mName;
		}

		void MaterialInformation::setPositionType(PositionType positionType)
		{
			mPositionType = positionType;
		}

		MaterialInformation::PositionType MaterialInformation::getPositionType() const
		{
			return mPositionType;
		}

		void MaterialInformation::addShader(Shader::Type type, std::string const& name)
		{
			Shader shader
			{
				type,
				name
			};

			mShaders.push_back(shader);
		}

		vector<MaterialInformation::Shader> const& MaterialInformation::getShaders() const
		{
			return mShaders;
		}

		void MaterialInformation::addTexture(bool isResource, string const& binding, string const& resource)
		{
			Texture t
			{
				isResource,
				binding,
				resource
			};

			mTextures.push_back(t);
		}

		vector<MaterialInformation::Texture> const& MaterialInformation::getTextures() const
		{
			return mTextures;
		}

		void MaterialInformation::addUniform(string const& name, size_t numComponents, int32_t const* values)
		{
			Uniform u
			{
				name,
				"int",
				numComponents
			};

			for (size_t i = 0; i < numComponents; ++i)
			{
				u.values[i] = values[i];
			}

			mUniforms.push_back(u);
		}

		void MaterialInformation::addUniform(string const& name, size_t numComponents, uint32_t const* values)
		{
			Uniform u
			{
				name,
				"uint",
				numComponents
			};

			for (size_t i = 0; i < numComponents; ++i)
			{
				u.values[i] = values[i];
			}

			mUniforms.push_back(u);
		}

		void MaterialInformation::addUniform(string const& name, size_t numComponents, float const* values)
		{
			Uniform u
			{
				name,
				"float",
				numComponents
			};

			for (size_t i = 0; i < numComponents; ++i)
			{
				u.values[i] = values[i];
			}

			mUniforms.push_back(u);
		}

		vector<MaterialInformation::Uniform> const& MaterialInformation::getUniforms() const
		{
			return mUniforms;
		}

	}
}