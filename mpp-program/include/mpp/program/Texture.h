#pragma once

#include <string>

#include "glslTypes.h"

namespace mpp
{
	namespace program
	{

		struct Texture
		{
			std::string name;
			GLSLTypeDecl type;
			size_t count;
		};

	}
}