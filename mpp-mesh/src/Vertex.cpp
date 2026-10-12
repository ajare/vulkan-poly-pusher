#undef min
#undef max

#include "half/half.hpp"

#include "mpp/mesh/Vertex.h"
#include "mpp/mesh/MppMeshException.h"

using namespace std;

namespace mpp
{
	namespace mesh
	{

		/*
		 * Get the size of a component in 'units'.
		 *
		 */
		size_t Vertex::getComponentSize(Vertex::Component component)
		{
			switch (component)
			{
			case Vertex::Component::Unused:
				return 0;
			case Vertex::Component::Colour1:
			case Vertex::Component::UserDefined1:
				return 1;
			case Vertex::Component::Position2:
			case Vertex::Component::TexCoord2:
			case Vertex::Component::UserDefined2:
				return 2;
			case Vertex::Component::Position3:
			case Vertex::Component::Normal3:
			case Vertex::Component::TexCoord3:
			case Vertex::Component::Colour3:
			case Vertex::Component::UserDefined3:
				return 3;
			case Vertex::Component::Position4:
			case Vertex::Component::Normal4:
			case Vertex::Component::TexCoord4:
			case Vertex::Component::Colour4:
			case Vertex::Component::UserDefined4:
			case Vertex::Component::Tangent4:
				return 4;
			default:
				throw MppMeshException("Vertex::getComponentSize() unknown component!");
			}
		}

		/*
		 * Get the size of a datatype.
		 *
		 */
		size_t Vertex::getDataTypeSize(Vertex::DataType dataType)
		{
			switch (dataType)
			{
			case Vertex::DataType::None:
				return 0;
			case Vertex::DataType::Byte:
			case Vertex::DataType::UnsignedByte:
				return sizeof(char);
			case Vertex::DataType::Short:
			case Vertex::DataType::UnsignedShort:
				return sizeof(short);
			case Vertex::DataType::Int:
			case Vertex::DataType::UnsignedInt:
				return sizeof(int);
			case Vertex::DataType::HalfFloat:
				return sizeof(half_float::half);
			case Vertex::DataType::Float:
				return sizeof(float);
			case Vertex::DataType::Double:
				return sizeof(double);
			case Vertex::DataType::Int_2_10_10_10_REV:
			case Vertex::DataType::UnsignedInt_2_10_10_10_REV:
				return sizeof(int);
			default:
				throw MppMeshException("Vertex::getDataTypeSize() could not get size of data type!");
			}
		}

		bool Vertex::isDataTypeNormalisable(DataType dataType)
		{
			switch (dataType)
			{
			case Vertex::DataType::Byte:
			case Vertex::DataType::UnsignedByte:
			case Vertex::DataType::Short:
			case Vertex::DataType::UnsignedShort:
			case Vertex::DataType::Int:
			case Vertex::DataType::UnsignedInt:
			case Vertex::DataType::Int_2_10_10_10_REV:
			case Vertex::DataType::UnsignedInt_2_10_10_10_REV:
				return true;
			case Vertex::DataType::HalfFloat:
			case Vertex::DataType::Float:
			case Vertex::DataType::Double:
				return false;
			case Vertex::DataType::None:
			default:
				throw MppMeshException("Vertex::isDataTypeNormalisable() invalid data type!");
			}
		}

		bool Vertex::isDataTypeFloatingPoint(DataType dataType)
		{
			return dataType == DataType::HalfFloat || dataType == DataType::Float || dataType == DataType::Double;
		}

		string Vertex::getComponentName(Component component)
		{
			switch (component)
			{
			case Component::Position2:
				return "Position2";
			case Component::TexCoord2:
				return "TexCoord2";
			case Component::Position3:
				return "Position3";
			case Component::Normal3:
				return "Normal3";
			case Component::Normal4:
				return "Normal4";
			case Component::TexCoord3:
				return "TexCoord3";
			case Component::TexCoord4:
				return "TexCoord4";
			case Component::Colour1:
				return "Colour1";
			case Component::Colour3:
				return "Colour3";
			case Component::Position4:
				return "Position4";
			case Component::Colour4:
				return "Colour4";
			case Component::UserDefined1:
				return "UserDefined1";
			case Component::UserDefined2:
				return "UserDefined2";
			case Component::UserDefined3:
				return "UserDefined3";
			case Component::UserDefined4:
				return "UserDefined4";
			case Component::Tangent4:
				return "Tangent4";
			default:
				return "Unknown";
			}
		}

		string Vertex::getDataTypeName(DataType dataType)
		{
			switch (dataType)
			{
			case DataType::Byte:
				return "Int8";
			case DataType::UnsignedByte:
				return "UInt8";
			case DataType::Short:
				return "Int16";
			case DataType::UnsignedShort:
				return "UInt16";
			case DataType::Int:
				return "Int32";
			case DataType::UnsignedInt:
				return "UInt32";
			case DataType::HalfFloat:
				return "HalfFloat";
			case DataType::Float:
				return "Float";
			case DataType::Double:
				return "Double";
			case DataType::Int_2_10_10_10_REV:
				return "Int_2_10_10_10_REV";
			case DataType::UnsignedInt_2_10_10_10_REV:
				return "UnsignedInt_2_10_10_10_REV";
			default:
				return "Unknown";
			}
		}
	}
}