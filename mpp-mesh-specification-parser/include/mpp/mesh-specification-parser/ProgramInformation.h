#pragma once

#include <string>
#include <map>

#include "mpp/mesh-specification-parser/Config.h"

namespace mpp
{
	namespace mesh_specification_parser
	{
		class _MPPMESHSPECIFICATIONPARSERAPI ProgramInformation
		{
			std::string mName;

			std::string mVertexShader, mFragmentShader;

			std::map<std::string, std::string> mTextures;

			std::map<std::string, std::string> mUniforms;

		public:

			ProgramInformation() {}

			explicit ProgramInformation(std::string const& name);

			std::string const& getName() const;

			void setVertexShader(std::string const& shader);

			void setFragmentShader(std::string const& shader);

			void setTexture(std::string const& binding, std::string const &value);

			std::map<std::string, std::string> const& getTextures() const;

			void setUniform(std::string const& binding, std::string const &value);

			std::map<std::string, std::string> const& getUniforms() const;

		};
	}
}