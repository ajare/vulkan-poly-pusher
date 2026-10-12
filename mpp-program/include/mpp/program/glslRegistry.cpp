#include <map>
#include <string>

#include "glslTypes.h"
#include "MppProgramException.h"

namespace mpp
{
	namespace program
	{

		using namespace std;

		map<string, GLSLTypeDecl> gsGLSLTypeDecls
		{
			{"",   {"user", GLSLType::User, mesh::Vertex::DataType::None, {1, 1}, false, false}},

			{"bool",   {"bool", GLSLType::Bool, mesh::Vertex::DataType::Int, {1, 1}, false, true}},
			{"int",    {"int", GLSLType::Int, mesh::Vertex::DataType::Int, {1, 1}, false, true}},
			{"uint",   {"uint", GLSLType::Uint, mesh::Vertex::DataType::UnsignedInt, {1, 1}, false, false}},
			{"float",  {"float", GLSLType::Float, mesh::Vertex::DataType::Float, {1, 1}, true, true}},
			{"double", {"double", GLSLType::Double, mesh::Vertex::DataType::Double, {1, 1}, true, true}},

			{"bvec2", {"bvec2", GLSLType::Bool, mesh::Vertex::DataType::Int, {1, 2}, false, true}},
			{"ivec2", {"ivec2", GLSLType::Int, mesh::Vertex::DataType::Int, {1, 2}, false, true}},
			{"uvec2", {"uvec2", GLSLType::Uint, mesh::Vertex::DataType::UnsignedInt, {1, 2}, false, false}},
			{"vec2",  {"vec2", GLSLType::Float, mesh::Vertex::DataType::Float, {1, 2}, true, true}},
			{"dvec2", {"dvec2", GLSLType::Double, mesh::Vertex::DataType::Double, {1, 2}, true, true}},
			
			{"bvec3", {"bvec3", GLSLType::Bool, mesh::Vertex::DataType::Int, {1, 3}, false, true}},
			{"ivec3", {"ivec3", GLSLType::Int, mesh::Vertex::DataType::Int, {1, 3}, false, true}},
			{"uvec3", {"uvec3", GLSLType::Uint, mesh::Vertex::DataType::UnsignedInt, {1, 3}, false, false}},
			{"vec3",  {"vec3", GLSLType::Float, mesh::Vertex::DataType::Float, {1, 3}, true, true}},
			{"dvec3", {"dvec3", GLSLType::Double, mesh::Vertex::DataType::Double, {1, 3}, true, true}},

			{"bvec4", {"bvec4", GLSLType::Bool, mesh::Vertex::DataType::Int, {1, 4}, false, true}},
			{"ivec4", {"ivec4", GLSLType::Int, mesh::Vertex::DataType::Int, {1, 4}, false, true}},
			{"uvec4", {"uvec4", GLSLType::Uint, mesh::Vertex::DataType::UnsignedInt, {1, 4}, false, false}},
			{"vec4",  {"vec4", GLSLType::Float, mesh::Vertex::DataType::Float, {1, 4}, true, true}},
			{"dvec4", {"bvec4", GLSLType::Double, mesh::Vertex::DataType::Double, {1, 4}, true, true}},

			{"mat2", {"mat2", GLSLType::FloatMatrix, mesh::Vertex::DataType::Float, {2, 2}, true, true}},
			{"dmat2", {"dmat2", GLSLType::DoubleMatrix, mesh::Vertex::DataType::Double, {2, 2}, true, true}},

			{"mat3", {"mat3", GLSLType::FloatMatrix, mesh::Vertex::DataType::Float, {3, 3}, true, true}},
			{"dmat3", {"dmat3", GLSLType::DoubleMatrix, mesh::Vertex::DataType::Double, {3, 3}, true, true}},

			{"mat4", {"mat4", GLSLType::FloatMatrix, mesh::Vertex::DataType::Float, {4, 4}, true, true}},
			{"dmat4", {"dmat4", GLSLType::DoubleMatrix, mesh::Vertex::DataType::Double, {4, 4}, true, true}},

			{"mat2x3", {"mat2x3", GLSLType::FloatMatrix, mesh::Vertex::DataType::Float, {2, 3}, true, true}},
			{"dmat2x3", {"dmat2x3", GLSLType::DoubleMatrix, mesh::Vertex::DataType::Double, {2, 3}, true, true}},

			{"mat2x4", {"mat2x4", GLSLType::FloatMatrix, mesh::Vertex::DataType::Float, {2, 4}, true, true}},
			{"dmat2x4", {"dmat2x4", GLSLType::DoubleMatrix, mesh::Vertex::DataType::Double, {2, 4}, true, true}},

			{"mat3x2", {"mat3x2", GLSLType::FloatMatrix, mesh::Vertex::DataType::Float, {3, 2}, true, true}},
			{"dmat3x2", {"dmat3x2", GLSLType::DoubleMatrix, mesh::Vertex::DataType::Double, {3, 2}, true, true}},

			{"mat3x4", {"mat3x4", GLSLType::FloatMatrix, mesh::Vertex::DataType::Float, {3, 4}, true, true}},
			{"dmat3x4", {"dmat3x4", GLSLType::DoubleMatrix, mesh::Vertex::DataType::Double, {3, 4}, true, true}},

			{"mat4x2", {"mat4x2", GLSLType::FloatMatrix, mesh::Vertex::DataType::Float, {4, 2}, true, true}},
			{"dmat4x2", {"dmat4x2", GLSLType::DoubleMatrix, mesh::Vertex::DataType::Double, {4, 2}, true, true}},

			{"mat4x3", {"mat4x3", GLSLType::FloatMatrix, mesh::Vertex::DataType::Float, {4, 3}, true, true}},
			{"dmat4x3", {"dmat4x3", GLSLType::DoubleMatrix, mesh::Vertex::DataType::Double, {4, 3}, true, true}}
		};

		string getComponentIndexDefault(string const& component, bool isFloating, int index, string def)
		{
			if (component == "POSITION")
			{
				switch (index)
				{
				case 2: 
					return isFloating ? "0.0" : "0";

				case 3: 
					return isFloating ? "1.0" : "1";

				default:
					THROW_MPP_PROGRAM("Bad index for component '" + component + "'.", __LINE__, __FILE__, __func__);
				}
			}
			else if (component == "NORMAL")
			{
				switch (index)
				{
				case 3:
					return isFloating ? "1.0" : "1";

				default:
					THROW_MPP_PROGRAM("Bad index for component '" + component + "'.", __LINE__, __FILE__, __func__);
				}

			}
			else if (component == "TEXCOORDS")
			{
				switch (index)
				{
				case 2:
				case 3:
					return isFloating ? "1.0" : "1";

				default:
					THROW_MPP_PROGRAM("Bad index for component '" + component + "'.", __LINE__, __FILE__, __func__);
				}

			}
			else if (component == "COLOUR")
			{
				switch (index)
				{
				case 1:
				case 2:
					return def;

				case 3:
					return isFloating ? "1.0" : "1";

				default:
					THROW_MPP_PROGRAM("Bad index for component '" + component + "'.", __LINE__, __FILE__, __func__);
				}
			}
			else if (component == "USER")
			{
				switch (index)
				{
				case 1:
				case 2:
				case 3:
				case 4:
					return isFloating ? "1.0" : "1";

				default:
					THROW_MPP_PROGRAM("Bad index for component '" + component + "'.", __LINE__, __FILE__, __func__);
				}
			}
			else
			{
				return isFloating ? "1.0" : "1";;
			}
		}

	}
}