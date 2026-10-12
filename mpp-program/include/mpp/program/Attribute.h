#pragma once

#include <string>

#include "mpp/mesh/Vertex.h"

#include "Config.h"
#include "glslTypes.h"

namespace mpp
{
	namespace program
	{

		enum class AttributeType
		{
			Position,
			Normal,
			TexCoords,
			Colour,
			UserDefined
		};

		struct _MPPPROGRAMAPI Attribute
		{
			std::string name;
			GLSLTypeDecl type;
			bool normalised{ false };

		public:

			std::string getGlslType(mesh::Vertex::DataType dataType, size_t size[2]) const;
		};

	}
}