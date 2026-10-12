#pragma once

#include <string>

#include "glslTypes.h"

namespace mpp
{
	namespace program
	{

		struct Uniform
		{
			std::string qualifier;
			std::string name;
			GLSLTypeDecl type;
			size_t count;
			bool inBlock;
		};

	}
}