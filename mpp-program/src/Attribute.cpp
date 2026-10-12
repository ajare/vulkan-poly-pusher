#include <format>
#include "utils/StringUtils.h"

#include "Attribute.h"
#include "MppProgramException.h"

namespace mpp
{
	namespace program
	{

		using namespace std;

		string Attribute::getGlslType(mesh::Vertex::DataType dataType, size_t size[2]) const
		{
			string glslType;
			int componentSize = (int)(size[0] * size[1]);

			// Get dimension
			switch (componentSize)
			{
			case 1:
				glslType = ""; break;
			default:
				glslType = std::format("vec{}", componentSize);
			}

			// Get type
			switch (componentSize)
			{
			case 1:
				switch (dataType)
				{
				case mpp::mesh::Vertex::DataType::Float:
				case mpp::mesh::Vertex::DataType::HalfFloat:
					glslType = "float";
					break;
				case mpp::mesh::Vertex::DataType::Double:
					glslType = "double";
					break;
				case mpp::mesh::Vertex::DataType::Byte:
				case mpp::mesh::Vertex::DataType::Short:
				case mpp::mesh::Vertex::DataType::Int:
				case mpp::mesh::Vertex::DataType::Int_2_10_10_10_REV:
					glslType = normalised ? "float" : "int";
					break;
				case mpp::mesh::Vertex::DataType::UnsignedByte:
				case mpp::mesh::Vertex::DataType::UnsignedShort:
				case mpp::mesh::Vertex::DataType::UnsignedInt:
				case mpp::mesh::Vertex::DataType::UnsignedInt_2_10_10_10_REV:
					glslType = normalised ? "float" : "int";
					break;
				default:
					THROW_MPP_PROGRAM("Unknown data type.", __LINE__, __FILE__, __func__);
				}

			default:
				switch (dataType)
				{
				case mpp::mesh::Vertex::DataType::Double:
					glslType = "d" + glslType;
					break;
				case mpp::mesh::Vertex::DataType::Byte:
				case mpp::mesh::Vertex::DataType::Short:
				case mpp::mesh::Vertex::DataType::Int:
				case mpp::mesh::Vertex::DataType::Int_2_10_10_10_REV:
					if (!normalised)
					{
						glslType = "i" + glslType;
					}
					break;
				case mpp::mesh::Vertex::DataType::UnsignedByte:
				case mpp::mesh::Vertex::DataType::UnsignedShort:
				case mpp::mesh::Vertex::DataType::UnsignedInt:
				case mpp::mesh::Vertex::DataType::UnsignedInt_2_10_10_10_REV:
					if (!normalised)
					{
						glslType = "u" + glslType;
					}
					break;
				}
			}

			return glslType;
		}

	}
}