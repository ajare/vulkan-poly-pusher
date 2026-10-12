#pragma once

#include <string>

#include <mpp/mesh/Vertex.h>

namespace mpp
{
	namespace program
	{

		enum class GLSLType
		{
			Unknown,
			Bool,
			Int,
			Uint,
			Float,
			Double,
			FloatMatrix,
			DoubleMatrix,
			User
		};

		struct GLSLTypeDecl
		{
			std::string name;
			GLSLType type;
			mesh::Vertex::DataType dataType;
			size_t size[2];
			bool isFloatingPoint;
			bool isSigned;
		};

		std::string getComponentIndexDefault(std::string const& component, bool isFloating, int index, std::string def = "");
	}
}