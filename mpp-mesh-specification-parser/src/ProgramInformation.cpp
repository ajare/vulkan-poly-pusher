#include "mpp/mesh-specification-parser/ProgramInformation.h"

namespace mpp
{
	namespace mesh_specification_parser
	{

		using namespace std;

		ProgramInformation::ProgramInformation(string const& name)
			: mName(name)
			, mVertexShader("")
			, mFragmentShader("")
		{
		}

		string const& ProgramInformation::getName() const
		{
			return mName;
		}

		void ProgramInformation::setVertexShader(string const& shader)
		{
			mVertexShader = shader;
		}

		void ProgramInformation::setFragmentShader(string const& shader)
		{
			mFragmentShader = shader;
		}

		void ProgramInformation::setTexture(string const& binding, string const &value)
		{
			mTextures[binding] = value;
		}

		map<string, string> const& ProgramInformation::getTextures() const
		{
			return mTextures;
		}

		void ProgramInformation::setUniform(string const& binding, string const &value)
		{
			mUniforms[binding] = value;
		}

		map<string, string> const& ProgramInformation::getUniforms() const
		{
			return mUniforms;
		}

	}
}